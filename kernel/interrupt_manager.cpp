#include "interrupt_manager.h"
#include "lock_guard.h"
#include "sleep_lock.h"
#include "xv6pp.h"
#include "flag_guard.h"

extern "C" {
// in kernelvec.S, calls kerneltrap().
void kernelvec();

void kerneltrap() {
  kernel.interrupts.kernel_interrupt();
}

uint64 usertrap() {
  return kernel.interrupts.user_interrupt();
}
}

extern char trampoline[], uservec[], userret[];

void syscall();

interrupt_manager::interrupt_manager(const char *name) :
    kernel_module(name), ticks_lock(name) {
}

void interrupt_manager::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*) (PLIC + UART0_IRQ * 4) = 1;
  *(uint32*) (PLIC + VIRTIO0_IRQ * 4) = 1;
}

void interrupt_manager::inithart() {
  int hart = kernel.cpus.cpuid();
  log(log_level::INFO, "inithart, hart=%d\n", hart);
  w_stvec((uint64) kernelvec);
  // set enable bits for this hart's S-mode
  // for the uart and virtio disk.
  *(uint32*) PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);
  // set this hart's S-mode priority threshold to 0.
  *(uint32*) PLIC_SPRIORITY(hart) = 0;
}

void interrupt_manager::prepare_return() {
  process *p = kernel.cpus.curproc();
  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();
  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);
  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64) usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()
  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);
  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}

void interrupt_manager::clock_tick() {
  if (!kernel.cpus.cpuid()) {
    lock_guard<spin_lock> g(ticks_lock);
    ticks++;
    kernel.processes.wakeup(&ticks);
  }
  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

int interrupt_manager::sleep(uint n) {
  lock_guard<spin_lock> g(ticks_lock);
  auto ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (kernel.cpus.curproc()->get_killed()) {
      return -1;
    }
    sleep(&ticks, ticks_lock);
  }
  return 0;
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void interrupt_manager::sleep(void *chan, spin_lock &lk) {
  process *p = kernel.cpus.curproc();
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  p->lock.acquire();  //DOC: sleeplock1
  lk.release();
  // Go to sleep.
  p->chan = chan;
  p->state = process::process_state::SLEEPING;
  kernel.scheduler.sched(p);
  // Tidy up.
  p->chan = 0;
  // Reacquire original lock.
  p->lock.release();
  lk.acquire();
}

void interrupt_manager::kernel_interrupt() {
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  {
    // Invariant: Only one kerneltrap should execute at a time on a given CPU.
    // Use flag_guard to detect reentrant traps or concurrent execution on the same CPU.
    flag_guard g(kernel.cpus.cpu().hkintr);
    if ((sstatus & SSTATUS_SPP) == 0)
      panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
      panic("kerneltrap: interrupts enabled");
    if ((which_dev = device_interrupt()) == 0) {
      // interrupt or trap from an unknown source
      log(log_level::ERROR, "scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
      panic("kerneltrap");
    }
  }
  // Important: `flag_guard` must be destructed *before* calling `yield()`.
  // `yield()` may cause a context switch, during which a timer interrupt
  // can fire and re-enter `kerneltrap()` on the same CPU. If the guard
  // is still active (i.e., the flag is set), the new `kerneltrap()` will
  // panic due to a violation of the single-entry invariant.
  //
  // To avoid false positives and ensure correctness, always restrict the
  // lifetime of `flag_guard` to a scope that ends before any operation
  // that might yield the CPU or re-enable interrupts.
  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && kernel.cpus.curproc())
    kernel.scheduler.yield(kernel.cpus.curproc());
  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

uint64 interrupt_manager::user_interrupt() {
  int which_dev = 0;
  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64) kernelvec);
  process *p = kernel.cpus.curproc();
  // save user program counter.
  p->trapframe->epc = r_sepc();
  if (r_scause() == 8) {
    // system call
    if (p->get_killed())
      p->exit(-1);
    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;
    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();
    syscall();
  } else if ((which_dev = device_interrupt()) != 0) {
    // ok
  } else {
    log(log_level::WARN, "usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    log(log_level::WARN, "            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    p->set_killed(true);
  }
  if (p->get_killed())
    p->exit(-1);
  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2)
    kernel.scheduler.yield(p);
  prepare_return();
  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);
  // return to trampoline.S; satp value in a0.
  return satp;
}

int interrupt_manager::device_interrupt() {
  uint64 scause = r_scause();
  if (scause == 0x8000000000000009L) {
    // this is a supervisor external interrupt, via PLIC.
    // irq indicates which device interrupted.
    int irq = plic_claim();
    if (irq == UART0_IRQ) {
      kernel.console_device.handle_interrupt();
    } else if (irq == VIRTIO0_IRQ) {
      kernel.disk.handle_interrupt();
    } else if (irq) {
      log(log_level::WARN, "unexpected interrupt irq=%d\n", irq);
    }
    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);
    return 1;
  } else if (scause == 0x8000000000000005L) {
    // timer interrupt.
    kernel.interrupts.clock_tick();
    return 2;
  } else {
    return 0;
  }
}

// ask the PLIC what interrupt we should serve.
int interrupt_manager::plic_claim() {
  int hart = kernel.cpus.cpuid();
  int irq = *(uint32*) PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void interrupt_manager::plic_complete(int irq) {
  int hart = kernel.cpus.cpuid();
  *(uint32*) PLIC_SCLAIM(hart) = irq;
}

void interrupt_manager::intr_on() const {
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// disable device interrupts
void interrupt_manager::intr_off() const {
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// are device interrupts enabled?
int interrupt_manager::intr_get() const {
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

