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
#include "threads/malloc.h"
#include <string.h>
#include "vm/page.h"
#include "threads/palloc.h"
#include "devices/timer.h"

static void syscall_handler (struct intr_frame *);
static int get_user(const uint8_t *uaddr);
static bool valid_check (const void *usrc_, size_t size);
static bool copy_in (void *dst_, const void *usrc_, size_t size);
static struct file *find_opened_file (int fd);

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

/* P3 update - helper function to validate user provided pointer value address 
   before using. */
static bool
valid_check (const void *usrc_, size_t size)
{
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, usrc++)
    {
      if (usrc_ == NULL || !is_user_vaddr (usrc))
        return false;

      /* Check if current supplimental page contails a page for this 
         address */
      struct page *page = find_page (usrc, false);
      if (page == NULL)
        {
          /* If not in the page check if its an action of PUSH or PUSHA, if it
             is those situations, find_page will allocate a page
             and retuen it as grow is set to true. */
          page = find_page (usrc, true);
          if (page == NULL)
            return false;

          /* Load page into stack */
          if (!page_load_helper (page))
            return false;
        }
      
      /* Map the page to kernal address */
      if (page->frame == NULL && !page_load_helper (page))
        return false;
    }

  return true;
}

/* P2 update - helper function for reading user address. */
static bool
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    {
      /* error checking if address is valid */
      if (!valid_check (usrc, 1))
        return false;
      else
        *dst = get_user (usrc);
    }
  return true;
}

/* P2 update - helper function for finding file with file descriptor = fd 
   opened from the current thread */
static struct file*
find_opened_file (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->opened_files); 
       e != list_end (&cur->opened_files);
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

/* P2 update - system call for halt */
void
handle_halt (void)
{
  shutdown_power_off ();
}

/* P2 update - system call for exit */
void
handle_exit (int status)
{
  thread_current ()->exit_code = status;
  /* Allow writes to file not in use as executables */
  while (thread_current ()->fd > 1)
    {
      handle_close(thread_current ()->fd);
    }
  thread_exit ();
}

/* P2 update - system call for exec */
pid_t
handle_exec (const char *cmd_line)
{
  tid_t tid;
  lock_acquire (&filesys_lock);
  tid = process_execute (cmd_line);
  lock_release (&filesys_lock);
  return tid;
} 

/* P2 update - system call for create */
bool
handle_create (const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool success = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return success;
}

/* P2 update - system call for remove */
bool
handle_remove (const char *file)
{
  lock_acquire (&filesys_lock);
  bool success = filesys_remove (file);
  lock_release (&filesys_lock);
  return success;
}

/* P2 update - system call for open */
int
handle_open (const char *file)
{
  lock_acquire (&filesys_lock);
  struct file *open_file = filesys_open (file);
  lock_release (&filesys_lock);
  if (open_file == NULL)
    return -1;

  /* Store the opened file into current thread's opened files list. */
  struct opened_file *new_file = malloc (sizeof (struct opened_file));
  if (new_file == NULL)
    return -1;
  thread_current ()->fd++;
  new_file->file = open_file;
  new_file->fd = thread_current ()->fd;
  list_push_back (&thread_current ()->opened_files, &new_file->file_elem);
  return new_file->fd;
}

/* P2 update - system call for filesize */
int
handle_filesize (int fd)
{
  lock_acquire (&filesys_lock);
  int size = -1; 
  
  /* find file with fd = fd in current thread's opened files list. */
  struct file *file = find_opened_file (fd);
  if (file)
    size = file_length (file);
  lock_release (&filesys_lock);
  return size;
}

/* P2 update - system call for read */
int
handle_read (int fd, void *buffer, unsigned size)
{
  struct page *p = find_page (buffer, false);
  if (p->read_only)
    handle_exit (-1);
  if (fd == STDIN_FILENO)
    {
      uint8_t *temp_buf = (uint8_t *) buffer;
      if (!valid_check (temp_buf, size))
          handle_exit (-1);
      for (unsigned i = 0; i < size; i ++)
        {
          temp_buf[i] = input_getc ();
        }
      return size;
    }
  uint8_t *udst = buffer;
  int bytes_read = 0;
  struct file *file;

  file = find_opened_file (fd);
  if (!file)
    handle_exit (-1);

  while (size > 0)
    {
      /* check remaning read */
      size_t remaining = PGSIZE - pg_ofs (udst);
      size_t temp_size = remaining;
      if (size < remaining)
          temp_size = size;

      /* Read from file into page. */
      if (!valid_check (udst, temp_size))
        handle_exit (-1);
      lock_acquire (&filesys_lock);
      off_t read_num = file_read (file, udst, temp_size);
      lock_release (&filesys_lock);
      /* Check if read success. */
      if (read_num < 0)
        {
          if (bytes_read == 0)
            bytes_read = -1;
          break;
        }
      bytes_read += read_num;
      if (read_num != (off_t) temp_size)
          /* Finished reading. */
          break;
      
      udst += read_num;
      size -= read_num;
    }

  return bytes_read;
}

