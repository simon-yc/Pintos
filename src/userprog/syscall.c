#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

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
  if (is_kernel_vaddr (usrc_))
    return false;
  if (pagedir_get_page (thread_current ()->pagedir, usrc_) == NULL)
   	return false;
	return true;
}

static bool
copy_in (void *dst_, const void *usrc_, size_t size)
{
  // error checking if address is valid
  if (! valid_check (usrc_))
    return false;

  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    *dst = get_user (usrc);
  return true;
}

static void
handle_umimplement (int syscall_number)
{
  thread_exit ();
}

static void
handle_exit (struct intr_frame *f)
{
  thread_current ()->exit_code = *(((int *) f->esp) + 1);
  thread_exit ();
}
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];  
  //extract the syscall number
  if (!copy_in(&syscall_number, f->esp, sizeof syscall_number))
    handle_exit (f);
  // printf("  ***syscall number: %u (should be %u)\n", syscall_number, SYS_WRITE);

  switch (syscall_number) {
  	case 0: // halt
      {
        shutdown_power_off (syscall_number);
        break;
      }
    case 1: // exit
      {
        handle_exit (f);
        break;
      }
    case 2: // exec
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 3: // wait
      {
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 1))
          handle_exit (f);
  	    f->eax = (uint32_t) process_wait (args[2]);
        break;
      }
    case 4: // create
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 5: // remove
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 6: // open
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 7: // filesize
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 8: // read
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 9: // write
      {
        //extract the 3 arguements
        if (!copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3))
          handle_exit (f);
        // printf("  ***fd: %d (should be %u)\n", args[0], STDOUT_FILENO);
        // printf("  ***buffer address: %p\n", args[1]);
        // printf("  ***size: %u\n", args[2]);

        //execute the write on STDOUT_FILENO
        putbuf (args[1], args[2]);

        //set the returned value
        f->eax = args[2];
        break;
      }
    case 10:  // seek
      {
        handle_umimplement (syscall_number);
        break;

      }
    case 11:  // tell
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 12:  // close
      {
        handle_umimplement (syscall_number);
        break;
      }
  }
}

