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

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static block_sector_t get_direct_sector (const struct inode *, off_t);
static block_sector_t get_indirect_sector (const struct inode *, uint32_t, off_t);
static block_sector_t get_doubly_indirect_sector (const struct inode *, uint32_t, off_t);
static void allocate_ind_blocks (block_sector_t *, size_t *, size_t, size_t *);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Helper function to get the direct block sector */
static block_sector_t
get_direct_sector (const struct inode *inode, off_t position)
{
  return inode->data.blocks[position / BLOCK_SECTOR_SIZE];
}

/* Helper function to get the indirect block sector. 
   index indicate which indirect block, i.e, which block. For example, if is 
   the 2nd indirect block, index = numb of direct block + 2. 
   position indicate the position of the sector in this indirect block */
static block_sector_t
get_indirect_sector (const struct inode *inode, uint32_t index, off_t position)
{
  /* Create an array to store the indirect block pointers */
  block_sector_t indirect[MAX_DIRECT];
  block_read (fs_device, inode->data.blocks[index], &indirect);
  return indirect[position / BLOCK_SECTOR_SIZE];
}

/* Helper function to get the doubly indirect block sector 
   index indicate the corresponding position for thsi sector in the inode.
   position indicate the position of the sector in this db_indirect block*/
static block_sector_t
get_doubly_indirect_sector (const struct inode *inode, uint32_t index, 
                            off_t position)
{
  /* Create an array to store the doublely indirect block pointers */
  block_sector_t db_indirect[MAX_DIRECT];
  block_read (fs_device, inode->data.blocks[index], &db_indirect);
  /* Create an array to store the indirect block pointers */
  block_read (fs_device, 
             db_indirect[(position / (BLOCK_SECTOR_SIZE * MAX_DIRECT))],
             &db_indirect);
  position %= (BLOCK_SECTOR_SIZE * MAX_DIRECT);
  return db_indirect[position / BLOCK_SECTOR_SIZE];
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    {
      if (pos < BLOCK_SECTOR_SIZE * DIRECT_SIZE)
        /* Direct block */ 
        return get_direct_sector (inode, pos);
      else if (pos < BLOCK_SECTOR_SIZE * 
                     (DIRECT_SIZE + INDIRECT_SIZE * MAX_DIRECT))
        {
          /* Indirect block, update position first */
          pos -= BLOCK_SECTOR_SIZE * DIRECT_SIZE;
          return get_indirect_sector (inode, 
                                      (pos / (BLOCK_SECTOR_SIZE * MAX_DIRECT) + 
                                              DIRECT_SIZE), 
                                      pos % (BLOCK_SECTOR_SIZE * MAX_DIRECT));
        }
      else
        {
          /* DB Indirect block, update position first */
          pos -= BLOCK_SECTOR_SIZE * (DIRECT_SIZE + 
                                      INDIRECT_SIZE * MAX_DIRECT);
          return get_doubly_indirect_sector (inode,
                                             DIRECT_SIZE + INDIRECT_SIZE, 
                                             pos);              
        }
    }
  else
    return -1;
}

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      struct inode inode;
      initialize_inode_data (&inode);

      /* Allocate more sectors for the inode if needed */
      inode_extend (&inode, disk_inode->length);

      /* Copy the inode data to the disk inode after extention */
      copy_inode_data (&inode, disk_inode);

      /* Write the disk inode to the disk as inode has been extended*/
      block_write (fs_device, sector, disk_inode);
      success = true; 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  block_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          inode_free (inode);
        }

      free (inode); 
    }
}

