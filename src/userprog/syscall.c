#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "userprog/process.h"

struct lock filesys_lock; /* lock for file I/O */

static void syscall_handler (struct intr_frame *);
struct vm_entry* check_address (void *addr, void *esp UNUSED);
void get_argument (void *esp, int *arg, int count);

/*
 * Assignment 11 : check for validity
 */
void check_valid_buffer( void* buffer, unsigned size, void *esp, bool to_write );
void check_valid_string( const void* str, void *esp );

//syscalls
static void halt(void);
static void exit(int status);
static bool create(const char *file, unsigned initial_size);
static bool remove(const char *file);
static tid_t exec(const char *cmd_line);
static int wait(tid_t tid);
static int open(const char *file);
static int filesize(int fd);
static int read(int fd, void *buffer, unsigned size);
static int write(int fd, void *buffer, unsigned size);
static void seek(int fd, unsigned position);
static unsigned tell(int fd);
static void close(int fd);
static int mmap(int fd, void* addr);
static void munmap(int map_id);

/*
 * in case that the kernel needs to call exit.
 */
void
sys_exit (int status)
{
  exit (status);
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock); /* initialize filesys_lock */
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall_index;
  int arguments[3];

  //check esp, if in user area, get syscall index
  check_address(f->esp, f->esp);
  check_address((char*)f->esp + 3, f->esp);

  //get syscall index
  syscall_index = *((int*)(f->esp));

  switch( syscall_index )
  {
    case SYS_HALT:
      halt();
      break;

    case SYS_EXIT:
      // argument num 1: int
      get_argument(f->esp, arguments, 1);
      exit( arguments[0] );
      break;

    case SYS_CREATE:
      // argument num 2: const char*, int
      get_argument(f->esp, arguments, 2);

      // verify const char*
      // check_address((void *)arguments[0], f->esp);
      /* Assignment 11 : check valid string */
      check_valid_string( (const void*)arguments[0], f->esp );

      f->eax = create((const char*)arguments[0], (unsigned int)arguments[1]);
      break;

    case SYS_REMOVE:
      // argument num 1: const char*
      get_argument(f->esp, arguments, 1);
      
      // verify const char*
      // check_address( (void *)arguments[0] , f->esp );
      /* Assignment 11 : check valid string */
      check_valid_string( (const void*)arguments[0], f->esp );

      f->eax = remove( (const char*)arguments[0] );
      break;

    case SYS_EXEC:
      // argument num 1: const char*
      get_argument(f->esp, arguments, 1);
      
      // verify const char*
      // check_address( (void *)arguments[0] , f->esp );
      /* Assignment 11 : check valid string */
      check_valid_string( (const void*)arguments[0], f->esp );

      f->eax = exec( *((const char**)(f->esp) + 1) );
      break;

    case SYS_WAIT:
      // argument num 1 : int
      get_argument(f->esp, arguments, 1);
      f->eax = wait( arguments[0] );
      break;

    case SYS_OPEN:
      // argument num 1: const char*
      get_argument(f->esp, arguments, 1);
      
      // verify const char*
      // check_address( (void *)arguments[0] , f->esp );
      /* Assignment 11 : check valid string */
      check_valid_string( (const void*)arguments[0], f->esp );

      f->eax = open( (const char*)arguments[0] );
      break;

    case SYS_CLOSE:
      // argument num 1 : int
      get_argument(f->esp, arguments, 1);
      close( arguments[0] );
      break;

    case SYS_READ:
      // argument num 3 : int, const char*, int
      get_argument(f->esp, arguments, 3);

      // verify const char*
      // check_address((void *)arguments[1], f->esp);
      /* Assignment 11 : check buffer */
      check_valid_buffer( (void*)arguments[1], (unsigned)arguments[2], f->esp, true );

      f->eax = read( arguments[0], (void *)arguments[1], (unsigned)arguments[2] );
      break;

    case SYS_WRITE:
      // argument num 3 : int, const char*, int
      get_argument(f->esp, arguments, 3);

      // verify const char*
      // check_address((void *)arguments[1], f->esp);
      /* Assignment 11 : check string */
      check_valid_string( (const void*)arguments[1], f->esp );

      f->eax = write( arguments[0], (void *)arguments[1], (unsigned)arguments[2] );
      break;

    case SYS_SEEK:
      // argument num 2 : int, int
      get_argument(f->esp, arguments, 2);

      seek( arguments[0], arguments[1] );
      break;

    case SYS_TELL:
      // argument num 1 : int
      get_argument(f->esp, arguments, 1);
      f->eax = tell( arguments[0] );
      break;
    
    case SYS_FILESIZE:
      // argument num 1 : int
      get_argument(f->esp, arguments, 1);
      f->eax = filesize( arguments[0] );
      break;

    /* Assignment 12 : mmap, munmap */
    case SYS_MMAP:
      /* 2 args : int, void* */
      get_argument( f->esp, arguments, 2 );
      f->eax = mmap( arguments[0], (void*)arguments[1] );
      break;

    case SYS_MUNMAP:
      /* 1 arg : int */
      get_argument( f->esp, arguments, 1 );
      munmap( arguments[0] );
      break;

    default:
      thread_exit();
  }
}

