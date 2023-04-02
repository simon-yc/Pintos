#ifndef VM_SWAP_H
#define VM_SWAP_H 1

#include <stdbool.h>

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct page;

/* Initiallize swap table. */
void swap_init (void);
/* Swaps in the page from disk to memory. */
void swap_in (struct page *);
/* Swap out the page to disk */
bool swap_out (struct page *);

#endif /* vm/swap.h */
