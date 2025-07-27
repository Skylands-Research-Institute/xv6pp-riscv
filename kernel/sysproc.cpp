#include "xv6pp.h"
#include "syscall_args.h"

uint64 sys_exit(void) {
  int n;
  argint(0, &n);
  kernel.cpus.curproc()->exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) {
  return kernel.cpus.curproc()->get_pid();
}

uint64 sys_fork(void) {
  return kernel.cpus.curproc()->fork();
}

uint64 sys_wait(void) {
  uint64 p;
  argaddr(0, &p);
  return kernel.processes.wait(p);
}

uint64 sys_sbrk(void) {
  uint64 addr;
  int n;

  argint(0, &n);
  process *p = kernel.cpus.curproc();
  addr = p->get_sz();
  if (p->grow(n) < 0)
    return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  //uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  return kernel.interrupts.sleep(n);
}

uint64 sys_kill(void) {
  int pid;

  argint(0, &pid);
  return kernel.processes.kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  return kernel.interrupts.get_ticks();
}

