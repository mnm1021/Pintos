#include <kernel/hash.h>
#include <kernel/list.h>

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

/*
 * Assignment 11 : virtual memory
 */
struct vm_entry
{
  uint8_t type;                      /* VM_BIN, VM_FILE, VM_ANON */
  void *vaddr;                       /* virtual address */
  bool writable;                     /* flag for writability */
  bool is_loaded;                    /* flag that indicates loaded or not */
  struct file *file;                 /* file mapped to virtual address */
  size_t offset;                     /* offset for file */
  size_t read_bytes;                 /* readed bytes */
  size_t zero_bytes;                 /* zero bytes */

  struct hash_elem elem;             /* element for thread's vm table */
  struct list_elem mmap_elem;        /* element for mmap_file */

  /* later on */
  size_t swap_slot;                  /* */
};


/*
 * Assignment 11 : virtual memory
 */
void vm_init( struct hash *vm );
bool insert_vme( struct hash *vm, struct vm_entry *vme );
bool delete_vme( struct hash *vm, struct vm_entry *vme );
struct vm_entry* find_vme( void *vaddr );
void vm_destroy( struct hash *vm );
bool load_file( void *kaddr, struct vm_entry *vme );

/*
 * Assignment 12 : memory-mapped file
 */
struct mmap_file
{
  int map_id;                         /* identifier */
  struct file *file;                  /* mapped file */
  struct list_elem elem;              /* list elem for thread's mmap_list */
  struct list vme_list;               /* list of vme for file */
};

void do_munmap( struct mmap_file *mmap_file );