#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <stdbool.h>
#include "filesys/off_t.h"
#include "threads/synch.h"
#include "devices/block.h"

/* Virtual page. */
struct page 
  {
    /* Immutable members. */
    void *vaddr;                /* User virtual address mapped to this page */
    struct thread *thread;      /* Thread owning the page */
    struct frame *frame;        /* Frame that the page is mapped to */
    bool read_only;             /* If page is ready only */
    struct hash_elem hash_elem; /* hash table element */

    /* Memory-mapped file information, protected by 
       frame->frame_acquire_lock. */
    bool private;                /* False to write back to file,
                                   true to write back to swap. */
                                   
    struct file *file;           /* File */
    off_t file_offset;           /* File offset */
    off_t file_bytes;            /* Info about file, read/write */

    /* Swap information, protected by frame->frame_acquire_lock. */
    block_sector_t sector;       /* Starting sector of swap area, or -1. */
  };

  /* Allocate a paage for given user space */
  struct page *page_allocation (void *, bool);
  bool page_load (void *);

  /* hash table helper */
  hash_hash_func page_get_hash;
  /* hash table helper */
  hash_less_func page_less;
  /* free all pages in the page hash table */
  void page_exit (void);
  /* Returns true if the page is accessed, false otherwise */ 
  bool page_check_accessed (struct page *);
  /* Evict the page */
  bool page_evict (struct page *);
  /* Zero the page. Helper function for loading page */
  void zeroing_page (struct page *);
  /* Helper function for lead to load from file. */
  void load_from_file (struct page *);
  /* Helper function for helper function */
  bool page_load_helper (struct page *);
  /* Clear the given page */
  void page_clear (void *);
  /* Find the page containning virtual address ADDRESS or grow stack */
  struct page *find_page (const void *, bool);
  
#endif /* vm/page.h */