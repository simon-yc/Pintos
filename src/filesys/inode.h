#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_SIZE 8
#define INDIRECT_SIZE 4
#define DB_INDIRECT_SIZE 1

#define MAX_DIRECT 128
#define TOTAL_DIRECT (DIRECT_SIZE + INDIRECT_SIZE + DB_INDIRECT_SIZE)

/* Struct for indirect blocks */
struct indirect_block
  {
    block_sector_t blocks[MAX_DIRECT];
  };

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t direct;                    /* direct block. */
    uint32_t indirect;                  /* indirect block. */
    uint32_t db_indirect;               /* double indirect block. */
    uint32_t unused[110];               /* Not used. */
    uint32_t blocks[TOTAL_DIRECT];      /* Inode's blocks */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };
struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
void inode_extend (struct inode *, off_t);
void inode_free (struct inode *);

#endif /* filesys/inode.h */
