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
  *retval = 1;
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

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
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
  struct proc * childProc = proc_create_runprogram(curproc->p_name);

  /* check if childproc is failed due to memory constraint */

  if (!childProc) *retval = 1; return ENOMEM;

  if (childProc->pid > 0) {
    /* create new addr space and copy the old one over */
    int result = as_copy(curproc->p_addrspace, &(childProc->p_addrspace));
    if (result != 0){
      /* no memory */
      proc_destroy(childProc);
      return result;
    }
    spinlock_acquire(&curproc->p_lock);
    curproc->countChild++;
    childProc->childArray[curproc->countChild] = childProc->pid;
    spinlock_release(&curproc->p_lock);

    struct trapframe *childTf = kmalloc(sizeof(struct trapframe));

    if (childTf == NULL) *retval = 1; return ENOMEM;

	  memcpy(childTf,tf,sizeof(struct trapframe));
    result = thread_fork("child process", childProc, (void*)enter_forked_process, (void*)childTf,0);
    if (result) {
      *retval = 1;
      proc_destroy(childProc);
      return result;
    }
    *retval = childProc->pid; //success
    kfree(childTf);
  } else {
    *retval = 1;
    proc_destroy(childProc);
    return 0;

  }
}
#endif
