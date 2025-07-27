#include "sleep_lock.h"
#include "lock_guard.h"
#include "xv6pp.h"

sleep_lock::sleep_lock(const char *name) :
    lock_base(name), lk(name) {
  pid = 0;
}

void sleep_lock::acquire() {
  lock_guard<spin_lock> g(lk);
  while (locked)
    kernel.interrupts.sleep(&lk, lk);
  locked = true;
  pid = kernel.cpus.curproc()->get_pid();
}

void sleep_lock::release() {
  lock_guard<spin_lock> g(lk);
  locked = false;
  pid = 0;
  kernel.processes.wakeup(&lk);
}

bool sleep_lock::holding() const {
  lock_guard<spin_lock> g(lk);
  return locked && (pid == kernel.cpus.curproc()->get_pid());
}

