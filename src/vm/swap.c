#include "vm/swap.h"
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "vm/frame.h"
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"
#include "userprog/syscall.h"

/* lock for swap to prevent race. */
static struct lock swap_lock;

/* Swap block. */
static struct block *swap_block;

/* Bitmap of free swap slots. */
static struct bitmap *swap_table;

void
swap_init (void)
{
  /* Initialize swap block. */
  swap_block = block_get_role (BLOCK_SWAP);
  if (!swap_block)
    /* No swap block. */
    swap_table = bitmap_create (0);
  else
    /* Create swap bitmap. */
    swap_table = bitmap_create (block_size (swap_block)
                                 / SECTORS_PER_PAGE);
  if (swap_table == NULL)
    handle_exit (-1);
  lock_init (&swap_lock);
}

/* Swaps in the page from disk to frame. */
void
swap_in (struct page *p)
{
  if (!swap_block || !swap_table)
    return;
  size_t i;
  lock_acquire(&swap_lock);
  for (i = 0; i < SECTORS_PER_PAGE; i++)
    {
      /* Read in page sectors. */
      block_read (swap_block, p->sector + i,
                  p->frame->kernel_virtual_address + i * BLOCK_SECTOR_SIZE);
    }
  /* Free a swap slot when its contents are read back into a frame. */
  bitmap_reset (swap_table, p->sector / SECTORS_PER_PAGE);
  lock_release(&swap_lock);
  p->sector = (block_sector_t) -1;
}

/* Swap out the page from frame to disk */
bool
swap_out (struct page *p)
{
  if (!swap_block || !swap_table)
    return false;
  size_t i;

  /* Find a free swap slot. Use swap_lock to prevent race. */
  lock_acquire (&swap_lock);
  size_t open_slot = bitmap_scan_and_flip (swap_table, 0, 1, false);
  lock_release (&swap_lock);

  if (open_slot == BITMAP_ERROR)
    /* No free swap slots. */
    return false;

  p->sector = open_slot * SECTORS_PER_PAGE;

  for (i = 0; i < SECTORS_PER_PAGE; i++)
  {
    /* Write page sectors to swap block. */
    block_write (swap_block, p->sector + i,
                 (uint8_t *) p->frame->kernel_virtual_address + i * BLOCK_SECTOR_SIZE);
  }
  /* Reset page. */
  p->file_offset = 0;
  p->file_bytes = 0;
  p->file = NULL;
  p->private = false;
  return true;
}