#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

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

static void
copy_in (void *dst_, const void *usrc_, size_t size)
{
  uint8_t *dst = dst_;
  const uint8_t *usrc = usrc_;

  for (; size > 0; size--, dst++, usrc++)
    *dst = get_user (usrc);

  // TODO: error checking if address is valid
}

static bool
valid_check (struct intr_frame *f UNUSED)
{
  if (f == NULL)
    return false;
  if (is_kernel_vaddr (f->esp))
    return false;
  if (pagedir_get_page (thread_current ()->pagedir, f->esp) == NULL)
   	return false;
	return true;
}

static void
handle_umimplement (int syscall_number)
{
  thread_exit ();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  unsigned syscall_number;
  int args[3];
  // check if valid address
  if (! valid_check (f))
    {
      thread_current ()->exit_code = *(((int *) f->esp) + 1);
      thread_exit ();
    }
  //extract the syscall number
  copy_in(&syscall_number, f->esp, sizeof syscall_number);
  // printf("  ***syscall number: %u (should be %u)\n", syscall_number, SYS_WRITE);

  switch (syscall_number) {
  	case 0:
      {
        shutdown_power_off (syscall_number);
        break;
      }
    case 1:
      {
        thread_current ()->exit_code = *(((int *) f->esp) + 1);
        thread_exit ();
        break;
      }
    case 2:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 3:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 4:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 5:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 6:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 7:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 8:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 9:
      {
        //extract the 3 arguements
        copy_in(args, (uint32_t *) f->esp + 1, sizeof *args * 3);
        // printf("  ***fd: %d (should be %u)\n", args[0], STDOUT_FILENO);
        // printf("  ***buffer address: %p\n", args[1]);
        // printf("  ***size: %u\n", args[2]);

        //execute the write on STDOUT_FILENO
        putbuf (args[1], args[2]);

        //set the returned value
        f->eax = args[2];
        break;
      }
    case 10:
      {
        handle_umimplement (syscall_number);
        break;

      }
    case 11:
      {
        handle_umimplement (syscall_number);
        break;
      }
    case 12:
      {
        handle_umimplement (syscall_number);
        break;
      }
  }
}

