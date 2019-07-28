#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <mips/trapframe.h>
#include "opt-A2.h"

#if OPT_A2
static void isExist(pid_t pid, int *child_flag) {

  lock_acquire(curproc->lk_child);

  for (int i = 0; i < curproc->countChild; i++){
    if (curproc->childArray[i] == pid) {
      *child_flag = 1;
      break;
    }
  }
  lock_release(curproc->lk_child);
  //return child_flag;
}


#endif //OPT_A2


  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);

#if OPT_A2

  lock_acquire(lk_tb);

  curproc->isAlive = false;
  curproc->code = exitcode;
  curproc->status = _MKWAIT_EXIT(exitcode);

  exitTable[curproc->pid - 2] = exitcode;
  aliveTable[curproc->pid - 2] = false;

  cv_broadcast(curproc->cv_child, lk_tb);

  lock_release(lk_tb);

#endif //OPT_A2


  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  proc_destroy(p);

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}



/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A2

  *retval = curproc->pid; //MC 3:22 am

#endif // OPT_A2
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;
  if (options != 0) {
    return(EINVAL);
  }



#if OPT_A2

  if (!status) return EFAULT;

  int child_flag = 0;
  int *ptr = &child_flag;
  isExist(pid, ptr);
  if(*ptr == 0){
      //*retval = ECHILD;
      return ECHILD;
  }

  lock_acquire(lk_tb);
  while (aliveTable[pid - 2] == true) {
    cv_wait(procTable[pid - 2]->cv_child, lk_tb);
  }


  int exitcode = exitTable[pid - 2];

  exitstatus = _MKWAIT_EXIT(exitcode);

  lock_release(lk_tb);

#endif //OPT_A2

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */


  /* for now, just pretend the exitstatus is 0 */

  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }

  *retval = pid;
  return(0);
}

#ifdef OPT_A2
int
sys_fork(struct trapframe *tf, pid_t *retval) {
  KASSERT(tf);
	KASSERT(retval);
  struct proc * childProc = proc_create_runprogram("children");

  /* check if childproc is failed due to memory constraint */

  if (!childProc){
    *retval = 1;
    return ENOMEM;
  }

  if (childProc->pid > 0) {
    /* create new addr space and copy the old one over */
    int result = as_copy(curproc->p_addrspace, &(childProc->p_addrspace));
    if (result != 0){
      /* no memory */
      proc_destroy(childProc);
      return result;
    }
    spinlock_acquire(&curproc->p_lock);

    curproc->childArray[curproc->countChild] = childProc->pid;
    curproc->countChild++;
    spinlock_release(&curproc->p_lock);

    struct trapframe *childTf = kmalloc(sizeof(struct trapframe));


	  memcpy(childTf,tf,sizeof(struct trapframe));
    result = thread_fork("child process", childProc, (void*)enter_forked_process, (void*)childTf,0);
    if (result) {
      proc_destroy(childProc);
      return result;
    }
    *retval = childProc->pid; //success
    return 0;
  } else {
    proc_destroy(childProc);
    return 0;

  }
}
#endif

#ifdef OPT_A2

/*
From Coursenote: Replaces currently executing program with a newly loaded program image. Process id
remains unchanged. Path of the program is passed in as program. Arguments to the
program (args) is an array of NULL terminated strings. The array is terminated by a
NULL pointer. In the new user program, argv[argc] should == NULL.
*/

int
sys_execv(userptr_t interface_progname, userptr_t interface_args){
  char * progname = (char*) interface_progname;
	char ** args = (char**) interface_args;

  /* Sanity Check */
  if(progname == NULL || args == NULL) return EFAULT;

	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

  /* Count number of arguments */
  int argc = 0;
  while(args[argc])	{ argc++; }

  /* Allocate memory in the kernel's address as copyinster does not create memory */
  char * addr_name = kmalloc(sizeof(char) * (strlen(progname) + 1));
  char ** argv = kmalloc(sizeof(char *) * (argc + 1));

  if (addr_name == NULL || argv == NULL) return ENOMEM;

  /* copy program name from user to kernel address */
  if (copyinstr((const_userptr_t) progname, addr_name, strlen(progname) + 1, NULL)) {
    return result;
  }

  /* Copy each argument str from user to kernel address */
  for(int i = 0; i < argc; i++) {
		argv[i] = kmalloc(sizeof(char) * (strlen(args[i]) + 1));
		if (argv[i] == NULL) return ENOMEM;
		result = copyinstr((const_userptr_t)args[i], argv[i], strlen(args[i]) + 1, NULL);
		if (result) return result;
	}

  /* Must set explicitly the last element as Null */
  argv[argc] = NULL;

	/* Open the file. */
	result = vfs_open(progname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}


	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	struct addrspace* oldAddrSpc = curproc_setas(as); //curproc_setas return the old address space
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
    curproc_setas(oldAddrSpc); //reverse
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
    curproc_setas(oldAddrSpc); //reverse
		return result;
	}

//int argumentPassing(vaddr_t *stackptr) {
  vaddr_t strAddr[argc+1];
	strAddr[argc] = 0;

  /* copy program argument strings to stack first */
	for (int i = argc - 1; i >= 0; i--) {
		stackptr -= ROUNDUP((sizeof(char) * (strlen(argv[i]) + 1)), 8);
		strAddr[i] = stackptr;
		result = copyoutstr(argv[i], (userptr_t)(stackptr), (sizeof(char) * (strlen(argv[i]) + 1)), NULL);
    //kfree(argv[i]);
    if(result) {
      return result;
    }
	}
  /* make each of the upper part of stack point to the lower corresponding string */
  for (int i = argc; i >= 0; i--) {
    stackptr -= ROUNDUP(sizeof(vaddr_t), 4);
		copyout(&strAddr[i], (userptr_t)(stackptr), ROUNDUP(sizeof(vaddr_t), 4));
    if(result) {
      return result;
    }
  }
//}
  /* Delete old address space */
  as_destroy(oldAddrSpc);
	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}



#endif //OPT_A2
