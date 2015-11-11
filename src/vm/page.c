#include <vm/page.h>
#include <string.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/file.h"

static unsigned vm_hash_func( const struct hash_elem *e, void *aux UNUSED );
static bool vm_less_func( const struct hash_elem *a,
                          const struct hash_elem *b,
                          void *aux UNUSED );
static void vm_destroy_func( struct hash_elem *e, void *aux UNUSED );

/*
 * Assignment 11 : initialize vm_table
 */
void vm_init( struct hash *vm )
{
  hash_init( vm, vm_hash_func, vm_less_func, NULL );
}

/*
 * Assignment 11 : hash function
 */
static unsigned vm_hash_func( const struct hash_elem *e, void *aux UNUSED )
{
  return hash_int( (int)( hash_entry( e, struct vm_entry, elem )->vaddr ) );
}

/*
 * Assignment 11 : compare function
 */
static bool vm_less_func( const struct hash_elem *a,
                          const struct hash_elem *b,
                          void *aux UNUSED )
{
  return ( hash_entry( a, struct vm_entry, elem )->vaddr ) < 
         ( hash_entry( b, struct vm_entry, elem )->vaddr );
}

/*
 * Assignment 11 : insert vme
 */
bool insert_vme( struct hash *vm, struct vm_entry *vme )
{
  /* if table has vme, returns the vme's address. else, return NULL. */
  /* so, if returned NULL, successfully added. */
  return hash_insert( vm, &vme->elem ) == NULL;
}

/*
 * Assignment 11 : delete vme
 */
bool delete_vme( struct hash *vm, struct vm_entry *vme )
{
  /* if table has vme, returns its address. else, return NULL. */
  /* so, if returned not NULL, successfully deleted. */
  return hash_delete( vm, &vme->elem ) != NULL;
}

/*
 * Assignment 11 : find vme
 */
struct vm_entry* find_vme( void *vaddr )
{
  struct vm_entry key;
  struct hash_elem *found;

  /* set key, find from table. */
  key.vaddr = pg_round_down( vaddr );
  ASSERT( pg_ofs( key.vaddr ) == 0 );
  found = hash_find( &thread_current()->vm, &key.elem );

  /* if NULL, return NULL. */
  if( found == NULL )
    return NULL;

  return hash_entry( found, struct vm_entry, elem );
}

/*
 * Assignment 11 : destroy vm table
 */
void vm_destroy( struct hash *vm )
{
  hash_destroy( vm, vm_destroy_func );
}

/*
 * Assignment 11 : function for free-ing vm_entry
 */
static void vm_destroy_func( struct hash_elem *e, void *aux UNUSED )
{
  free( hash_entry( e, struct vm_entry, elem ) );
}

/*
 * Assignment 11 : load file to page
 */
bool load_file( void *kpage, struct vm_entry *vme )
{
  /* load the page. */
  if( file_read_at( vme->file, kpage, vme->read_bytes, vme->offset ) 
      != (int)vme->read_bytes )
  {
    return false;
  }

  /* set zero bytes. */
  memset( kpage + vme->read_bytes, 0, vme->zero_bytes );

  return true;
}







