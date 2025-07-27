#include "pipe.h"
#include "xv6pp.h"
#include "spin_lock.h"
#include "lock_guard.h"

pipe::pipe() {
  nread = 0;
  nwrite = 0;
  readopen = 0;
  writeopen = 0;
}

void pipe::close(bool writable) {
  lock_guard < spin_lock > g(lock);
  if (writable) {
    writeopen = 0;
    kernel.processes.wakeup(&nread);
  } else {
    readopen = 0;
    kernel.processes.wakeup(&nwrite);
  }
}

int pipe::read(uint64 addr, int n) {
  auto pr = kernel.cpus.curproc();
  lock_guard < spin_lock > g(lock);
  while (nread == nwrite && writeopen) {  //DOC: pipe-empty
    if (pr->get_killed()) {
      return -1;
    }
    pr->sleep(&nread, lock); //DOC: piperead-sleep
  }
  int i;
  for (i = 0; i < n; i++) {  //DOC: piperead-copy
    if (nread == nwrite)
      break;
    auto ch = data[nread++ % PIPESIZE];
    if (kernel.memory.copyout(pr->get_pagetable(), addr + i, &ch, 1) == -1)
      break;
  }
  kernel.processes.wakeup(&nwrite);  //DOC: piperead-wakeup
  return i;
}

int pipe::write(uint64 addr, int n) {
  process *pr = kernel.cpus.curproc();
  int i = 0;
  lock_guard < spin_lock > g(lock);
  while (i < n) {
    if (readopen == 0 || pr->get_killed()) {
      return -1;
    }
    if (nwrite == nread + PIPESIZE) { //DOC: pipewrite-full
      kernel.processes.wakeup(&nread);
      pr->sleep(&nwrite, lock);
    } else {
      char ch;
      if (kernel.memory.copyin(pr->get_pagetable(), &ch, addr + i, 1) == -1)
        break;
      data[nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  kernel.processes.wakeup(&nread);
  return i;
}

