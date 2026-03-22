#include "file_manager.h"
#include "file_ref_guard.h"
#include "lock_guard.h"

struct devsw devsw[NDEV];

file_manager::file_manager(const char *name) :
    kernel_module(name) {
}

void file_manager::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

file* file_manager::alloc_file() {
  lock_guard < spin_lock > g(file_lock);
  for (auto f = files; f < files + NFILE; f++) {
    if (f->ref == 0) {
      f->ref = 1;
      return f;
    }
  }
  return 0;
}

int file_manager::alloc_pipe(file **f0, file **f1) {
  *f0 = *f1 = nullptr;
  if ((*f0 = alloc_file()) != 0 && (*f1 = alloc_file()) != 0) {
    pipe *p;
    lock_guard < spin_lock > g(pipe_lock);
    for (p = pipes; p < pipes + NFILE / 2; p++) {
      if (!p->readopen && !p->writeopen)
        break;
    }
    p->readopen = 1;
    p->writeopen = 1;
    p->nwrite = 0;
    p->nread = 0;
    (*f0)->type = file::FD_PIPE;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pp = p;
    (*f1)->type = file::FD_PIPE;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pp = p;
    return 0;
  }
  file_ref_guard f0_ref(*f0);
  file_ref_guard f1_ref(*f1);
  return -1;
}
