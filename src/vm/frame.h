#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "threads/synch.h"

/* A physical frame. */
struct frame 
  {
    struct lock frame_inuse;            /* Lock if frame is in use. */
    void *kernel_virtual_address;       /* Kernel virtual base address. */
    struct page *page;                  /* Page maapped to this frame. */
  };

void frame_table_init (void);           /* Initialaize frame table. */
struct frame *frame_allocation (struct page *page); 
                                        /* Map page to one frame. */
void frame_acquire_lock(struct page *p);        /* Lock frame. */
void frame_release_lock(struct page *p);      /* Unlock frame. */
void frame_reset (struct frame *);       /* Free frame. */

#endif /* vm/frame.h */
