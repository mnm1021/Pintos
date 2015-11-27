#include "vm/page.h"
#include <string.h>
#include <debug.h>
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/swap.h"

/* Assignment 13 : lru list for pages */
struct list lru_list;
struct lock lru_lock;
struct list_elem* lru_clock;

static struct page* get_victim_page( void );
static void* try_to_get_page( enum palloc_flags flag );
static void __free_page( struct page* page );

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
  struct vm_entry* vme;
  struct page* page;
  struct list_elem* elem;

  /* get current vm_entry */
  vme = hash_entry( e, struct vm_entry, elem );

  lock_acquire( &lru_lock );

  /* search through lru list */
  for( elem = list_begin( &lru_list );
       elem != list_end( &lru_list );
       /* empty */ )
  {
    /* get page from list */
    page = list_entry( elem, struct page, lru_elem );
    elem = list_next( elem );

    /* if page holds this vme, delete page from list. */
    if( page->vme == vme )
    {
      __free_page( page );
    }
  }

  lock_release( &lru_lock );

  free( vme );
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

/*
 * Assignment 12 : do_munmap
 */
void do_munmap( struct mmap_file *mmap_file )
{
  struct list_elem *ex;
  struct vm_entry *vme;
  struct list_elem *elem;
  struct page *page;

  /* remove all vmes */
  for( ex = list_begin( &mmap_file->vme_list );
       ex != list_end( &mmap_file->vme_list );
       /* empty */ )
  {
    vme = list_entry( ex, struct vm_entry, mmap_elem );

    /* if page is dirty, write on file. */
    if( pagedir_is_dirty( thread_current()->pagedir, vme->vaddr ) )
    {
      file_write_at( vme->file, vme->vaddr, vme->read_bytes, vme->offset );
    }

    /* remove from list */  
    ex = list_remove( ex );

    lock_acquire( &lru_lock );

    /* search through lru list, find for page. */
    for( elem = list_begin( &lru_list );
         elem != list_end( &lru_list );
         elem = list_next( elem ) )
    {
      /* get page from list */
      page = list_entry( elem, struct page, lru_elem );

      /* if page holds this vme, free page and break. */
      if( page->vme == vme )
      {
        __free_page( page );
        break;
      }
    }

    lock_release( &lru_lock );

    delete_vme( &thread_current()->vm, vme );
    free( vme );
  }
}


/*
 * Assignment 13 : initialize lru
 */
void lru_init()
{
  list_init( &lru_list );
  lock_init( &lru_lock );
  lru_clock = NULL;
}

/*
 * Assignment 13 : adding page to list
 */
void add_page_to_list( struct page* page )
{
  lock_acquire( &lru_lock );

  /* add this page's lru to list. */
  list_push_back( &lru_list, &page->lru_elem );

  lock_release( &lru_lock );
}

/*
 * Assignment 13 : deleting page from list
 * required :
 *   if clock pointing to argument, move to next.
 */
void delete_page_from_list( struct page* page )
{
  /* check if clock pointing to this page. */
  if( lru_clock == &page->lru_elem )
  {
    lru_clock = list_remove( lru_clock );

    /* if clock at end of list, set to NULL. */
    if( lru_clock == list_end( &lru_list ) )
      lru_clock = NULL;
  }
  else
    list_remove( &page->lru_elem );
}

/*
 * Assignment 13 : allocating page
 * WARNING - member 'vme' and 'lru_elem' should be handled
 *           out of this page.
 */
struct page* alloc_page( enum palloc_flags flag )
{
  void* kaddr;
  struct page* page;

  /* allocate kernel memory. */
  /* if no more, try to free a page. */
  kaddr = palloc_get_page( flag );
  while( kaddr == NULL )
    kaddr = try_to_get_page( flag );

  /* initialize page */
  page = (struct page*) malloc (sizeof(struct page));
  memset( page, 0, sizeof(struct page) );

  page->kaddr = kaddr;
  page->thread = thread_current();

  return page;
}

/*
 * Assignment 13 : try to free victim, and get page.
 */
static void* try_to_get_page( enum palloc_flags flag )
{
  struct page* victim;
  bool is_dirty;

  lock_acquire( &lru_lock );

  /* get victim */
  victim = get_victim_page();

  /* get if page is dirty */
  is_dirty = pagedir_is_dirty( victim->thread->pagedir, victim->vme->vaddr );

  /* switch by vme type */
  switch( victim->vme->type )
  {
    case VM_BIN:
      /* if dirty, write to file. */
      if( is_dirty == true )
      {
        file_write_at( victim->vme->file, victim->vme->vaddr,
                       victim->vme->read_bytes, victim->vme->offset );
      }

      /* switch type to VM_ANON. */
      victim->vme->type = VM_ANON;

      /* set swap slot. */
      victim->vme->swap_slot = swap_out( victim->kaddr );

      break;

    case VM_FILE:
      /* if dirty, write to file. */
      if( is_dirty == true )
      {
        file_write_at( victim->vme->file, victim->vme->vaddr,
                       victim->vme->read_bytes, victim->vme->offset );
      }
      break;

    case VM_ANON:
      /* always set swap slot. */
      victim->vme->swap_slot = swap_out( victim->kaddr );
      break;
  }

  /* unloaded from now on. */
  victim->vme->is_loaded = false;

  __free_page( victim );

  lock_release( &lru_lock );

  return palloc_get_page( flag );
}

/*
 * Assignment 13 : getting victim page from list
 */
static struct page* get_victim_page()
{
  struct list_elem* e;
  struct page* page;

  /* if clock not NULL, set elem to list_begin. */
  e = ( lru_clock != NULL ) ? lru_clock : list_begin( &lru_list );

  /* get page descriptor */
  page = list_entry( e, struct page, lru_elem );

  while( page->vme->is_pinned == false &&
         pagedir_is_accessed( page->thread->pagedir, page->vme->vaddr ) )
  {
    /* if page is accessed, set to 'unaccessed'. */
    pagedir_set_accessed( page->thread->pagedir, page->vme->vaddr, false );

    /* switch to next element. */
    e = list_next( e );

    /* if e at the end of list, set to list begin. */
    if( e == list_end( &lru_list ) )
      e = list_begin( &lru_list );

    /* get page descriptor */
    page = list_entry( e, struct page, lru_elem );
  }

  lru_clock = e;

  return page;
}

/*
 * Assignment 13 : free page for outside of this source
 */
void free_page( struct page* page )
{
  lock_acquire( &lru_lock );
  __free_page( page );
  lock_release( &lru_lock );
}

/*
 * Assignment 13 : free page descriptor, delete page entry
 */
static void __free_page( struct page* page )
{
  /* delete from list */
  delete_page_from_list( page );

  /* deallocate page from kernel, delete entry of page directory. */
  pagedir_clear_page( page->thread->pagedir, page->vme->vaddr );
  palloc_free_page( page->kaddr );

  /* deallocate page descriptor. */
  free( page );
}




