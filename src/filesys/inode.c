#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/buffer_cache.h"
//
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Defines number of direct blocks, singly indirect blocks, etc. */
#define DIRECT_BLOCKS 124
#define INDIRECT_BLOCKS 128

/* Assignment 16 : indicates directness. */
enum direct_t
{
  DIRECT,
  INDIRECT,
  DOUBLE_INDIRECT,
  OUT_LIMIT
};

/* Assignment 16 : store direction and index of position. */
struct sector_location
{
  enum direct_t directness;
  off_t index1;
  off_t index2;
};


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
//  block_sector_t start;               /* First data sector. */
//  uint32_t unused[125];               /* Not used. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    
    /* Assignment 16 : Extensible File */
    block_sector_t direct_map_table[DIRECT_BLOCKS];  /* directly mapped blocks. */
    block_sector_t indirect_block;                   /* singly indirectly mapped block. */
    block_sector_t double_indirect_block;            /* doubly indirectly mapped block. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//  struct inode_disk data;             /* Inode content. */

    /* Assignment 16 : Extensible File */
    struct lock extend_lock;            /* semaphore lock. */
  };

/* Assignment 16 : convert offset into byte address */
static inline off_t map_table_offset( int index )
{
  return index * 4;
}

/* Assignment 16 : get inode_disk from inode. */
static bool get_disk_inode( const struct inode* inode,
                            struct inode_disk* inode_disk )
{
  /* read from disk, by buffer cache. */
  bc_read( inode->sector, inode_disk, 0, BLOCK_SECTOR_SIZE, 0 );
  return inode_disk->magic == INODE_MAGIC;
}


/* Assignment 16 : get directness, first and second index. */
static void locate_byte( off_t pos, struct sector_location* sec_pos )
{
  off_t pos_sector = pos / BLOCK_SECTOR_SIZE;

  /* direct mapped case. */
  if( pos_sector < DIRECT_BLOCKS )
  {
    sec_pos->directness = DIRECT;
    sec_pos->index1 = pos_sector;
  }

  /* singly indirect mapped case. */
  else if( pos_sector < (off_t)(DIRECT_BLOCKS + INDIRECT_BLOCKS) )
  {
    sec_pos->directness = INDIRECT;
    sec_pos->index1 = pos_sector - DIRECT_BLOCKS;
  }

  /* doubly indirect mapped case. */
  else if( pos_sector < (off_t)(DIRECT_BLOCKS +
                        INDIRECT_BLOCKS * (INDIRECT_BLOCKS + 1)) )
  {
    sec_pos->directness = DOUBLE_INDIRECT;
    sec_pos->index1 = (pos_sector - DIRECT_BLOCKS - INDIRECT_BLOCKS)
                      / INDIRECT_BLOCKS;
    sec_pos->index2 = (pos_sector - DIRECT_BLOCKS - INDIRECT_BLOCKS)
                      % INDIRECT_BLOCKS;
  }

  /* wrong offset. */
  else
  {
    sec_pos->directness = OUT_LIMIT;
  }
}


/* Assignment 16 : register new block to inode_disk. */
static bool register_sector( struct inode_disk* inode_disk,
                             block_sector_t new_sector,
                             struct sector_location sec_loc )
{
  block_sector_t* block;
  off_t index;

