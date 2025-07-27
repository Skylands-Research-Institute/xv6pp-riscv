#include "types.h"
#include "elf.h"
#include "process.h"
#include "xv6pp.h"
#include "lock_guard.h"

process::process() {
}

void process::free() {
  if (trapframe)
    kernel.allocator.free(trapframe);
  trapframe = nullptr;
  if (pagetable)
    kernel.memory.free(pagetable, sz);
  pagetable = 0;
  sz = 0;
  pid = 0;
  parent = 0;
  name[0] = 0;
  chan = 0;
  killed = 0;
  xstate = 0;
  state = process_state::UNUSED;
}

int process::grow(int n) {
  uint64 newsz = sz;
  if (n > 0) {
    if ((newsz = kernel.memory.alloc(pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if (n < 0) {
    newsz = kernel.memory.dealloc(pagetable, sz, sz + n);
  }
  sz = newsz;
  return 0;
}

int process::fork() {
  // Allocate new process.
  process *np = kernel.processes.alloc();
  if (!np) {
    return -1;
  }
  // Copy user memory from parent to child.
  if (kernel.memory.copy(pagetable, np->pagetable, sz) < 0) {
    np->free();
    np->lock.release();
    return -1;
  }
  np->sz = sz;
  // copy saved user registers.
  *(np->trapframe) = *trapframe;
  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;
  // increment reference counts on open file descriptors.
  for (auto i = 0; i < NOFILE; i++) {
    if (ofile[i]) {
      ofile[i]->dup();
      np->ofile[i] = ofile[i];
    }
  }
  np->cwd = kernel.fsystem.idup(cwd);
  safestrcpy(np->name, name, sizeof(name));
  np->lock.release();
  {
    lock_guard<spin_lock> g(kernel.processes.wait_lock);
    np->parent = this;
  }
  {
    lock_guard<spin_lock> g(np->lock);
    np->state = process_state::RUNNABLE;
  }
  return np->pid;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
int process::loadseg(pagetable_t pt, uint64 va, struct inode *ip, uint offset, uint sz) {
  for (uint i = 0; i < sz; i += PGSIZE) {
    auto pa = kernel.memory.walkaddr(pt, va + i);
    if (!pa)
      panic("loadseg: address should exist");
    auto n = PGSIZE;
    if (sz - i < PGSIZE)
      n = sz - i;
    if (kernel.fsystem.readi(ip, 0, (uint64) pa, offset + i, n) != n)
      return -1;
  }
  return 0;
}

static inline int flags2perm(int flags) {
  int perm = 0;
  if (flags & 0x1)
    perm = PTE_X;
  if (flags & 0x2)
    perm |= PTE_W;
  return perm;
}

static inline int error(pagetable_t newpagetable, uint64 newsz, struct inode *ip) {
  if (newpagetable)
    kernel.processes.free_pagetable(newpagetable, newsz);
  if (ip) {
    kernel.fsystem.iunlockput(ip);
    kernel.log.end_op();
  }
  return -1;
}

int process::exec(char *path, char **argv) {
  char *s, *last;
  int i, off;
  uint64 argc, newsz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t newpagetable = 0, oldpagetable;

  kernel.log.begin_op();

  if ((ip = kernel.fsystem.namei(path)) == 0) {
    kernel.log.end_op();
    return -1;
  }
  kernel.fsystem.ilock(ip);

  // Check ELF header
  if (kernel.fsystem.readi(ip, 0, (uint64) &elf, 0, sizeof(elf)) != sizeof(elf))
    return error(newpagetable, newsz, ip);

  if (elf.magic != ELF_MAGIC)
    return error(newpagetable, newsz, ip);

  if (!(newpagetable = kernel.processes.create_pagetable(this)))
    return error(newpagetable, newsz, ip);

  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
    if (kernel.fsystem.readi(ip, 0, (uint64) &ph, off, sizeof(ph)) != sizeof(ph))
      return error(newpagetable, newsz, ip);
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      return error(newpagetable, newsz, ip);
    if (ph.vaddr + ph.memsz < ph.vaddr)
      return error(newpagetable, newsz, ip);
    if (ph.vaddr % PGSIZE != 0)
      return error(newpagetable, newsz, ip);
    uint64 sz1;
    if ((sz1 = kernel.memory.alloc(newpagetable, newsz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      return error(newpagetable, newsz, ip);
    newsz = sz1;
    if (loadseg(newpagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      return error(newpagetable, newsz, ip);
  }
  kernel.fsystem.iunlockput(ip);
  kernel.log.end_op();
  ip = 0;

  uint64 oldsz = sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  newsz = PGROUNDUP(newsz);
  uint64 sz1;
  if ((sz1 = kernel.memory.alloc(newpagetable, newsz, newsz + (USERSTACK + 1) * PGSIZE, PTE_W)) == 0)
    return error(newpagetable, newsz, ip);
  newsz = sz1;
  kernel.memory.clear(newpagetable, newsz - (USERSTACK + 1) * PGSIZE);
  sp = newsz;
  stackbase = sp - USERSTACK * PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  for (argc = 0; argv[argc]; argc++) {
    if (argc >= MAXARG)
      return error(newpagetable, newsz, ip);
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      return error(newpagetable, newsz, ip);
    if (kernel.memory.copyout(newpagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      return error(newpagetable, newsz, ip);
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    return error(newpagetable, newsz, ip);
  if (kernel.memory.copyout(newpagetable, sp, (char*) ustack, (argc + 1) * sizeof(uint64)) < 0)
    return error(newpagetable, newsz, ip);

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(name, last, sizeof(name));

  // Commit to the user image.
  oldpagetable = pagetable;
  pagetable = newpagetable;
  sz = newsz;
  trapframe->epc = elf.entry;  // initial program counter = main
  trapframe->sp = sp; // initial stack pointer
  kernel.processes.free_pagetable(oldpagetable, oldsz);

  return argc; // this ends up in a0, the first argument to main(argc, argv)
}

void process::exit(int status) {
  if (this == kernel.processes.initprocess)
    panic("init exiting");
  // Close all open files.
  for (int fd = 0; fd < NOFILE; fd++) {
    if (ofile[fd]) {
      auto f = ofile[fd];
      f->close();
      ofile[fd] = 0;
    }
  }
  kernel.log.begin_op();
  kernel.fsystem.iput(cwd);
  kernel.log.end_op();
  cwd = 0;
  {
    lock_guard<spin_lock> g(kernel.processes.wait_lock);
    // Give any children to init.
    kernel.processes.reparent(this);
    // Parent might be sleeping in wait().
    kernel.processes.wakeup(parent);
    lock.acquire();
    xstate = status;
    state = process_state::ZOMBIE;
  }
  // Jump into the scheduler, never to return.
  kernel.scheduler.sched(this);
  panic("zombie exit");
}

void process::sleep(void *c, spin_lock &lk) {
  {
    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.
    lock_guard<spin_lock> g(lock);
    lk.release();
    // Go to sleep.
    chan = c;
    state = process_state::SLEEPING;
    kernel.scheduler.sched(this);
    // Tidy up.
    chan = nullptr;
  }
  // Reacquire original lock.
  lk.acquire();
}

bool process::get_killed() {
  lock_guard<spin_lock> g(lock);
  return killed;
}

void process::set_killed(bool k) {
  lock_guard<spin_lock> g(lock);
  killed = k;
}

