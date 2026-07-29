#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct spinlock phistory_lock;
struct proc_history phistory[MAX_HISTORY];
int phistory_count = 0;

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&phistory_lock, "phistory");  // HISTORY LOCK

}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;
  
  p->start_later = 0;
  p->exec_ticks = -1;
  p->rem_ticks = -1;
  p->scheduler_change = 0;

  acquire(&tickslock);
  p->creation_time = ticks;
  release(&tickslock);

  p->start_time = -1;
  p->end_time = -1;
  p->waiting_time = 0;
  p->context_switches = 0;
  p->cpu_ticks = 0;
  p->base_priority = INIT_PRIORITY;

  p->signal_handler = 0;    // No handler initially
  p->handler_pending = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

static void
record_history(struct proc *p)
{
    acquire(&phistory_lock);

    if(phistory_count < MAX_HISTORY){
        struct proc_history *h = &phistory[phistory_count++];

        h->pid = p->pid;
        safestrcpy(h->name, p->name, sizeof(h->name));
        h->mem_usage = p->sz;
        h->start_time = p->start_time;
    }

    release(&phistory_lock);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  if(current_sh > 0){
    current_sh-=1;
    // cprintf("Exited.. with current number of shells : %d\n",current_sh);
  }else{
    cprintf("allocated space is small for the stack\n");
  }
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
  record_history(curproc);
  
  curproc->end_time = ticks;
  int tat = curproc->end_time - curproc->creation_time;
  int wt =  curproc->waiting_time;
  int rt = curproc->start_time - curproc->creation_time;
  int cs = curproc->context_switches;
  // cprintf("start_time: %d, end_time: %d, tat: %d, wt: %d, rt: %d\n",curproc->start_time,curproc->end_time,tat,wt,rt);
  // cprintf("exec_time: %d, cpu_tcks: %d, remaining_ticks: %d,exec_ticks: %d\n", tat-wt, curproc->cpu_ticks,curproc->rem_ticks,curproc->exec_ticks);
  cprintf("PID: %d\nTAT: %d\nWT: %d\nRT: %d\n#CS: %d\n",curproc->pid,tat,wt,rt,cs);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      if(curproc->pid==2 && p->state==SUSPENDED) continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc *final;
  struct cpu *c = mycpu();

  c->proc = 0;

  for(;;){
    // Enable interrupts.
    sti();

    acquire(&ptable.lock);

    final = 0;
    int curr_priority = 0;

    // Find the highest priority runnable process.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      int priority = p->base_priority
                     - (ALPHA * p->cpu_ticks)
                     + (BETA * p->waiting_time);

      if(priority < 0)
        priority = 0;

      // Pick higher priority process.
      // Break ties using smaller PID.
      if(final == 0 ||
         priority > curr_priority ||
         (priority == curr_priority && p->pid < final->pid)){
        curr_priority = priority;
        final = p;
      }
    }

    if(final){
      // Count context switches.
      if(final != c->last_proc){
        if(c->last_proc)
          c->last_proc->context_switches++;
        c->last_proc = final;
      }

      // Record first execution time.
      if(final->start_time == -1)
        final->start_time = ticks;

      // Run the selected process.
      c->proc = final;
      switchuvm(final);
      final->state = RUNNING;

      swtch(&(c->scheduler), final->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

void
yield_no_state_change(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING || p->state == SUSPENDED)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie",
  [SUSPENDED] "suspended"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int block(int id)
{
  if (id == 1 || id == 2 || id < 0 || id >= MAX_SYSCALLS)
  {
    return -1;
  }
  struct proc *curr_proc = myproc();
  if(strncmp(curr_proc->name, "sh",2)!=0) return -1;
  blocked_calls[current_sh] |= (1U << id);
  return 0;
}

int unblock(int id)
{
  if (id == 1 || id == 2 || id < 0 || id >= MAX_SYSCALLS)
  {
    return -1;
  }
  struct proc *curr_proc = myproc();
  if(strncmp(curr_proc->name, "sh" , 2) != 0) return -1;
  if (current_sh > 0) { 
    uint parent_blocked = blocked_calls[current_sh - 1];
    if ((parent_blocked & (1U << id))) {
        return -1; // Parent didn't block this call
    }
  }
  blocked_calls[current_sh] &= ~(1U << id);
  return 0;
}

void killp() {
  struct proc *p;
  cprintf("Ctrl-C is detected by xv6\n");
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->pid > 2 && p->state!=UNUSED) { 
          p->killed = 1;
          if(p->state == SLEEPING || p->state==SUSPENDED)
            p->state = RUNNABLE;
      }
  }
  release(&ptable.lock);
}


struct proc* get_proc(int x){
  struct proc *p;
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if(p->pid==x) return p;
  }
  return 0;
}

void suspend_all() {
  struct proc *p,*init_proc,*sh;
  acquire(&ptable.lock);  
  cprintf("Ctrl-B is detected by xv6\n");

  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->pid > 2 && (p->state==RUNNING || p->state==RUNNABLE || p->state==SLEEPING)) {
          p->state=SUSPENDED;
      }
  }
  sh=get_proc(2);
  if(sh->state==SLEEPING) sh->state=RUNNABLE;
  release(&ptable.lock);
  
}

void resume_all() {
  struct proc *p;
  cprintf("Ctrl-F is detected by xv6\n");
  acquire(&ptable.lock);
  // cprintf("current pid is %d and %d ",myproc()->pid,myproc()->state);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->pid > 2 && p->state==SUSPENDED){
            p->state = RUNNABLE;
            get_proc(2)->state=SLEEPING;
      }
      else if (p->pid > 2 && p->state==RUNNING){
        p->state = RUNNABLE;
      }
  }
  release(&ptable.lock);
}



void invoke_custom_handler(){
  cprintf("Ctrl-G is detected by xv6\n");
  struct proc *p;
  acquire(&ptable.lock);
  for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->pid > 2 && p->state!=UNUSED && p->signal_handler != 0) {
          p->handler_pending=1;
      }
  }
  release(&ptable.lock);
}

void update_waiting_time(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == RUNNABLE){
      p->waiting_time++;
    }
  }
  release(&ptable.lock);
}


int custom_fork(int start_later, int exec_ticks){

  
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  /* ---- i initailsed here --------------------  */
  np->start_later = start_later;
  np->exec_ticks = exec_ticks;
  np->rem_ticks = exec_ticks;
  np->creation_time = ticks;

  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);
  if(start_later){
    np->state = SLEEPING;
    np->scheduler_change = 1;
  }else{
    np->state = RUNNABLE;
  }

  release(&ptable.lock);

  return pid;


}


int scheduler_start(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->scheduler_change == 1){
      
      p->state = RUNNABLE;
      if(p->exec_ticks == 0){
        p->killed = 1;
      }
      p->scheduler_change = 0;
    }
  }
  release(&ptable.lock);
  return 0;
}
