#ifndef VM_SWAP_H
#define VM_SWAP_H 1

#include <stdbool.h>

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct page;
void swap_init (void);
void swap_in (struct page *);
bool swap_out (struct page *);

#endif /* vm/swap.h */
