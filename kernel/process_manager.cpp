#include "process_manager.h"
#include "xv6pp.h"
#include "lock_guard.h"

extern char trampoline[]; // trampoline.S

process_manager::process_manager(const char *name) :
    kernel_module(name), lock(name), pid_lock("pid_lock"), wait_lock(
        "wait_lock") {
}

void process_manager::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
  for (auto p = processes; p < &processes[NPROC]; p++) {
    p->state = process::process_state::UNUSED;
    p->kstack = KSTACK((int ) (p - processes));
  }
  mapstacks();
  if (kernel.cpus.boot_cpu())
    initial_process();
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void process_manager::mapstacks() {
  auto kpgtbl = kernel.memory.get_kernel_pagetable();
  for (auto p = processes; p < &processes[NPROC]; p++) {
    auto pa = kernel.allocator.alloc();
    if (!pa)
      panic("alloc");
    uint64 va = KSTACK((int ) (p - processes));
    kernel.memory.map(kpgtbl, va, (uint64) pa, PGSIZE, PTE_R | PTE_W);
  }
}

int process_manager::get_next_pid() {
  lock_guard < spin_lock > g(pid_lock);
  return next_pid++;
}

void process_manager::initial_process() {
  auto p = alloc();
  initprocess = p;
  p->cwd = kernel.fsystem.namei((char*)"/");
  p->state = process::process_state::RUNNABLE;
  p->lock.release();
#if 0
  // a user program that calls exec("/init")
  // assembled from ../user/initcode.S
  // od -t xC ../user/initcode
  uchar initcode[] = { 0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97,
      0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02, 0x93, 0x08, 0x70, 0x00, 0x73,
      0x00, 0x00, 0x00, 0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00, 0xef,
      0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69, 0x74, 0x00, 0x00, 0x24, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  process *p = alloc();
  initprocess = p;
  // allocate one user page and copy initcode's instructions
  // and data into it.
  kernel.memory.first(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;
  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer
  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = kernel.fsystem.namei((char*) "/");
  p->state = process::process_state::RUNNABLE;
  p->lock.release();
#endif
}

process* process_manager::alloc() {
  process *p;

  for (p = processes; p < &processes[NPROC]; p++) {
    p->lock.acquire();
    if (p->state == process::process_state::UNUSED) {
      goto found;
    } else {
      p->lock.release();
    }
  }
  return 0;
  found: p->pid = get_next_pid();
  p->state = process::process_state::USED;
  // Allocate a trapframe page.
  if (!(p->trapframe = (struct trapframe*) kernel.allocator.alloc())) {
    kernel.processes.free(p);
    p->lock.release();
    return 0;
  }
  // An empty user page table.
  if (!(p->pagetable = kernel.processes.create_pagetable(p))) {
    kernel.processes.free(p);
    p->lock.release();
    return 0;
  }
  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64) forkret;
  p->context.sp = p->kstack + PGSIZE;
  return p;
}

void process_manager::free(process *p) {
  if (p->trapframe)
    kernel.allocator.free((void*) p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
    free_pagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = process::process_state::UNUSED;
}

void process_manager::forkret() {
  static bool first = true;
  // Still holding p->lock from scheduler.
  kernel.cpus.curproc()->lock.release();
  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    kernel.fsystem.fsinit(ROOTDEV);
    first = false;
    // ensure other cores see first=0.
    __sync_synchronize();
    // We can invoke exec() now that file system is initialized.
    // Put the return value (argc) of exec into a0.
    process* p = kernel.processes.initprocess;
    static char init[] = "/init";   // non-const to satisfy char*
    char* argv[] = { init, nullptr };
    p->trapframe->a0 = p->exec(init, argv);
    if (p->trapframe->a0 == (uint64)-1) {
      panic("exec");
    }
  }
  kernel.interrupts.return_to_user();
}

pagetable_t process_manager::create_pagetable(process *p) {
  pagetable_t pagetable = kernel.memory.create();
  if (!pagetable)
    return 0;
  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if (kernel.memory.mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64) trampoline,
  PTE_R | PTE_X) < 0) {
    kernel.memory.free(pagetable, 0);
    return 0;
  }
  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if (kernel.memory.mappages(pagetable, TRAPFRAME, PGSIZE,
      (uint64) (p->trapframe), PTE_R | PTE_W) < 0) {
    kernel.memory.unmap(pagetable, TRAMPOLINE, 1, 0);
    kernel.memory.free(pagetable, 0);
    return 0;
  }
  return pagetable;
}

void process_manager::free_pagetable(pagetable_t pagetable, uint64 sz) {
  kernel.memory.unmap(pagetable, TRAMPOLINE, 1, 0);
  kernel.memory.unmap(pagetable, TRAPFRAME, 1, 0);
  kernel.memory.free(pagetable, sz);
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void process_manager::reparent(process *p) {
  for (auto pp = processes; pp < &processes[NPROC]; pp++) {
    if (pp->parent == p) {
      pp->parent = initprocess;
      wakeup(initprocess);
    }
  }
}

int process_manager::wait(uint64 addr) {
  lock_guard < spin_lock > g(wait_lock);
  process *p = kernel.cpus.curproc();
  for (;;) {
    // Scan through table looking for exited children.
    bool havekids = false;
    for (auto pp = processes; pp < &processes[NPROC]; pp++) {
      if (pp->parent == p) {
        // make sure the child isn't still in exit() or swtch().
        lock_guard < spin_lock > g(pp->lock);
        havekids = true;
        if (pp->state == process::process_state::ZOMBIE) {
          // Found one.
          auto pid = pp->pid;
          if (addr != 0
              && kernel.memory.copyout(p->pagetable, addr, (char*) &pp->xstate,
                  sizeof(pp->xstate)) < 0) {
            return -1;
          }
          kernel.processes.free(pp);
          return pid;
        }
      }
    }
    // No point waiting if we don't have any children.
    if (!havekids || p->killed) {
      return -1;
    }
    // Wait for a child to exit.
    p->sleep(p, wait_lock);  //DOC: wait-sleep
  }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void process_manager::wakeup(void *chan) {
  for (auto p = processes; p < &processes[NPROC]; p++) {
    if (p != kernel.cpus.curproc()) {
      lock_guard < spin_lock > g(p->lock);
      if (p->state == process::process_state::SLEEPING && p->chan == chan) {
        p->state = process::process_state::RUNNABLE;
      }
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int process_manager::kill(int pid) {
  for (auto p = processes; p < &processes[NPROC]; p++) {
    lock_guard < spin_lock > g(p->lock);
    if (p->pid == pid) {
      p->killed = true;
      if (p->state == process::process_state::SLEEPING) {
        // Wake process from sleep().
        p->state = process::process_state::RUNNABLE;
      }
      return 0;
    }
  }
  return -1;
}

void process_manager::dump() {
  static const char *states[] = { "unused",   // UNUSED = 0
      "used",     // USED = 1
      "sleep ",   // SLEEPING = 2
      "runble",   // RUNNABLE = 3
      "run   ",   // RUNNING = 4
      "zombie"    // ZOMBIE = 5
      };
  const char *state;
  printf("\n");
  for (auto p = processes; p < &processes[NPROC]; p++) {
    if (p->state == process::process_state::UNUSED)
      continue;
    const unsigned long i = (unsigned long) p->state;
    if (i >= 0 && i < NELEM(states) && states[i])
      state = states[i];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

