#include "spin_lock.h"
#include "xv6pp.h"

spin_lock::spin_lock(const char *n) :
    lock_base(n) {
}

void spin_lock::acquire() {
  kernel.cpus.push_off();
  if (holding()) {
    panic("acquire");
  }

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while (__sync_lock_test_and_set(&locked, 1) != 0)
    ;

  // Tell the C compiler and the processor to not move loads or stores
  // past this point, to ensure that the critical section's memory
  // references happen strictly after the lock is acquired.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  cpuid = kernel.cpus.cpuid();
}

void spin_lock::release() {
  if (!holding())
    panic("release");
  cpuid = -1;

  // Tell the C compiler and the CPU to not move loads or stores
  // past this point, to ensure that all the stores in the critical
  // section are visible to other CPUs before the lock is released,
  // and that loads in the critical section occur strictly before
  // the lock is released.
  // On RISC-V, this emits a fence instruction.
  __sync_synchronize();

  // Release the lock, equivalent to lk->locked = 0.
  // This code doesn't use a C assignment, since the C standard
  // implies that an assignment might be implemented with
  // multiple store instructions.
  // On RISC-V, sync_lock_release turns into an atomic swap:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&locked);

  kernel.cpus.pop_off();
}

bool spin_lock::holding() const {
  //return ::holding(&lock);
  if ((uint64) this < 0x80000000 || (uint64) this > 0x88000000)
    panic("spin_lock: invalid this");
  return locked && cpuid == kernel.cpus.cpuid();
}

