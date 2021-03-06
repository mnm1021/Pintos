#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include <kernel/hash.h>
#include "threads/synch.h"
#include "filesys/file.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* Assignment 10 : MLFQS */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

    /* Assignment 6 : Alarm */
    int64_t wakeup_tick;                /* tick for wake-up */

    /* Assignment 9 : Priority Inversion */
    int init_priority;                  /* store initial priority */
    struct lock *wait_on_lock;          /* lock waiting for acquirement */
    struct list donations;              /* store threads donated */
    struct list_elem donation_elem;     /* element for donations */

    /* Assignment 10 : MLFQS */
    int nice;                           /* set nice value for mlfqs */
    int recent_cpu;                     /* set recently-used cpu ticks for mlfqs */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Assignment 3 : Process Hierarchy */
    struct thread *parent;              /* parent thread's descriptor */
    struct list_elem child_elem;        /* element of child_list */
    struct list child_list;             /* list of this thread's child */

    bool loaded;                        /* is loaded on memory? */
    bool exited;                        /* is thread exited? */

    struct semaphore sema_load;         /* semaphore for load */
    struct semaphore sema_wait;         /* semaphore for wait */
    struct semaphore sema_exit;         /* semaphore for exit */

    int exit_status;                    /* exit status value : is set in exit syscall */
    
    /* Assignment 5 : current file */
    struct file *current_file;          /* current open file */

    /* Assignment 4 : File Descriptor */
    struct file* *fd_table;             /* file descriptor table */
    int num_fd;                         /* number of current file descriptors */

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

    /* Assignment 11 : virtual memory */
    struct hash vm;                     /* hash table for vm_entry */

    /* Assignment 12 : mmap */
    struct list mmap_list;              /* list of mmap_file */
    int mmap_id;                        /* mmap_file id */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* Assignment 6 : Alarm */
/* make current thread to be blocked */
void thread_sleep (int64_t ticks);
/* make thread awake from queue */
void thread_awake (int64_t ticks);
/* save next tick */
void update_next_tick_to_awake (int64_t ticks);
/* return next_tick_to_awake */
int64_t get_next_tick_to_awake (void);

/* Assignment 7 : Priority */
/* compare current thread's priority with highest thread priority */
void test_max_priority (void);
/* compare between 2 threads */
bool cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

/* Assignment 9 : Priority Inversion */
/* donate current priority to other thread */
void donate_priority (struct thread *cur);
/* remove lock-holder from donations */
void remove_with_lock (struct thread *cur, struct lock *lock);
/* after thread donation or lock release, refresh priority */
void refresh_priority (struct thread *cur, int *priority);

/* Assignment 10 : MLFQS */
int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void mlfqs_priority (struct thread *t);
void mlfqs_recent_cpu (struct thread *t);
void mlfqs_load_avg (void);
void mlfqs_increment (void);
void mlfqs_recalc (void);

#endif /* threads/thread.h */
