#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

uint blocked_calls[MAX_SH] = {0};  // Define and initialize
int current_sh = 0;

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int sys_gethistory(void) {
  acquire(&phistory_lock);

  int count = phistory_count; 

  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
      if (phistory[j].start_time > phistory[j + 1].start_time) {
        // Swap it
        struct proc_history temp = phistory[j];
        phistory[j] = phistory[j + 1];
        phistory[j + 1] = temp;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    struct proc_history *ph = &phistory[i];
    cprintf("%d %s %d\n", ph->pid, ph->name, ph->mem_usage);
  }

  release(&phistory_lock);
  return count;
}

int sys_block(void){
  int id;
  if(argint(0, &id)<0){
    return -1;
  }
  return block(id);
}

int sys_unblock(void){
  int id;
  if(argint(0,&id)<0){
    return -1;
  }
  return unblock(id);
}

int sys_signal(void){
  sighandler_t handler;
  
  if(argptr(0, (void*)&handler, sizeof(handler)) < 0)
    return -1;
    
  myproc()->signal_handler = handler;
  return 0;
}

