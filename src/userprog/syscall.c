#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include <list.h>
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

/* P2 updates */
/* Reads a byte at user virtual address UADDR. 
   UADDR must be below PHYS_BASE. 
   RETURNs the byte value if successful, -1 if a segfault occured. */
static int
get_user(const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Writes BYTE to user address UDST. 
   UDST must be below PHYS_BASE. 
   RETURNS true if successful, false if a segfault occured. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
        : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

static bool
valid_check (const void *usrc_)
{
  if (usrc_ == NULL || !is_user_vaddr(usrc_) ||
      pagedir_get_page (thread_current ()->pagedir, usrc_) == NULL)
    return false;
	return true;
}

static bool
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    // error checking if address is valid
    if (!valid_check (usrc_))
      return false;
    else
      *dst = get_user (usrc);
  return true;
}

static void
handle_bad_addr (struct intr_frame *f)
{
  thread_current ()->exit_code = -1;
  thread_exit ();
}

static struct file*
find_opened_file (int fd)
{
  struct thread *cur = thread_current();
  struct list_elem *e;
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e))
    {
      struct opened_file *f = list_entry (e, struct opened_file, file_elem);
      if (fd == f->fd)
        {
          return f->file;
        }
    }
  return NULL;
}

static void
handle_halt (void)
{
  shutdown_power_off ();
}

static void
handle_exit (int status)
{
  thread_current ()->exit_code = status;
  thread_exit ();
}

static pid_t
handle_exec (const char *cmd_line)
{
  tid_t child_pid = -1;
  child_pid = process_execute (cmd_line);
	return child_pid;
}

static bool
handle_create (const char* file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

static bool
handle_remove (const char* file)
{
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

static int
handle_open (const char *file)
{
  lock_acquire(&filesys_lock);
  struct file *open_file = filesys_open(file);
  lock_release(&filesys_lock);
  if (open_file == NULL)
    {
      return -1;
    }
  else
    {
      /* Store the opened file into current thread's opened files list. */
      struct opened_file *new_file = malloc(sizeof(struct opened_file));
      new_file->file = open_file;
      new_file->fd = thread_current()->fd;
      thread_current()->fd++;
      list_push_back(&thread_current()->opened_files, &new_file->file_elem);
      return new_file->fd;
    }
}

static int
handle_filesize (int fd)
{
  lock_acquire(&filesys_lock);
  int size = -1; 
  /* find file with fd = fd in current thread's opened files list. */
  struct file *file = find_opened_file (fd);
  if (file)
    {
      size = file_length(file);
    }
  lock_release(&filesys_lock);
  return size;
}

static int
handle_read (int fd, void *buffer, unsigned size)
{
  if (fd == STDIN_FILENO)
    {
      uint8_t *temp_buf = (uint8_t *) buffer;
      for (unsigned i = 0; i < size; i ++)
        {
          temp_buf[i] = input_getc();
        }
      return size;
    }
  else 
    {
      lock_acquire(&filesys_lock);
      /* find file with fd = fd in current thread's opened files list. */
      struct file *file = find_opened_file (fd);
      if (!file)
        {
          lock_release(&filesys_lock);
          return -1;
        }
      int bytes_read = file_read(file, buffer, size);
      lock_release (&filesys_lock);
      return bytes_read;
    }
}

static int
handle_write (int fd, void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
    {
      putbuf ((const char *) buffer, size);
      return size;
    }
  else
    {
      lock_acquire(&filesys_lock);
      /* find file with fd = fd in current thread's opened files list. */
      struct file *file = find_opened_file (fd);
      if (!file)
        {
          lock_release (&filesys_lock);
          return -1;
        }
      int bytes_write = file_write (file, buffer, size);
      lock_release (&filesys_lock);
      return bytes_write;
    }
}

static void
handle_seek (int fd, unsigned position)
{
  lock_acquire(&filesys_lock);
  struct file *file = find_opened_file (fd);
  if (file)
    {
      file_seek (file, position);
    }
  lock_release (&filesys_lock);
}

static unsigned
handle_tell (int fd)
{
  unsigned next = -1;
  lock_acquire(&filesys_lock);
  struct file *file = find_opened_file (fd);
  if (file)
    {
      next = file_tell(file);
    }
  lock_release (&filesys_lock);
  return next;
}

static void
handle_close (int fd)
{
  lock_acquire(&filesys_lock);
  struct thread *cur = thread_current();
  struct list_elem *e;
  for (e = list_begin (&cur->opened_files); e != list_end (&cur->opened_files);
       e = list_next (e))
    {
      struct opened_file *f = list_entry (e, struct opened_file, file_elem);
      if (fd == f->fd)
        {
          list_remove(&f->file_elem);
          file_close(f->file);
        }
    }
  lock_release(&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  unsigned syscall_number;
  //extract the syscall number
  if (!copy_in (&syscall_number, f->esp, sizeof syscall_number))
    {
      handle_bad_addr (f);
    }
  // printf("  ***syscall number: %u \n", syscall_number);

  switch (syscall_number) {
  	case 0: // halt
      {
        handle_halt ();
        break;
      }
    case 1: // exit
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_exit (args[0]);
        break;
      }
    case 2: // exec
      {
  	    int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[0]))
          handle_bad_addr (f);
        f->eax = (uint32_t) handle_exec ((const char *) args[0]);
        break;
      }
    case 3: // wait
      {
  	    int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        f->eax = (uint32_t) process_wait (args[0]);
        break;
      }
    case 4: // create
      {
        int args[2];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 2))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[0]))
          handle_bad_addr (f);
        f->eax = (uint32_t) handle_create ((const char *) args[0], args[1]);
        break;
      }
    case 5: // remove
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[0]))
          handle_bad_addr (f);
        f->eax = (uint32_t) handle_remove ((const char *) args[0]);
        break;
      }
    case 6: // open
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[0]))
          handle_bad_addr (f);
        f->eax = handle_open ((const char *) args[0]);
        break;
      }
    case 7: // filesize
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        f->eax = handle_filesize (args[0]);
        break;
      }
    case 8: // read
      {
        int args[3];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[1]))
          handle_bad_addr (f);
        f->eax = handle_read (args[0], (void *) args[1], (unsigned) args[2]);
        break;
      }
    case 9: // write
      {
        int args[3];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3))
          handle_bad_addr (f);
        if (!valid_check((const char *) args[1]))
          handle_bad_addr (f);
        f->eax = handle_write (args[0], (void *) args[1], (unsigned) args[2]);
        break;
      }
    case 10:  // seek
      {
        int args[2];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 2))
          handle_bad_addr (f);
        handle_seek (args[0], args[1]);
        break;
      }
    case 11:  // tell
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        f->eax = handle_tell (args[0]);
        break;
      }
    case 12:  // close
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_close (args[0]);
        break;
      }
  }
}