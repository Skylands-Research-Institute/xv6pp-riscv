#include "xv6pp.h"
#include "cpu_manager.h"

cpu_manager::cpu_manager(const char *name) :
    kernel_module(name) {
  for (int i = 0; i < NCPU; i++) {
    cpu_state &cpu = cpus[i];
    memset(&cpu, 0, sizeof(cpu));
    cpu.cpuid = i;
    cpu.hkintr = false;
    cpu.intena = false;
    cpu.noff = 0;
    cpu.curproc = nullptr;
  }
}

void cpu_manager::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

process* cpu_manager::curproc() {
  push_off();
  cpu_state &c = cpu();
  process *p = c.curproc;
  pop_off();
  return p;
}

// Check whether this cpu is holding the lock.
// Interrupts must be off.
bool cpu_manager::holding(spin_lock &lock) {
  return lock.is_locked() && lock.get_cpuid() == cpuid();
}

// push_off/pop_off are like intr_off()/intr_on() except that they are matched:
// it takes two pop_off()s to undo two push_off()s.  Also, if interrupts
// are initially off, then push_off, pop_off leaves them off.
void cpu_manager::push_off() {
  bool old = kernel.interrupts.intr_get();
  kernel.interrupts.intr_off();
  cpu_state &c = cpu();
  if (!c.noff)
    c.intena = old;
  c.noff++;
}

void cpu_manager::pop_off(void) {
  cpu_state &c = cpu();
  if (kernel.interrupts.intr_get())
    panic("pop_off - interruptible");
  if (c.noff < 1)
    panic("pop_off");
  c.noff--;
  if (!c.noff && c.intena)
    kernel.interrupts.intr_on();
}