/* Write system call. */
int
handle_write (int fd, void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO)
    {
      if (!valid_check (buffer, size))
        /* Invalid address. */
        handle_exit (-1);
      putbuf ((const char *) buffer, size);
      return size;
    }
  uint8_t *temp_buffer = buffer;
  int bytes_written = 0;
  struct file *file;

  file = find_opened_file (fd);
  if (!file)
    handle_exit (-1);

  while (size > 0)
    {
      size_t remaining = PGSIZE - pg_ofs (temp_buffer);
      size_t temp_size = remaining;
      if (size < remaining)
          temp_size = size;

      if (!valid_check (temp_buffer, temp_size))
        /* Invalid address. */
        handle_exit (-1);

      lock_acquire (&filesys_lock);
      off_t retval = file_write (file, temp_buffer, temp_size);
      lock_release (&filesys_lock);

      /* Check if write success. */
      if (retval < 0)
        {
          if (bytes_written == 0)
            bytes_written = -1;
          break;
        }
      bytes_written += retval;

      if (retval != (off_t) temp_size)
        /* Finished reading. */
        break;
      
      temp_buffer += retval;
      size -= retval;
    }

  return bytes_written;
}


/* P2 update - system call for seek */
void
handle_seek (int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  struct file *file = find_opened_file (fd);
  if (file)
    file_seek (file, position);
  lock_release (&filesys_lock);
}

/* P2 update - system call for tell */
unsigned
handle_tell (int fd)
{
  unsigned next = -1;
  lock_acquire (&filesys_lock);
  struct file *file = find_opened_file (fd);
  if (file)
    next = file_tell (file);
  lock_release (&filesys_lock);
  return next;
}

/* P2 update - system call for close */
void
handle_close (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->opened_files); 
       e != list_end (&cur->opened_files);
       e = list_next (e))
    {
      struct opened_file *f = list_entry (e, struct opened_file, file_elem);
      if (fd == f->fd)
        {
          list_remove (&f->file_elem);
          lock_acquire (&filesys_lock);
          file_close (f->file);
          lock_release (&filesys_lock);
          cur->fd--;
          free (f);
          break;
        }
    }
}

/* P3 update - system call for mmap */
mapid_t
handle_mmap (int fd, void *addr)
{
  if (fd == 0 || fd == 1 || addr == NULL || addr == 0 || pg_ofs (addr) != 0)
    return -1;
  
  /* find file in current thread's opened files list. */
  struct file *open_file = find_opened_file (fd);
  if (open_file == NULL)
    return -1;
  struct file_map *map = malloc (sizeof *map);
  if (map == NULL)
    return -1;

  /* Get file and file len */
  map->fd = thread_current ()->fd;
  lock_acquire (&filesys_lock);
  map->file = file_reopen (open_file);
  lock_release (&filesys_lock);
  if (map->file == NULL)
    {
      /* If file reopen failed, free map and return -1 */
      free (map);
      return -1;
    }

  list_push_front (&thread_current ()->file_maps, &map->elem);
  map->vaddr = addr;
  map->page_num = 0;
  off_t offset = 0;
  lock_acquire (&filesys_lock);
  off_t file_len = file_length (map->file);
  lock_release (&filesys_lock);
  while (file_len > 0)
    {
      struct page *p = page_allocation ((uint8_t *) addr + offset, false);
      if (p == NULL)
        {
          /* If page allocation failed, unmap as some pages may be already 
             allocated, and return -1 */
          handle_munmap (fd);
          return -1;
        }
      p->private = false;
      p->file = map->file;
      p->file_offset = offset;
      if (file_len >= PGSIZE)
        p->file_bytes = PGSIZE;
      else
        p->file_bytes = file_len;
      offset += p->file_bytes;
      file_len -= p->file_bytes;
      map->page_num++;
    }
  return map->fd;
}

