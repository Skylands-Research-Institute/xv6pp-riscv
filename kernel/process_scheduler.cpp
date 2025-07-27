#include "process_scheduler.h"
#include "xv6pp.h"
#include "lock_guard.h"

process_scheduler::process_scheduler(const char *name) :
    kernel_module(name) {
}

void process_scheduler::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void process_scheduler::run() const {
  cpu_state &cpu = kernel.cpus.cpu();
  cpu.set_process(nullptr);
  for (;;) {
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();
    bool found = false;
    for (auto p = kernel.processes.processes; p < &kernel.processes.processes[NPROC]; p++) {
      lock_guard<spin_lock> g(p->lock);
      if (p->state == process::process_state::RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = process::process_state::RUNNING;
        cpu.set_process(p);
        swtch(cpu.get_context(), &p->context);
        // Process is done running for now.
        // It should have changed its p->state before coming back.
        cpu.set_process(nullptr);
        found = true;
      }
    }
    if (!found) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Give up the CPU for one scheduling round.
void process_scheduler::yield(process *p) const {
  lock_guard<spin_lock> g(p->lock);
  p->state = process::process_state::RUNNABLE;
  sched(p);
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void process_scheduler::sched(process *p) const {
  if (!p->lock.holding())
    panic("sched p->lock");
  if (kernel.cpus.cpu().get_noff() != 1)
    panic("sched locks");
  if (p->state == process::process_state::RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");
  auto intena = kernel.cpus.cpu().get_intena();
  swtch(&p->context, kernel.cpus.cpu().get_context());
  kernel.cpus.cpu().set_intena(intena);
}