void 
inode_free (struct inode *inode)
{
  return;
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  
  /* Check if offset is within inode's length */
  if (offset < inode_length (inode))
    {
      while (size > 0) 
        {
          /* Disk sector to read, starting byte offset within sector. */
          block_sector_t sector_idx = byte_to_sector (inode, offset);
          int sector_ofs = offset % BLOCK_SECTOR_SIZE;

          /* Bytes left in inode, bytes left in sector, lesser of the two. */
          off_t inode_left = inode_length (inode) - offset;
          int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
          int min_left = inode_left < sector_left ? inode_left : sector_left;

          /* Number of bytes to actually copy out of this sector. */
          int chunk_size = size < min_left ? size : min_left;
          if (chunk_size <= 0)
            break;

          if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
            /* Read full sector directly into caller's buffer. */
            block_read (fs_device, sector_idx, buffer + bytes_read);
          else 
            {
              /* Read sector into bounce buffer, then partially copy
                into caller's buffer. */
              if (bounce == NULL) 
                {
                  bounce = malloc (BLOCK_SECTOR_SIZE);
                  if (bounce == NULL)
                    break;
                }
              block_read (fs_device, sector_idx, bounce);
              memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
            }
          
          /* Advance. */
          size -= chunk_size;
          offset += chunk_size;
          bytes_read += chunk_size;
        }
      free (bounce);
    }
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  /* Check if offset is within inode's length */
  if (offset + size > inode_length (inode))
    {
      /* Extend the inode if currently not enough */

      /* Acquire inode's lock to prevent race and promote synchronization */
      lock_acquire (&((struct inode *)inode)->lock);
      inode_extend (inode, offset + size);
      lock_release (&((struct inode *) inode)->lock);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  return inode->data.length;
}

void 
initialize_inode_data (struct inode *inode) 
{
  inode->data.length = 0;
  inode->data.direct_idx = 0;
  inode->data.indirect_idx = 0;
  inode->data.db_indirect_idx = 0;
}

/* Copy the inode data blocks from src inode to dst inode_disk */
void 
copy_inode_data (struct inode *src, struct inode_disk *dst) 
{
  dst->direct_idx = src->data.direct_idx;
  dst->indirect_idx = src->data.indirect_idx;
  dst->db_indirect_idx = src->data.db_indirect_idx;
  memcpy (&dst->blocks, &src->data.blocks, 
         TOTAL_DIRECT * sizeof (block_sector_t));
}

/* Extends the given inode to the specified new length */
void 
inode_extend (struct inode *inode, off_t new_len)
{
  /* Declare an empty block to be used when writing new data blocks */
  static char empty_block[BLOCK_SECTOR_SIZE];

  /* Find the number of additional sectors needed for the new length */
  size_t remaining_sec = bytes_to_sectors (new_len) - 
                            bytes_to_sectors (inode->data.length);
  
  /* If no new sectors are needed, update the inode's length and return */
  if (remaining_sec == 0)
    {
      inode->data.length = new_len;
      return;
    }

  /* Extend the direct data blocks by allocating new sectors and writing empty 
     blocks to them. Also decrement the remaining sectors count each time */
  while (inode->data.direct_idx < DIRECT_SIZE)
    {
      free_map_allocate (1, &inode->data.blocks[inode->data.direct_idx]);
      block_write (fs_device, inode->data.blocks[inode->data.direct_idx], empty_block);
      inode->data.direct_idx++;
      remaining_sec--;
      /* If all needed sectors are extended, update the inode's length and return */
      if (remaining_sec == 0)
        {
          inode->data.length = new_len;
          return;
        }
    }

  /* Extend indirect data blocks */
  while (inode->data.direct_idx < (DIRECT_SIZE + INDIRECT_SIZE))
    {
      remaining_sec = inode_grow_indirect (inode, remaining_sec);
      /* If all needed sectors are extended, update the inode's length and return */
      if (remaining_sec == 0)
        {
          inode->data.length = new_len;
          return;
        }
    }

  /* Extend doublely indirect data blocks */
  while (inode->data.direct_idx < (DIRECT_SIZE + INDIRECT_SIZE + DB_INDIRECT_SIZE))
    {
      remaining_sec = inode_grow_db_indirect (inode, remaining_sec);
      /* If all needed sectors are extended, update the inode's length and return */
      if (remaining_sec == 0)
        {
          inode->data.length = new_len;
          return;
        }
    }
}

/* Helper function for growing indirect blocks */
static void
allocate_ind_blocks (block_sector_t *parent_block, size_t *index, size_t max_index, size_t *remaining_sec)
{
  /* Declare an empty block to be used when writing new data blocks */
  static char empty_block[BLOCK_SECTOR_SIZE];
  block_sector_t block_list[MAX_DIRECT];

  /* If the index is 0, allocate a new block. Otherwise, read the existing
     block from the file system device*/
  if (*index == 0)
    free_map_allocate(1, parent_block);
  else
    block_read(fs_device, *parent_block, &block_list);

  while (*index < max_index)
  {
    free_map_allocate(1, &block_list[*index]);
    block_write(fs_device, block_list[*index], empty_block);
    (*index)++;
    (*remaining_sec)--;

    if (*remaining_sec == 0)
      break;
  }

  /* Write the updated block back to the file system device */
  block_write(fs_device, *parent_block, &block_list);
}

/* Extend inode's indirect data blocks by allocating and initializing new 
   sectors. */
size_t 
inode_grow_indirect(struct inode *inode, size_t remaining_sec)
{
  allocate_ind_blocks(&inode->data.blocks[inode->data.direct_idx], &inode->data.indirect_idx, MAX_DIRECT, &remaining_sec);

  /* If all entries in the indirect block are used, reset the indirect index 
     and increment the direct index */
  if (inode->data.indirect_idx == MAX_DIRECT)
  {
    inode->data.indirect_idx = 0;
    inode->data.direct_idx++;
  }

  return remaining_sec;
}

size_t 
inode_grow_db_indirect(struct inode *inode, size_t remaining_sec)
{
  block_sector_t db_indirect[MAX_DIRECT];

  /* If both doubly indirect and indirect indices are 0, allocate a new doubly 
     indirect block. Otherwise, read the existing doubly indirect block from 
     the file system device. */
  if (inode->data.db_indirect_idx == 0 && inode->data.indirect_idx == 0)
    free_map_allocate(1, &inode->data.blocks[inode->data.direct_idx]);
  else
    block_read(fs_device, inode->data.blocks[inode->data.direct_idx], &db_indirect);

  /* While loop for each indirects */
  while (inode->data.indirect_idx < MAX_DIRECT)
  {
    allocate_ind_blocks(&db_indirect[inode->data.indirect_idx], &inode->data.db_indirect_idx, MAX_DIRECT, &remaining_sec);

    /* If all entries in the indirect block are used, reset the doubly 
        indirect index and increment the indirect index */
    if (inode->data.db_indirect_idx == MAX_DIRECT)
    {
      inode->data.db_indirect_idx = 0;
      inode->data.indirect_idx++;
    }
    /* No more remaining sectors need to be extended */
    if (remaining_sec == 0)
      break;
  }

  /* Write the updated doubly indirect block back to the file system device */
  block_write(fs_device, inode->data.blocks[inode->data.direct_idx], &db_indirect);
  return remaining_sec;
}
