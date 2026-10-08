#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int nonblock;
  if (argaddr(0, &p) < 0 || argint(1, &nonblock) < 0) return -1;
  return wait(p, nonblock);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  printf("Save the context of the process to the memory region from address %p to %p\n",
         &p->context, &p->context + 1);
  printf("Current running process pid is %d and user pc is %p\n", p->pid,
         p->trapframe->epc);
  mycpu()->yield_trace = 1;
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
  return 0;
}

uint64 sys_seccomp_ctl(void) {
  int op;
  uint64 value;
  struct proc *p = myproc();
  if (argint(0, &op) < 0 || argaddr(1, &value) < 0) return -1;
  switch (op) {
    case 0:
      p->syscall_mask = value;
      return 0;
    case 1:
      if (value > 0x7fffffffUL) return -1;
      p->max_children = value;
      return 0;
    default:
      return -1;
  }
}

uint64 sys_seccomp_getlog(void) {
  uint64 buf, lenaddr;
  int len;
  struct proc *p = myproc();
  if (argaddr(0, &buf) < 0 || argaddr(1, &lenaddr) < 0 ||
      copyin(p->pagetable, (char *)&len, lenaddr, sizeof(len)) < 0 || len < 0)
    return -1;
  if (len > p->syscall_loglen) len = p->syscall_loglen;
  if (copyout(p->pagetable, buf, (char *)p->syscall_log, len * sizeof(uint64)) < 0 ||
      copyout(p->pagetable, lenaddr, (char *)&len, sizeof(len)) < 0)
    return -1;
  return 0;
}