/*
 * check if address is in 'user' area.
 */
struct vm_entry*
check_address (void *addr, void *esp UNUSED)
{
  if( (unsigned int)addr < 0x8048000 || !is_user_vaddr(addr) )
  {
    exit(-1);
  }

  return find_vme( addr );
}


/*
 * Get arguments from User Stack, store on arg.
 */
void
get_argument (void *esp, int *arg, int count)
{
  int iArg;

  for( iArg=1; iArg<=count; iArg++ )
  {
    check_address( (int*)esp + iArg, esp );
    arg[iArg-1] = *((int*)esp + iArg);
  }
}

/*
 * Assignment 11 : check valid buffer
 */
void
check_valid_buffer( void *buffer, unsigned size, void *esp, bool to_write )
{
  struct vm_entry *vme;
  unsigned iVm;
  unsigned prev_page=0, curr_page;

  /* rotate through all sizes, if page number different, vme again. */
  for( iVm=0; iVm<size; iVm++ )
  {
    curr_page = (unsigned) pg_round_down( (unsigned char*)buffer + iVm );

    /* if different, check vme. */
    if( prev_page != curr_page )
    {
      vme = check_address( (unsigned char*)buffer + iVm, esp );

      /* if vme doesn't exist, exit. */
      if( vme == NULL )
      {
        exit( -1 );
      }

      /* if trying to write on unwritable page, exit. */
      if( to_write == true && vme->writable == false )
      {
        exit( -1 );
      }
    }

    /* set prev page. */
    prev_page = curr_page;
  }
}

/*
 * Assignment 11 : check valid string
 */
void
check_valid_string( const void *str, void *esp )
{
  unsigned iStr;

  for( iStr=0; iStr<strlen(str); iStr++ )
    if( check_address( (char*)str + iStr, esp ) == NULL )
      exit(-1);
}


/*
 * System Call
 * halt : shutdown
 */
static void
halt ()
{
  shutdown_power_off();
}

/*
 * System Call
 * exit : exit this thread
 */
static void
exit (int status)
{
  struct thread *curr_thread;

  //get thread struct
  curr_thread = thread_current();

  //Process Termination Message
  printf("%s: exit(%d)\n", thread_name(), status);

  //save exit status
  curr_thread->exit_status = status;

  //exit
  thread_exit();
}

/*
 * System Call
 * create : create file of initial size
 */
static bool
create (const char *file, unsigned initial_size)
{
  bool success;

  if( file == NULL )
    exit(-1);
  
  success = filesys_create(file, initial_size);

  return success;
}


/*
 * System Call
 * remove : remove file
 */
static bool
remove (const char *file)
{
  bool success;

  if( file == NULL )
    exit(-1);

  success = filesys_remove(file);

  return success;
}

/*
 * System Call
 * exec : execute command line
 */
static tid_t
exec (const char *cmd_line)
{
  tid_t new_tid;
  struct thread *child;

  new_tid = process_execute( cmd_line );

  // if failed to execute process, return -1
  if( new_tid == TID_ERROR )
    return -1;
  
  // get child descriptor
  child = get_child_process( new_tid );
  if( child == NULL )
    return -1;

  // block this thread until the child loaded
  sema_down( &child->sema_load );

  // if load failed, return -1
  if( child->loaded == false )
    return -1;

  return new_tid;
}

/*
 * System Call
 * wait : return process_wait
 */
static int
wait (tid_t tid)
{
  return process_wait(tid);
}

/*
 * System Call
 * open : open given file, give file descriptor
 */
static int
open (const char *file)
{
  struct file *f;
  int fd;

  if( file == NULL )
    return -1;

  /* lock */
  lock_acquire( &filesys_lock );

  /* open file */
  f = filesys_open( file );

  /* return -1 if f == NULL */
  if( f == NULL )
  {
    lock_release( &filesys_lock );
    return -1;
  }

  /* add file to fd_table */
  fd = process_add_file( f );

  /* release */
  lock_release( &filesys_lock );

  return fd;
}


/*
 * System Call
 * filesize : return the size of given file
 */
static int
filesize (int fd)
{
  struct file *f;

  /* get the file descriptor */
  f = process_get_file( fd );

  /* if no file, return -1 */
  if( f == NULL )
    return -1;

  return file_length( f );
}

/*
 * System Call
 * read : read from the file, write on buffer
 * if 0, read from keyboard
 */
