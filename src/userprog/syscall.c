#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
void check_address (void *addr);
void get_argument (void *esp, int *arg, int count);
void halt();
void exit(int status);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	int syscall_index;
	int *arguments;

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
			exit( *((int*)f->esp + 1));
			break;

		case SYS_CREATE:
			//get 2 arguments from stack
			arguments = (int*)malloc(sizeof(int)*2);
			get_argument(f->esp, arguments, 2);

			//check if arg[1] is valid
			check_address(arguments[0]);

			f->eax = create((const char*)arguments[0], (unsigned int)arguments[1]);

			free(arguments);
			break;

		case SYS_REMOVE:
			//check if arg[1] is valid
			check_address( *((const char**)(f->esp) + 1) );

			f->eax = remove( *((const char**)(f->esp) + 1) );
			break;

		default:
			printf("syscall_index : %d\n", syscall_index);
			thread_exit();
	}
}

/*
 * check if address is in 'user' area.
 */
void
check_address (void *addr)
{
	if( (unsigned int)addr < 0x8048000 || (unsigned int)addr >= 0xc0000000 )
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

	//exit
	thread_exit();
}

/*
 * create : create file of initial size
 */
bool
create (const char *file, unsigned initial_size)
{
	return filesys_create(file, initial_size);
}


/*
 * remove : remove file
 */
bool
remove (const char *file)
{
	return filesys_remove(file);
}
