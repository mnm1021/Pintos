#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "vm/page.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
struct thread* get_child_process (int pid);
int process_add_file (struct file *f);
struct file* process_get_file (int fd);
void process_close_file (int fd);

/* Assignment 11 : handle page fault */
bool handle_mm_fault( struct vm_entry *vme );

/* Assignment 14 : expand stack */
bool verify_stack( void* sp, void* fault_addr );
bool expand_stack( void* vaddr );

#endif /* userprog/process.h */
