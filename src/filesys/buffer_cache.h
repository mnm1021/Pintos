#ifndef BUFFER_CACHE_H
#define BUFFER_CACHE_H

#include "filesys/off_t.h"
#include "threads/synch.h"
#include "devices/block.h"

/* Assignment 15 : buffer cache header */
struct buffer_head
{
  bool is_dirty;                  /* dirty flag */
  bool is_used;                   /* flag indicates this is used */
  bool is_clocked;                /* clock bit */

  block_sector_t disk_sector;     /* disk sector address */
  void* data;                     /* cache block address */

  struct lock bc_lock;            /* lock for this sector */
};

void bc_init( void );
void bc_destroy( void );

struct buffer_head* bc_select_victim( void );

struct buffer_head* bc_lookup( block_sector_t sector );

void bc_flush_entry( struct buffer_head *p_flush_entry );
void bc_flush_all_entries( void );

bool bc_read( block_sector_t sector_idx, void* buffer, off_t bytes_read,
              int chunk_size, int sector_ofs );
bool bc_write( block_sector_t sector_idx, void* buffer, off_t bytes_written,
               int chunk_size, int sector_ofs );

#endif
