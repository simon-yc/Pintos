#include "vm/frame.h"
#include <stdio.h>
#include "vm/page.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "devices/timer.h"
#include "vm/swap.h"
#include "threads/thread.h"

static struct frame *frames;
static struct lock frame_enter_lock;
static size_t frame_count;
static size_t evict_loop;

/* Initialize the frame table. */
void
frame_table_init (void) 
{
  /* Malloc space for frame table to keep track of all frames. */
  frames = malloc (sizeof *frames * init_ram_pages); 

  /* check if enough space */
  if (frames == NULL)
    thread_exit ();
  
  frame_count = 0;
  /* Use a list to store all frames, keep allocating space for each frame
      until there is no space in physical address */
  void *kernel_virtual_address;
  while ((kernel_virtual_address = palloc_get_page (PAL_USER)) != NULL) 
    {
      struct frame *f = &frames[frame_count];
      lock_init (&f->frame_inuse);
      f->kernel_virtual_address = kernel_virtual_address;
      f->page = NULL;
      frame_count++;
    }

  /* Synchronize access to the frame table. */
  lock_init (&frame_enter_lock);

  return;
}

/* Try to allocate a frame for page.
   return frame is successful, otherwise null. */
struct frame *
frame_allocation (struct page *page) 
{
  /* Acquire the frame_enter_lock to ensure exclusive access to the frame table. */
  lock_acquire (&frame_enter_lock);

  /* Find a free frame. */
  for (size_t i = 0; i < frame_count; i++)
    {
      struct frame *f = &frames[i];

      /* Try to acquire the frame_inuse lock for the current frame. */
      bool acquired = lock_try_acquire (&f->frame_inuse);
      if (!acquired)
        /* Frame is currently in use, will try next frame. */
        continue;

      /* Check if the current frame is free. */
      if (f->page != NULL)
        {
          /* Release the frame_inuse lock for the current frame. */
          lock_release (&f->frame_inuse);
          continue;
        }

      /* Assign the given page to the frame. */
      f->page = page;

      /* Release the frame_enter_lock to allow other threads to allocate 
        frames. */
      lock_release (&frame_enter_lock);

      return f;
    }

  /* No free frame.  Find a frame to evict and try multiple times. */
  for (size_t i = 0; i < frame_count * 3; i++) 
    {
      /* Get a frame. */
      struct frame *f = &frames[evict_loop];
      evict_loop ++;
      if (evict_loop >= frame_count)
      /* Reset evict_loop to 0 to start from the beginning of the frame table, 
         as new frame may become avliable. */
        evict_loop = 0;
        
      if (!lock_try_acquire (&f->frame_inuse))
        /* Frame is in use. Go to next frame */
        continue;

      if (f->page == NULL) 
        {
          /* Frame is free. Will use this frame */
          f->page = page;
          lock_release (&frame_enter_lock);
          return f;
        } 

      if (page_check_accessed (f->page)) 
        {
          /* Frame is recently accessed. Go to next frame */
          lock_release (&f->frame_inuse);
          continue;
        }
          
      lock_release (&frame_enter_lock);
      
      /* Frame is not recently used. Evict this frame. */
      if (!page_evict (f->page))
        {
          /* Check if eviction failed. */
          lock_release (&f->frame_inuse);
          return NULL;
        }
      /* Eviction successful. Will use this frame */
      f->page = page;
      return f;
    }

  lock_release (&frame_enter_lock);
  
  return NULL;
}

/* Lock page p's frame to disallow changes and eviction */
void
frame_acquire_lock (struct page *p) 
{
  struct frame *f = p->frame;
  /* If the page has no frame, return. */
  if (f == NULL)
    return;

  /* If a frame exists, acquire the lock for that frame. */
  lock_acquire(&f->frame_inuse);

  /* Verify that the page's frame has not changed since it was last accessed.
     If the frame was removed asynchronously, release the lock and assert that
     the frame is no longer associated with the page. */
  if (f != p->frame)
    lock_release(&f->frame_inuse);
}

/* Release page p's frame lock to allow eviction. */
void
frame_release_lock (struct page *p) 
{
  struct frame *f = p->frame;
  if (f != NULL) 
    lock_release (&f->frame_inuse);
}