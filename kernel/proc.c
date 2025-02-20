#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
struct cpu cpus[NCPU];

struct proc proc[NPROC];



struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// void inc_waiting()
// {
//   // acquire(&ptable.lock);

//   for (struct proc *p = proc; p < &proc[NPROC]; p++)
//     {
//       if (p->state == RUNNABLE)
//       {
//         p->time_wait++;
//         p->total_wait++;
//       }

//       // if(p->pid>3)
//       // cprintf("graph %d %d %d\n",ticks, p->pid, p->priority);
//     }

//   // release(&ptable.lock);
// }

int
waitx(uint64 addr, uint* rtime, uint* wtime)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          *rtime = np->trtime;
          *wtime = np->etime - np->ctime - np->trtime;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }

    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}


// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->ctime = ticks;
  p->pid = allocpid();
  p->state = USED;
p->rtime = 0;
p->trtime = 0;
 p->num_runs = 0;
 p->time_wait = 0;
 p->priority = 60;
p->etime = 0;
p->total_wait = 0;
 
  p->in_queue = 0;      // not assigned a queue
  p->cpu_ticks = 0;
  #ifdef MLFQ
    p->priority = 0; 
    // signifies initial queue alloted to process
    
    for(int i=0; i<5; i++)
      p->q_ticks[i] = 0;    //ticks in each queue

  #else
    p->priority = 60;

    for (int i = 0; i < 5; i++)
      p->q_ticks[i] = -1; // inapplicable
#endif
  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}


// int set_priority(int new_priority, int pid)
// {
  
//   struct proc *p;
   
  
//   if(new_priority>100 || new_priority<0)
//    {
//      printf("This priority is invalid!\n");
//      return -1;
//    }
//   for(p = proc; p < &proc[NPROC]; p++) 
//   {
//     acquire(&p->lock);
//     if(p->pid == pid)
//     {
//       printf("priority :%d pid: %%d\n", p->priority, p->pid);
//       int old_priority = p->priority;
//           p->priority = new_priority;
//           p->rtime = 0;
//         p->sleeptime = 0;
//       release(&p->lock);

//       if (old_priority > new_priority)
//             yield();
//         break;

//  printf("priority :%d pid: %%d\n", p->priority, p->pid);
//           return old_priority;
//     }
    
//   }
 
//   release(&p->lock);
  
//   return -1;
// }
// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // copy trace mask
  np->tracemask = p->tracemask;
  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;
  p->etime = ticks;
  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();
#ifdef RR
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->num_runs++;
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
#else
#ifdef FCFS
    struct proc *firstComeProc = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        if (!firstComeProc || firstComeProc->ctime > p->ctime)
        {
          if (firstComeProc)
            release(&firstComeProc->lock);
          firstComeProc = p;
          continue;
        }
      }
      release(&p->lock);
    }
    // As long as firstproc contains a process
    if (firstComeProc)
    {
      firstComeProc->num_runs++;
      firstComeProc->state = RUNNING;
      c->proc = firstComeProc;
      swtch(&c->context, &firstComeProc->context); //context switching
      c->proc = 0;
      release(&firstComeProc->lock);
    }
#else
#ifdef PBS
    struct proc *priorityProc = 0;
    int dp = 120;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      int niceness = 5;

      if(p->num_runs > 0 && p->sleeptime + p->rtime != 0)
      {
        niceness = (int)((p->sleeptime * 10) / (p->sleeptime + p->rtime));
      }
      int proc_dp = p->priority - niceness + 5 < 100 ? (p->priority - niceness + 5) : 100;
      proc_dp = proc_dp < 100 ? proc_dp: 100;
      proc_dp = 0 > proc_dp ? 0:proc_dp;

      if (p->state == RUNNABLE)
      {
        if (!priorityProc || proc_dp < dp || 
        (dp==proc_dp && 
        (p->num_runs < priorityProc->num_runs || (p->num_runs == priorityProc->num_runs && p->ctime < priorityProc->ctime )))) 
        {
            if(priorityProc)
              release(&priorityProc->lock);

            dp = proc_dp;
            priorityProc = p;
            continue;
        }
      }
      release(&p->lock);
    }
    if (priorityProc)
    {
      priorityProc->num_runs++;
      priorityProc->starttime = ticks;
      priorityProc->state = RUNNING;
      priorityProc->rtime = 0;
      priorityProc->sleeptime = 0;

      c->proc = priorityProc;
      swtch(&c->context, &priorityProc->context);
      c->proc = 0;
      release(&priorityProc->lock);
    }