static int
read (int fd, void *buffer, unsigned size)
{
  struct file *f;
  char ch;
  int iCh=0;
  off_t length;

  /* acquire filesys_lock */
  lock_acquire( &filesys_lock );

  /* if fd == STDIN */
  if( fd == 0 )
  {
    /* while not -1, read from STDIN */
    while( ( ch = input_getc() ) != -1 )
    {
      /* store on buffer */
      ( (char*)buffer )[iCh++] = ch;
    }

    length = iCh;
  }
  else /* open file */
  {

    /* get file descriptor */
    f = process_get_file( fd );

    /* if f == NULL, ret -1 */
    if( f == NULL )
    {
      lock_release( &filesys_lock );
      return -1;
    }

    /* read from file to buffer */
    length = file_read( f, buffer, size );

  }

  /* release lock */
  lock_release( &filesys_lock );

  return length;
}

/*
 * System Call
 * write : write on file, read from the buffer
 * if 1, write on monitor
 */
static int
write (int fd, void *buffer, unsigned size)
{
  struct file *f;
  off_t length;

  /* acquire filesys_lock */
  lock_acquire( &filesys_lock );

  /* if fd == STDOUT */
  if( fd == 1 )
  {
    /* release lock */
    lock_release( &filesys_lock );

    /* put buffer on STDOUT */
    putbuf( buffer, size );

    length = size;
  }
  else /* open file */
  {

    /* get file descriptor */
    f = process_get_file( fd );

    /* if f == NULL, ret -1 */
    if( f == NULL )
    {
      lock_release( &filesys_lock );
      return -1;
    }
    
    /* write on file */
    length = file_write( f, buffer, size );

    /* release lock */
    lock_release( &filesys_lock );
  }

  return length;
}

/*
 * System Call
 * seek : set offset of the file
 */
static void
seek (int fd, unsigned position)
{
  struct file *f;

  /* get file descriptor */
  f = process_get_file( fd );

  /* if no file, quit */
  if( f == NULL )
  {
    lock_release( &filesys_lock );
    return;
  }
  
  /* call file_seek */
  file_seek( f, position );
}

/*
 * System Call
 * tell : tell the offset of file
 */
static unsigned
tell (int fd)
{
  struct file *f;

  /* get file descriptor */
  f = process_get_file( fd );

  /* if no file, return -1 */
  if( f == NULL )
    return -1;

  return file_tell( f );
}

/*
 * System Call
 * close : close the file
 */
static void
close (int fd)
{
  process_close_file( fd );
}


/*
 * System Call
 * Assignment 12 - mmap : memory-map file
 */
static int
mmap (int fd, void* addr)
{
  struct file *file;
  struct mmap_file *mmap_file;
  struct vm_entry *vme;
  int length, offset=0;

  /* check address */
  if( (unsigned)addr < 0x8048000 
      || (unsigned)addr >= 0xc0000000 
      || pg_ofs( addr ) != 0)
    return -1;

  /* get file descriptor */
  file = process_get_file( fd );
  if( file == NULL )
    return -1;

  /* create mmap_file */
  mmap_file = (struct mmap_file*) malloc (sizeof(struct mmap_file));
  if( mmap_file == NULL )
    return -1;

  /* initialize mmap_file */
  memset( mmap_file, 0, sizeof(struct mmap_file) );
  mmap_file->map_id = thread_current()->mmap_id++;
  mmap_file->file = file_reopen( file );
  list_init( &mmap_file->vme_list );

  /* get length of file */
  length = file_length( mmap_file->file );

  /* while length = 0, create vm entry and add. */
  while( length > 0 )
  {
    /* if already allocated area, return -1. */
    if( find_vme( addr ) != NULL )
      return -1;

    /* create vm entry */
    vme = (struct vm_entry*) malloc (sizeof(struct vm_entry));
    if( vme == NULL )
      return -1;

    /* initialize vme */
    memset( vme, 0, sizeof(struct vm_entry) );
    vme->type = VM_FILE;
    vme->vaddr = addr;
    vme->writable = true;
    vme->is_loaded = false;
    vme->file = mmap_file->file;
    vme->offset = 0;
    vme->read_bytes = length > PGSIZE ? PGSIZE : length;
    vme->zero_bytes = 0;

    /* insert into mmap_file and current thread */
    insert_vme( &thread_current()->vm, vme );
    list_push_back( &mmap_file->vme_list, &vme->mmap_elem );

    /* set length, offset */
    length -= PGSIZE;
    offset += PGSIZE;
    addr += PGSIZE;
  }

  /* insert into thread's mmap_list */
  list_push_back( &thread_current()->mmap_list, &mmap_file->elem );

  return mmap_file->map_id;
}

/*
 * System Call
 * Assignment 12 - munmap : unmap mapped file
 */
static void
munmap( int map_id )
{
  struct list_elem *e;
  struct mmap_file *mmap_file;

  /* search through all mmap_list */
  for( e = list_begin( &thread_current()->mmap_list );
       e != list_end( &thread_current()->mmap_list );
       /* empty */ )
  {
    mmap_file = list_entry( e, struct mmap_file, elem );

    /* check map_id */
    if( mmap_file->map_id == map_id )
    {
      do_munmap( mmap_file );
      
      /* remove from list */
      e = list_remove( e );

      /* free mmap_elem */
      free( mmap_file );
    }
    else
      e = list_next( e );
  }
}








