#include "vm/swap.h"
#include <kernel/bitmap.h>
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "devices/block.h"
#include "userprog/syscall.h"

/* bitmap represents for swap slot */
struct bitmap* slots;

/* lock for swap */
struct lock swap_lock;

/*
 * Assignment 13 : creates bitmap
 */
void swap_init()
{
  slots = bitmap_create( 1024*8 );
  bitmap_set_all( slots, false );
  lock_init( &swap_lock );
}

/*
 * Assignment 13 : page block to swap slot
 */
size_t swap_out( void *kaddr )
{
  struct block *swap_block;
  size_t swap_slot;
  int i;

  /* get block for swap */
  swap_block = block_get_role( BLOCK_SWAP );

  lock_acquire( &swap_lock );

  /* get empty(false) slot, set to 1 */
  swap_slot = bitmap_scan_and_flip( slots, 0, 1, false );

  /* multiply by 8 : block size is 512, page size is 4KB */
  swap_slot *= 8;

  /* store on swap area */
  for( i=0; i<8; i++ )
  {
    block_write( swap_block, swap_slot+i, kaddr+(BLOCK_SECTOR_SIZE*i) );
  }

  /* return to original */
  swap_slot /= 8;

  lock_release( &swap_lock );

  return swap_slot;
}

/*
 * Assignment 13 : bring swap slot back to memory
 */
void swap_in( size_t index, void* kaddr )
{
  struct block *swap_block;
  int i;

  /* get block for swap */
  swap_block = block_get_role( BLOCK_SWAP );

  lock_acquire( &swap_lock );

  /* set bitmap into false */
  bitmap_set_multiple( slots, index, 1, false );

  /* multiply by 8 */
  index *= 8;

  /* load on kaddr */
  for( i=0; i<8; i++ )
  {
    block_read( swap_block, index+i, kaddr+(BLOCK_SECTOR_SIZE*i) );
  }

  lock_release( &swap_lock );
}



