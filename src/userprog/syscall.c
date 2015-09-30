#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
void check_address (void *addr);
void get_argument (void *esp, int *arg, int count);
void halt(void);
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
tid_t exec(const char *cmd_line);
int wait(tid_t tid);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	int syscall_index;
	int arguments[3];

	//check esp, if in user area, get syscall index
	check_address(f->esp);
	syscall_index = *((int*)(f->esp));

	switch( syscall_index )
	{
		case SYS_HALT:
			halt();
			break;

		case SYS_EXIT:
			//get one argument from stack
			exit( *((int*)f->esp + 1) );
			break;

		case SYS_CREATE:
			//get 2 arguments from stack
			get_argument(f->esp, arguments, 2);

			//check if arg[1] is valid
			check_address((void *)arguments[0]);

			f->eax = create((const char*)arguments[0], (unsigned int)arguments[1]);
			break;

		case SYS_REMOVE:
			//check if arg[1] is valid
			check_address( (void *)*((const char**)(f->esp) + 1) );

			f->eax = remove( *((const char**)(f->esp) + 1) );
			break;

		case SYS_EXEC:
			//check if arg[1] is valid
			check_address( (void *)*((const char**)(f->esp) + 1) );

			f->eax = exec( *((const char**)(f->esp) + 1) );
			break;

		case SYS_WAIT:
			// 1 argument from stack
			f->eax = wait( *((int*)f->esp + 1) );
			break;

		default:
			thread_exit();
	}
}

/*
 * check if address is in 'user' area.
 */
void
check_address (void *addr)
{
	if( (unsigned int)addr < 0x8048000 || (unsigned int)addr > 0xc0000000 )
		exit(-1);
}


/*
 * Get arguments from User Stack, store on arg.
 */
void
get_argument (void *esp, int *arg, int count)
{
	int iArg;

	for( iArg=0; iArg<count; iArg++ )
	{
		arg[iArg] = *((int*)esp + iArg);
	}
}

/*
 * halt : shutdown
 */
void
halt ()
{
	shutdown_power_off();
}

/*
 * exit : exit this thread
 */
void
exit (int status)
{
	struct thread *curr_thread;

	//get thread struct
	curr_thread = thread_current();

	//Process Termination Message
	printf("%s: exit(%d)\n", curr_thread->name, status);

	//save exit status
	curr_thread->exit_status = status;

	//exit
	thread_exit();
}

/*
 * create : create file of initial size
 */
bool
create (const char *file, unsigned initial_size)
{
	if( file == NULL )
		exit(-1);

	return filesys_create(file, initial_size);
}


/*
 * remove : remove file
 */
bool
remove (const char *file)
{
	if( file == NULL )
		exit(-1);

	return filesys_remove(file);
}

/*
 * exec : execute command line : reconstruct required
 */
tid_t
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
	ASSERT(child);

	// block this thread until the child loaded
	sema_down( &child->sema_load );

	// if load failed, return -1
	if( child->loaded == false )
		return -1;

	return new_tid;
}

/*
 * wait : return process_wait
 */
int
wait (tid_t tid)
{
	return process_wait(tid);
}






