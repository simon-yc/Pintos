#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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

static void
handle_halt (void)
{
  shutdown_power_off ();
}

static void
handle_exit (const char *cmd_line)
{
  thread_current ()->exit_code = cmd_line;
  thread_exit ();
}

static pid_t
handle_exec (const char *cmd_line)
{
	//printf("System call: exec\ncmd_line: %s\n", cmd_line);
  tid_t child_tid = TID_ERROR;

  // if(!valid_mem_access(cmd_line))
  //   handle_bad_addr (-1);

  child_tid = process_execute (cmd_line);

	return child_tid;
}

static void
handle_create (void)
{
  thread_exit ();
}

static void
handle_remove (void)
{
  thread_exit ();
}

static int
handle_open (const char *file)
{
//   lock_acquire(&file_system_lock);
//   struct file *file_ptr = filesys_open(file_name); // from filesys.h
//   if (!file_ptr)
//   {
//     lock_release(&file_system_lock);
//     return ERROR;
//   }
//   int filedes = add_file(file_ptr);
//   lock_release(&file_system_lock);
//   return filedes;
  thread_exit ();
}

static void
handle_filesize (void)
{
  thread_exit ();
}

static void
handle_read (void)
{
  thread_exit ();
}

static void
handle_write (int args[], struct intr_frame *f)
{
  putbuf (args[1], args[2]);
  f->eax = args[2];
}

static void
handle_seek (void)
{
  thread_exit ();
}

static void
handle_tell (void)
{
  thread_exit ();
}

static void
handle_close (void)
{
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f) 
{
  unsigned syscall_number;
  //extract the syscall number
  if (!copy_in (&syscall_number, f->esp, sizeof syscall_number))
    handle_bad_addr (f);
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
        f->eax = (uint32_t) handle_exec (args[0]);
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
        handle_create ();
        break;
      }
    case 5: // remove
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_remove ();
        break;
      }
    case 6: // open
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_open (args[0]);
        break;
      }
    case 7: // filesize
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_filesize ();
        break;
      }
    case 8: // read
      {
        int args[3];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3))
          handle_bad_addr (f);
        handle_read ();
        break;
      }
    case 9: // write
      {
        int args[3];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3))
          handle_bad_addr (f);
        handle_write (args, f);
        break;
      }
    case 10:  // seek
      {
        int args[2];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 2))
          handle_bad_addr (f);
        handle_seek ();
        break;

      }
    case 11:  // tell
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_tell ();
        break;
      }
    case 12:  // close
      {
        int args[1];
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_bad_addr (f);
        handle_close ();
        break;
      }
  }
}