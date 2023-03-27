#include "vm/page.h"
#include <stdio.h>
#include <string.h>
#include "vm/frame.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

/* Maximum size of process stack bytes. */
#define STACK_MAX (1024 * 1024)

/* Allocate a page for user and add the page to user pages hash tabale.
   Return the page if successful, otherwise null  */
struct page *page_allocation (void *vaddr, bool read_only)
{
  struct thread *cur = thread_current ();
  struct page *p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;
  p->vaddr = pg_round_down (vaddr);
  p->read_only = read_only;
  p->private = !read_only;
  p->frame = NULL;
  p->file = NULL;
  p->thread = cur;
  p->sector = (block_sector_t) - 1;
  
  /* Add this page to current thread's page table. */
  if (hash_insert (cur->sup_page_table, &p->hash_elem) == NULL)
    return p;
    
  free (p);
  return NULL;
}

/* Returm the hash value */
unsigned
page_get_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, hash_elem);
  return ((uintptr_t) p->vaddr) >> PGBITS;
}

void
zeroing_page (struct page *p)
{
  memset (p->frame->kernel_virtual_address, 0, PGSIZE);
}

void 
load_from_file (struct page *p)
{
  off_t read_bytes = file_read_at (p->file, 
                                    p->frame->kernel_virtual_address,
                                    p->file_bytes, p->file_offset);
  off_t zero_bytes = PGSIZE - read_bytes;
  memset (p->frame->kernel_virtual_address + read_bytes, 0, zero_bytes);
}


/* Hash comparison helper function */
bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  return hash_entry (a, struct page, hash_elem)->vaddr 
         < hash_entry (b, struct page, hash_elem)->vaddr;
}

bool
page_load_helper (struct page *p)
{
  bool success;
  frame_acquire_lock (p);
  if (p->frame == NULL)
    {
      /* Allocate a frame for page p. */
      p->frame = frame_allocation (p);
      if (p->frame == NULL)
        return false;
      else if (p->file != NULL)
        /* Load data from the file. */
        load_from_file (p);
      else
        /* Zero the page. */
        zeroing_page(p);
      success = true;
    }
  /* Install frame into page table. */
  success = pagedir_set_page (thread_current ()->pagedir, p->vaddr,
                              p->frame->kernel_virtual_address, !p->read_only);
  /* Release frame. */
  frame_release_lock (p);
  return success;
}

/* Lazy loading, page load in and return if successed. */
bool
page_load (void *fault_addr)
{
  struct page *p;

  /* if current thread do not have any pages, return false */
  if (thread_current ()->sup_page_table == NULL)
    return false;

  /* if no page correspond to the address or page allocation not successful, 
     return false */
  p = find_page (fault_addr);
  if (p == NULL)
    return false;
  
  return page_load_helper (p);
}

/* Find the page containning virtual address ADDRESS if exist, and allocates 
   stack pages if needed */
struct page *
find_page (const void *address)
{
  /* Check if address is virtual address */
  if (address >= PHYS_BASE)
    return NULL;

  struct page p;
  struct hash_elem *e;

  struct thread *cur = thread_current ();

  p.vaddr = (void *) pg_round_down (address);
  e = hash_find (cur->sup_page_table, &p.hash_elem);

  /* If the page exist, return the page. */
  if (e != NULL)
    return hash_entry (e, struct page, hash_elem);

  /* Check if need allocate stack page. */
  if ((p.vaddr > PHYS_BASE - STACK_MAX) && ((void *)cur->user_esp - 32 
                                            <= address))
    return page_allocation (p.vaddr, false);

  return NULL;
}

/* Destroys a page, which must be in the current process's
   page table.  Used as a callback for hash_destroy(). */
static void
page_exit_action (struct hash_elem *page_element, void *aux UNUSED)
{
  struct page *p = hash_entry (page_element, struct page, hash_elem);
  
  /* Acquire the lock on the frame that contains the page. */
  frame_acquire_lock (p);

  /* If the page has a frame, reset the frame. */
  if (p->frame)
    frame_reset (p->frame);
    
  free (p);
}

void
page_exit (void)
{
  struct hash *h = thread_current ()->sup_page_table;
  if (h != NULL)
    hash_destroy (h, page_exit_action);
}

