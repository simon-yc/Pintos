#include "vm/page.h"
#include <stdio.h>
#include <string.h>
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"

/* Maximum size for process stack. */
#define STACK_MAX (1024 * 1024)

/* Allocate a page for user and add the page to user pages hash tabale.
   Return the page if successful, otherwise null */
struct page *page_allocation (void *vaddr, bool read_only)
{
  struct thread *cur = thread_current ();
  struct page *p = malloc (sizeof *p);
  if (p == NULL)
    return NULL;
  /* Initialize the page. */
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
  /* If the page is already in the page table, free the page and return 
     null. */
  free (p);
  return NULL;
}

/* Return the hash value */
unsigned
page_get_hash (const struct hash_elem *e, void *aux UNUSED)
{
  const struct page *p = hash_entry (e, struct page, hash_elem);
  return ((uintptr_t) p->vaddr) >> PGBITS;
}

/* Hash comparison helper function */
bool
page_less (const struct hash_elem *a, const struct hash_elem *b,
           void *aux UNUSED)
{
  return hash_entry (a, struct page, hash_elem)->vaddr 
         < hash_entry (b, struct page, hash_elem)->vaddr;
}

/* Zero the page. Helper function for loading page. */
void
zeroing_page (struct page *p)
{
  memset (p->frame->kernel_virtual_address, 0, PGSIZE);
}

/* Load the page from file. Helper function for loading page. */
void 
load_from_file (struct page *p)
{
  off_t read_bytes = file_read_at (p->file, 
                                   p->frame->kernel_virtual_address,
                                   p->file_bytes, p->file_offset);
  off_t zero_bytes = PGSIZE - read_bytes;
  memset (p->frame->kernel_virtual_address + read_bytes, 0, zero_bytes);
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
      /* Copy data into the frame. */
      if (p->sector != (block_sector_t) -1)
        /* Load data from swap. */
        swap_in (p);
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
                              p->frame->kernel_virtual_address, 
                              !p->read_only);
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
  p = find_page (fault_addr, true);
  if (p == NULL)
    return false;
  
  return page_load_helper (p);
}

/* Destroys a page, which must be in the current process's
   page table.  Used as a callback for hash_destroy(). */
static void
page_exit_action (struct hash_elem *page_element, void *aux UNUSED)
{
  struct page *p = hash_entry (page_element, struct page, hash_elem);
  frame_acquire_lock (p);
  /* reset the frame if exist. */
  if (p->frame)
    {
      /* Also remove the page from the frame. */
      p->frame->page = NULL;
      frame_release_lock (p);
    }
  free (p);
}

void
page_exit (void)
{
  struct hash *h = thread_current ()->sup_page_table;
  if (h != NULL)
    hash_destroy (h, page_exit_action);
}

/* Returns true if the page is accessed, false otherwise. */
bool
page_check_accessed (struct page *p)
{
  bool accessed = pagedir_is_accessed (p->thread->pagedir, p->vaddr);
  if (accessed)
    pagedir_set_accessed (p->thread->pagedir, p->vaddr, false);
  return accessed;
}

bool
page_evict (struct page *p)
{
    /* Determine if a write is done to the page. */
  bool dirty = pagedir_is_dirty (p->thread->pagedir, (const void *) p->vaddr);

  /* Clear the page from the page table. Later accesses to the page will 
     fault*/
  pagedir_clear_page (p->thread->pagedir, (void *) p->vaddr);

  bool success = !dirty;
  
  if (p->file == NULL)
    /* If the page has no file associated with it, then it must be swapped 
       out. */
    success = swap_out (p);
  else if (dirty) 
    {
      /* If the page is dirty and has a file associated with it, then we 
          need to write it back to disk or file. */
      if (p->private)
        /* If the page is private, then we need to write it back to disk. */
        success = swap_out (p);
      else
        /* If the page is not private, page is file is a memory-mapped file 
           then write it back to file. */
        success = 
          file_write_at(p->file, 
                        (const void *) p->frame->kernel_virtual_address, 
                        p->file_bytes, p->file_offset);
    }

  if (success)
    /* free the frame */
    p->frame = NULL;
  return success;
}

/* Clear the page from the page table. */
void
page_clear (void *vaddr)
{
  struct page *p = find_page (vaddr, true);
  if (p)
    {
      /* Clear the page frame if exist. */
      if (p->frame)
        {
          frame_acquire_lock (p);
          p->frame->page = NULL;
          frame_release_lock (p);
        }
      /* Remove the page from the page table. */
      hash_delete (thread_current ()->sup_page_table, &p->hash_elem);
      free (p);
    }
}

/* Find the page containning virtual address ADDRESS if exist. If grow is set 
   to true, will allocates stack pages if requirements are met */
struct page *
find_page (const void *address, bool grow)
{
  /* Check if address is virtual address */
  if (address >= PHYS_BASE)
    return NULL;

  struct thread *cur = thread_current ();
  struct page p;

  p.vaddr = (void *) pg_round_down (address);
  struct hash_elem *elem = hash_find (cur->sup_page_table, &p.hash_elem);

  /* If the page exist, return the page. */
  if (elem != NULL)
    return hash_entry (elem, struct page, hash_elem);

  /* Check if need allocate stack page. */
  if (grow && (p.vaddr > PHYS_BASE - STACK_MAX) 
      && ((p.vaddr > (void *) cur->user_esp) 
      || ((void *) cur->user_esp - 32 == address) 
      || ((void *) cur->user_esp - 4 == address)))
    return page_allocation (p.vaddr, false);

  return NULL;
}