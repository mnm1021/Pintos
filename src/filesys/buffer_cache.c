#include "filesys/buffer_cache.h"
#include <debug.h>
#include <string.h>
#include "threads/palloc.h"

#define BUFFER_CACHE_ENTRY_NB 64

/* Assignment 15 : buffer cache heads, entries. */
struct buffer_head buffer_head[BUFFER_CACHE_ENTRY_NB];
char p_buffer_cache[BUFFER_CACHE_ENTRY_NB * BLOCK_SECTOR_SIZE];
struct block* block_filesys;

/* Assignment 15 : used in clock algorithm. */
int clock_hand;


/* Assignment 15 : initialize buffer cache. */
void bc_init()
{
  int i;

  /* initialize buffer_head. */
  for( i=0; i<BUFFER_CACHE_ENTRY_NB; i++ )
  {
    buffer_head[i].is_dirty = false;
    buffer_head[i].is_used = false;
    buffer_head[i].is_clocked = false;

    buffer_head[i].data = p_buffer_cache + BLOCK_SECTOR_SIZE*i;

    lock_init( &( buffer_head[i].bc_lock ) );
  }

  /* get block for filesys. */
  block_filesys = block_get_role( BLOCK_FILESYS );

  /* initialize clock hand. */
  clock_hand = 0;
}


/* Assignment 15 : destroy buffer cache */
void bc_destroy( void )
{
  /* flush entries. */
  bc_flush_all_entries();
}


/* Assignment 15 : select victim from buffer_head. */
struct buffer_head* bc_select_victim( void )
{
  int ret;
  /* rotate through list. if not used or not clocked, select cache. */
  for( ; ; clock_hand = (clock_hand + 1) % BUFFER_CACHE_ENTRY_NB )
  {
    /* if not used or not clocked, select this entry. */
    if( buffer_head[clock_hand].is_used == false || buffer_head[clock_hand].is_clocked == false )
    {
      ret = clock_hand;
      clock_hand = (clock_hand + 1) % BUFFER_CACHE_ENTRY_NB;

      /* lock this block and return. */
      lock_acquire( &buffer_head[ret].bc_lock );
      return &buffer_head[ret];
    }

    /* advance : set clock bit to false. */
    buffer_head[clock_hand].is_clocked = false;
  }
}


/* Assignment 15 : find entry that holds sector */
struct buffer_head* bc_lookup( block_sector_t sector )
{
  int i;

  /* search through buffer_head, select if 'used flag' and valid sector. */
  for( i=0; i<BUFFER_CACHE_ENTRY_NB; i++ )
  {
    if( buffer_head[i].is_used && buffer_head[i].disk_sector == sector )
    {
      lock_acquire( &buffer_head[i].bc_lock );
      return &buffer_head[i];
    }
  }

  return NULL;
}


/* Assignment 15 : flush entry */
void bc_flush_entry( struct buffer_head *p_flush_entry )
{
  /* write to disk. */
  block_write( block_filesys, p_flush_entry->disk_sector, p_flush_entry->data );

  /* set dirty flag. */
  p_flush_entry->is_dirty = false;
}


/* Assignment 15 : flush all entries if dirty. */
void bc_flush_all_entries( void )
{
  int i;

  /* search through buffer_head, if dirty, flush block. */
  for( i=0; i<BUFFER_CACHE_ENTRY_NB; i++ )
  {
    if( buffer_head[i].is_used && buffer_head[i].is_dirty )
      bc_flush_entry( &buffer_head[i] );
  }
}


/* Assignment 15 : read to buffer cache. */
bool bc_read( block_sector_t sector_idx, void* buffer, off_t bytes_read,
              int chunk_size, int sector_ofs )
{
  struct buffer_head* bc_entry;

  /* search block sector. */
  bc_entry = bc_lookup( sector_idx ); /* this entry has lock acquired if not NULL. */

  /* if NULL, select victim and set cache entry.. */
  if( bc_entry == NULL )
  {
    bc_entry = bc_select_victim(); /* this entry has lock acquired. */

    /* if dirty, flush block. */
    if( bc_entry->is_used && bc_entry->is_dirty )
      bc_flush_entry( bc_entry );

    /* read from block. */
    block_read( block_filesys, sector_idx, bc_entry->data );

    /* set entry flags. */
    bc_entry->is_dirty = false;
    bc_entry->is_used = true;
    bc_entry->disk_sector = sector_idx;
  }

  /* copy memory from cache to buffer. */
  memcpy( buffer + bytes_read, bc_entry->data + sector_ofs, chunk_size );

  /* set flag. */
  bc_entry->is_clocked = true;

  /* release lock acquired by selection. */
  lock_release( &bc_entry->bc_lock );

  return true;
}


/* Assignment 15 : write to buffer cache. */
bool bc_write( block_sector_t sector_idx, void* buffer, off_t bytes_written,
               int chunk_size, int sector_ofs )
{
  struct buffer_head* bc_entry;

  /* search block sector. */
  bc_entry = bc_lookup( sector_idx ); /* this entry has lock acquired if not NULL. */

  /* if NULL, select victim and set cache entry.. */
  if( bc_entry == NULL )
  {
    bc_entry = bc_select_victim(); /* this entry has lock acquired. */

    /* if dirty, flush block. */
    if( bc_entry->is_used && bc_entry->is_dirty )
      bc_flush_entry( bc_entry );

    /* read from block. */
    block_read( block_filesys, sector_idx, bc_entry->data );

    /* set entry flags. */
    bc_entry->is_used = true;
    bc_entry->disk_sector = sector_idx;
  }

  /* copy memory from cache to buffer. */
  memcpy( bc_entry->data + sector_ofs, buffer + bytes_written, chunk_size );

  /* set flags. */
  bc_entry->is_dirty = true;
  bc_entry->is_clocked = true;

  /* release lock acquired by selection. */
  lock_release( &bc_entry->bc_lock );

  return true;
}