  switch( sec_loc.directness )
  {
    /* direct mapping : map on direct_map_table. */
    case DIRECT:
      inode_disk->direct_map_table[sec_loc.index1] = new_sector;
      break;

    /* singly indirect mapping : read block, write sector, write block. */
    case INDIRECT:
      /* allocate memory. */
      block = (block_sector_t*)malloc(BLOCK_SECTOR_SIZE);
      if( block == NULL )
        return false;

      /* get index of first block. if none, allocate new block. */
      index = inode_disk->indirect_block;
      if( index == -1 )
      {
        if( free_map_allocate( 1, (block_sector_t *)&index ) == false )
        {
          free( block );
          return false;
        }
        /* put new block index to inode_disk. */
        inode_disk->indirect_block = index;

        /* fill into -1. */
        memset( block, 0xFF, BLOCK_SECTOR_SIZE );
      }
      else
      {
        /* read first block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      }

      /* write new sector, and write to buffer cache. */
      block[sec_loc.index1] = new_sector;
      bc_write( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      
      free( block );
      break;

    case DOUBLE_INDIRECT:
      /* allocate memory. */
      block = (block_sector_t*)malloc(BLOCK_SECTOR_SIZE);
      if( block == NULL )
        return false;

      /* get index of first block. if none, allocate new block. */
      index = inode_disk->double_indirect_block;
      if( index == -1 )
      {
        if( free_map_allocate( 1, (block_sector_t *)&index ) == false )
        {
          free( block );
          return false;
        }
        /* put new block index to inode_disk. */
        inode_disk->double_indirect_block = index;

        /* fill into -1. */
        memset( block, 0xFF, BLOCK_SECTOR_SIZE );

        /* write to buffer cache. */
        bc_write( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      }
      else
      {
        /* read first block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      }

      /* get index of second block. if none, allocate new block. */
      index = block[sec_loc.index1];
      if( index == -1 )
      {
        if( free_map_allocate( 1, (block_sector_t *)&index ) == false )
        {
          free( block );
          return false;
        }
        /* put new block index to inode_disk. */
        block[sec_loc.index1] = index;

        /* fill into -1. */
        memset( block, 0xFF, BLOCK_SECTOR_SIZE );
      }
      else
      {
        /* read second block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      }

      /* write new sector, and write to buffer cache. */
      block[sec_loc.index2] = new_sector;
      bc_write( index, block, 0, BLOCK_SECTOR_SIZE, 0 );
      
      free( block );
      break;

    default:
      return false;
  }

  return true;
}


/* Assignment 16 : get disk block number by file offset. */
static block_sector_t byte_to_sector( const struct inode_disk* inode_disk,
                                      off_t pos )
{
  block_sector_t result_sec = -1;
  off_t index;

  if( pos < inode_disk->length )
  {
    block_sector_t* block; /* buffer for block sector */
    struct sector_location sec_loc;

    /* get location from inode. */
    locate_byte( pos, &sec_loc );

    switch( sec_loc.directness )
    {
      case DIRECT:
        result_sec = inode_disk->direct_map_table[sec_loc.index1];
        break;

      case INDIRECT:
        /* allocate memory. */
        block = (block_sector_t*)malloc(BLOCK_SECTOR_SIZE);
        if( block == NULL )
          return false;

        /* get index of first block. if none, return -1. */
        index = inode_disk->indirect_block;
        if( index == -1 )
        {
            free( block );
            return false;
        }

        /* read first block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );

        /* get index from block. */
        result_sec = block[sec_loc.index1];
      
        free( block );
        break;

      case DOUBLE_INDIRECT:
        /* allocate memory. */
        block = (block_sector_t*)malloc(BLOCK_SECTOR_SIZE);
        if( block == NULL )
          return false;

        /* get index of first block. if none, return -1. */
        index = inode_disk->double_indirect_block;
        if( index == -1 )
        {
            free( block );
            return false;
        }

        /* read first block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );

        /* get index from block. */
        index = block[sec_loc.index1];
        if( index == -1 )
        {
            free( block );
            return false;
        }

        /* read second block. */
        bc_read( index, block, 0, BLOCK_SECTOR_SIZE, 0 );

        /* get index from block. */
        result_sec = block[sec_loc.index2];
      
        free( block );
        break;

      default:
        return -1;
    }
  }

  return result_sec;
}


/* Assignment 16 : update file length */
static bool inode_update_file_length( struct inode_disk* inode_disk,
                               off_t start_pos, off_t end_pos )
{
  off_t size, offset, chunk_size;
  block_sector_t sector_idx;
  void* block;

  /* initialize size, offset. */
  offset = start_pos;
  size = end_pos - start_pos;

  /* get block of 0's. */
  block = malloc( BLOCK_SECTOR_SIZE );
  if( block == NULL )
    return false;
  memset( block, 0, BLOCK_SECTOR_SIZE );

  /* loop until chunk_size is left. */
  while( size > 0 )
  {
    /* calculate offset. */
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* if sector offset is not 0, sector is already allocated. */
    if( sector_ofs != 0 )
    {
      chunk_size = BLOCK_SECTOR_SIZE - sector_ofs;
    }

    /* if sector offset is zero, should allocate new sector. */
    else
    {
      struct sector_location sec_loc;

      /* get location of offset. */
      locate_byte( offset, &sec_loc );

      /* allocate new block. */
      if( free_map_allocate( 1, &sector_idx ) == false )
      {
        free( block );
        return false;
      }

      /* put value to inode_disk. */
      if( register_sector( inode_disk, sector_idx, sec_loc ) == true )
      {
        bc_write( sector_idx, block, 0, BLOCK_SECTOR_SIZE, 0 );
      }

      /* get chunk size. */
      chunk_size = BLOCK_SECTOR_SIZE;
    }

    /* advance. */
    size -= chunk_size;
    offset += chunk_size;
  }

  free( block );
  return true;
}


/* Assignment 16 : free all blocks */
static void free_inode_sectors( struct inode_disk* inode_disk )
{
  int i, j;
  block_sector_t *block, *block2;

  block = (block_sector_t*)malloc((unsigned)BLOCK_SECTOR_SIZE);
  block2 = (block_sector_t*)malloc((unsigned)BLOCK_SECTOR_SIZE);

  /* deallocate doubly indirect mapped blocks. */
  if( (int)inode_disk->double_indirect_block != -1 )
  {
    /* read first block. */
    bc_read( inode_disk->double_indirect_block, block, 0, BLOCK_SECTOR_SIZE, 0 );

    /* loop until block sector is -1. */
    for( i=0; (int)block[i] != -1; i++ )
    {
      /* read second block. */
      bc_read( block[i], block2, 0, BLOCK_SECTOR_SIZE, 0 );

      /* loop until block sector is -1. */
      for( j=0; (int)block2[j] != -1; j++ )
      {
        /* free sector. */
        free_map_release( block2[j], 1 );
      }

      /* free this sector. */
      free_map_release( block[i], 1 );
    }

    /* free this sector. */
    free_map_release( inode_disk->double_indirect_block, 1 );
    inode_disk->double_indirect_block = -1;
  }

  /* deallocate singly indirect mapped blocks. */
  if( (int)inode_disk->indirect_block != -1 )
  {
    /* read first block. */
    bc_read( inode_disk->indirect_block, block, 0, BLOCK_SECTOR_SIZE, 0 );

    /* loop until block sector is -1. */
    for( i=0; (int)block[i] != -1; i++ )
    {
      /* free this sector. */
      free_map_release( block[i], 1 );
    }

    /* free this sector. */
    free_map_release( inode_disk->indirect_block, 1 );
    inode_disk->indirect_block = -1;
  }

  /* deallocate directly mapped blocks. */
  for( i=0; i<DIRECT_BLOCKS; i++ )
  {
    if( (int)inode_disk->direct_map_table[i] != -1 )
      free_map_release( inode_disk->direct_map_table[i], 1 );
  }

  free( block );
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

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
      memset( disk_inode, 0xFF, sizeof *disk_inode );
      //size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;

      /* Assignment 16 : rewrite inode create */
      if( length > 0 )
      {
        inode_update_file_length( disk_inode, 0, length );
      }

      /* write inode to buffer cache. */
      success = bc_write( sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0 );
      free( disk_inode );
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
//block_read (fs_device, inode->sector, &inode->data);
  lock_init( &inode->extend_lock );
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
          /* Assignment 16 : close rewrited inode */
          struct inode_disk* disk_inode;
          disk_inode = (struct inode_disk*)malloc(BLOCK_SECTOR_SIZE);

          get_disk_inode( inode, disk_inode );
          free_inode_sectors( disk_inode );
          free_map_release( inode->sector, 1 );

          free( disk_inode );
        }

      free (inode); 
    }
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
  struct inode_disk* disk_inode;

  disk_inode = (struct inode_disk *)malloc(BLOCK_SECTOR_SIZE);
  if( disk_inode == NULL )
    return -1;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
//      block_sector_t sector_idx = byte_to_sector (inode, offset);

      /* Assignment 16 : get inode_disk from inode. */
      if( get_disk_inode( inode, disk_inode ) == false )
        return -1;

      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_read( sector_idx, buffer, bytes_read, chunk_size, sector_ofs );
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  free( disk_inode );
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
  struct inode_disk* disk_inode;

  disk_inode = (struct inode_disk *)malloc(BLOCK_SECTOR_SIZE);
  if( disk_inode == NULL )
    return -1;

  /* Assignment 16 : get inode from buffer cache */
  if( get_disk_inode( inode, disk_inode ) == false )
    return -1;

  if (inode->deny_write_cnt)
    return 0;

  /* Assignment 16 : extensive file */
  lock_acquire( &inode->extend_lock );

  int old_length = disk_inode->length;
  int write_end = offset + size - 1;

  if( write_end > old_length-1 )
  {
    inode_update_file_length( disk_inode, old_length, write_end );

    /* update length of inode, write to buffer cache. */
    disk_inode->length = write_end + 1;
    bc_write( inode->sector, disk_inode, 0, BLOCK_SECTOR_SIZE, 0 );
  }

  lock_release( &inode->extend_lock );

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
//      block_sector_t sector_idx = byte_to_sector (inode, offset);

      /* Assignment 16 : get inode_disk from inode. */
      block_sector_t sector_idx = byte_to_sector (disk_inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      bc_write( sector_idx, (void *)buffer, bytes_written, chunk_size, sector_ofs );

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  free( disk_inode );
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
inode_length (const struct inode *inode)
{
  struct inode_disk* disk_inode = malloc((unsigned)BLOCK_SECTOR_SIZE);
  get_disk_inode( inode, disk_inode );
  off_t ret = disk_inode->length;
  free( disk_inode );
  return ret;
}