#else
#ifdef MLFQ

#endif
#endif
#endif
#endif
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    #if defined RR || defined FCFS
    int wtime = ticks - p->ctime - p->total_rtime;
    printf("%d\t%s\t%d\t%d\t%d\n", p->pid, state, p->trtime, wtime, p->num_runs);
    #else
    #ifdef PBS
    int wtime = ticks - p->ctime - p->total_rtime;
    printf("%d\t%d\t%s\t%d\t%d\t%d\n", p->pid, p->priority , state, p->trtime, wtime, p->num_runs);
    #else
    #ifdef MLFQ
    int wtime = ticks - p->ctime - p->total_rtime;
    printf("%d %d %s %d %d %d\n", p->pid, p->priority , state, p->trtime, wtime, p->num_runs);
    #endif
    #endif
    #endif
  }
}

void
update_time()
{
  struct proc *p;
  for(p = proc; p < & proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if(p->state ==RUNNING)
    {
      p->rtime++;
      p->trtime++;
    }
    if(p->state == SLEEPING)
    {
      p->sleeptime++;
    }
    release(&p->lock);
  }
}
// struct node *create_node(void)
// {
//   for (int i = 0; i < NPROC; i++)
//   {
//     // null process means it is available for use
//     if (free_nodes[i].proc == 0)
//       return &free_nodes[i];
//   }
//   printf("Shortage of available nodes!\n");
//   return 0;
// }

// void free_node(struct node *node)
// {
//   node->proc = 0;
// }

// void init_q(struct Queue *q)
// {
//   q->head = 0;
//   q->tail = 0;
//   q->len = 0;
// }

// void push(struct Queue *q, struct proc *proc)
// {
//   proc->in_queue = 1;
//   proc->cpu_ticks = 0;

//   struct node *new = create_node();
//   new->proc = proc;
//   new->next = 0;
//   q->len++;

//   if (q->tail == 0)
//   {
//     q->head = q->tail = new;
//     return;
//   }

//   q->tail->next = new;
//   q->tail = new;
// }

// void pop(struct Queue *q)
// {
//   if (q->head == 0)
//   {
//     printf("Empty Queue!\n");
//     return;
//   }

//   q->len--;
//   q->head->proc->in_queue = 0; // got popped

//   free_node(q->head);
//   q->head = q->head->next;

//   if (q->head == 0)
//     q->tail = 0;
// }


// int set_priority(int new_priority, int pid)
// {
  
//   struct proc *p;
   
  
//   if(new_priority>100 || new_priority<0)
//    {
//      printf("This priority is invalid!\n");
//      return -1;
//    }
//   for(p = proc; p < &proc[NPROC]; p++) 
//   {
//     acquire(&p->lock);
//     if(p->pid == pid)
//     {
//       printf("priority :%d pid: %%d\n", p->priority, p->pid);
//       int old_priority = p->priority;
//           p->priority = new_priority;
//           p->rtime = 0;
//         p->sleeptime = 0;
//       release(&p->lock);

//       if (old_priority > new_priority)
//             yield();
//         break;

//  printf("priority :%d pid: %%d\n", p->priority, p->pid);
//           return old_priority;
//     }
    
//   }
 
//   release(&p->lock);
  
//   return -1;
// }

int set_priority(int priority, int pid)
{
    // int val = -1;
    int old_priority =-1;
    struct proc *p;
  if(priority>100 || priority<0)
   {
     printf("This priority is invalid!\n");
     return -1;
   }
    for(p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);

      if(p->pid == pid)
      {
        
        old_priority = p->priority;
        p->priority = priority;
        p->rtime = 0;
        p->sleeptime = 0;
        release(&p->lock);

        if (old_priority > priority)
            yield();
        break;
        
      }
      release(&p->lock);
    }
    return old_priority;
}