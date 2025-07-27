#include "xv6pp.h"

enum syscall_number {
  SYS_fork = 1,
  SYS_exit,
  SYS_wait,
  SYS_pipe,
  SYS_read,
  SYS_kill,
  SYS_exec,
  SYS_fstat,
  SYS_chdir,
  SYS_dup,
  SYS_getpid,
  SYS_sbrk,
  SYS_sleep,
  SYS_uptime,
  SYS_open,
  SYS_write,
  SYS_mknod,
  SYS_unlink,
  SYS_link,
  SYS_mkdir,
  SYS_close
};

// Fetch the uint64 at addr from the current process.
int fetchaddr(uint64 addr, uint64 *ip) {
  process *p = kernel.cpus.curproc();
  if (addr >= p->get_sz() || addr + sizeof(uint64) > p->get_sz()) // both tests needed, in case of overflow
    return -1;
  if (kernel.memory.copyin(p->get_pagetable(), (char*) ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int fetchstr(uint64 addr, char *buf, int max) {
  process *p = kernel.cpus.curproc();
  if (kernel.memory.copyinstr(p->get_pagetable(), buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

uint64 argraw(int n) {
  process *p = kernel.cpus.curproc();
  switch (n) {
  case 0:
    return p->get_trapframe()->a0;
  case 1:
    return p->get_trapframe()->a1;
  case 2:
    return p->get_trapframe()->a2;
  case 3:
    return p->get_trapframe()->a3;
  case 4:
    return p->get_trapframe()->a4;
  case 5:
    return p->get_trapframe()->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
void argint(int n, int *ip) {
  *ip = argraw(n);
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
void argaddr(int n, uint64 *ip) {
  *ip = argraw(n);
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int argstr(int n, char *buf, int max) {
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
  nullptr,
  sys_fork,
  sys_exit,
  sys_wait,
  sys_pipe,
  sys_read,
  sys_kill,
  sys_exec,
  sys_fstat,
  sys_chdir,
  sys_dup,
  sys_getpid,
  sys_sbrk,
  sys_sleep,
  sys_uptime,
  sys_open,
  sys_write,
  sys_mknod,
  sys_unlink,
  sys_link,
  sys_mkdir,
  sys_close,
};

void syscall(void) {
  process *p = kernel.cpus.curproc();
  auto num = p->get_trapframe()->a7;
  if (num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Use num to lookup the system call function for num, call it,
    // and store its return value in p->trapframe->a0
    p->get_trapframe()->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %ld\n", p->get_pid(), p->get_name(), num);
    p->get_trapframe()->a0 = -1;
  }
}