/* P3 update - system call for munmap */
void
handle_munmap (mapid_t mapping)
{
  struct file_map *map = NULL;
  struct thread *cur = thread_current ();

  for (struct list_elem *e = list_begin (&cur->file_maps); 
       e != list_end (&cur->file_maps);
       e = list_next (e))
    {
      /* find the file map with mapping id = mapping. */
      map = list_entry (e, struct file_map, elem);
      if (map->fd == mapping)
        break;
    }
  if (map == NULL)
    /* if the file map is not found, exit. */
    thread_exit ();
  /* Remove the file map from the file map list. */
  list_remove (&map->elem);
  for(int i = 0; i < map->page_num; i++)
    {
      if (pagedir_is_dirty (thread_current ()->pagedir, 
                            ((const void *) ((map->vaddr) + (PGSIZE * i)))))
        {
          /* For each page, if the page is dirty, write it back to the 
             file. */
          lock_acquire (&filesys_lock);
          file_write_at (map->file, 
                         (const void *) (map->vaddr + (PGSIZE * i)), 
                         (PGSIZE * (map->page_num)), (PGSIZE * i));
          lock_release (&filesys_lock);
        }
      /* For each page, if the page is in the supplemental page table, 
         remove it. */
      page_clear ((void *) ((map->vaddr) + (PGSIZE * i)));
    }
}

/* P3 update - unmap when exit */
void
files_exit (void)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  for (e = list_begin (&cur->file_maps); e != list_end (&cur->file_maps);
       e = list_next (e))
    handle_munmap (list_entry (e, struct file_map, elem)->fd);
}

static void
syscall_handler (struct intr_frame *f) 
{
  unsigned syscall_number;

  /* extract the syscall number */
  if (!copy_in (&syscall_number, (const void *) f->esp, 
      sizeof syscall_number))
    handle_exit (-1);

  switch (syscall_number) 
    {
      case SYS_HALT:
        {
          handle_halt ();
          break;
        }
      case SYS_EXIT:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          handle_exit (args[0]);
          break;
        }
      case SYS_EXEC:
        {
          int args[1];
          /* Check if the address of arguments are valid */
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          /* Check each byte of actural argument are mapped to physicla 
             space */
          if (!valid_check ((const char *) args[0], sizeof *args))
            handle_exit (-1);
          f->eax = (uint32_t) handle_exec ((const char *) args[0]);
          break;
        }
      case SYS_WAIT:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          f->eax = (uint32_t) process_wait (args[0]);
          break;
        }
      case SYS_CREATE:
        {
          int args[2];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 2))
            handle_exit (-1);
          /* Check each byte of actural argument are mapped to physicla 
             space */
          if (!valid_check ((const char *) args[0], sizeof *args))
            handle_exit (-1);
          f->eax = (uint32_t) handle_create ((const char *) args[0], args[1]);
          break;
        }
      case SYS_REMOVE:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          /* Check the actural arguments are mapped to physicla space */
          if (!valid_check ((const char *) args[0], sizeof *args))
            handle_exit (-1);
          f->eax = (uint32_t) handle_remove ((const char *) args[0]);
          break;
        }
      case SYS_OPEN: 
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          /* Check the actural arguments are mapped to physicla space */
          if (!valid_check ((const char *) args[0], sizeof *args))
            handle_exit (-1);
          f->eax = handle_open ((const char *) args[0]);
          break;
        }
      case SYS_FILESIZE:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          f->eax = handle_filesize (args[0]);
          break;
        }
      case SYS_READ:
        {
          int args[3];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 3))
            handle_exit (-1);
          /* Check each byte of actural argument are mapped to physical 
             space */
          if (!valid_check ((const char *) args[1], args[2]))
            handle_exit (-1);
          f->eax = handle_read (args[0], (void *) args[1], 
                                (unsigned) args[2]);
          break;
        }
      case SYS_WRITE:
        {
          int args[3];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 3))
            handle_exit (-1);
          /* Check each byte of actural argument are mapped to physicla  
             space */
          if (!valid_check ((const char *) args[1], args[2]))
            handle_exit (-1);
          f->eax = handle_write (args[0], (void *) args[1], 
                                 (unsigned) args[2]);
          break;
        }
      case SYS_SEEK:
        {
          int args[2];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 2))
            handle_exit (-1);
          handle_seek (args[0], args[1]);
          break;
        }
      case SYS_TELL:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          f->eax = handle_tell (args[0]);
          break;
        }
      case SYS_CLOSE:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          handle_close (args[0]);
          break;
        }
      case SYS_MMAP:
        {
          int args[2];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 2))
            handle_exit (-1);
          f->eax = handle_mmap (args[0], (void *) args[1]);
          break;
        }
      case SYS_MUNMAP:
        {
          int args[1];
          if (!copy_in (args, (uint32_t *) f->esp + 1, sizeof *args * 1))
            handle_exit (-1);
          handle_munmap (args[0]);
          break;
        }
      default:
        handle_exit (-1);
    }
}
