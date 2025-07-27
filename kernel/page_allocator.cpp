#include "memlayout.h"
#include "types.h"
#include "riscv.h"
#include "page_allocator.h"
#include "lock_guard.h"

extern char end[];  // first address after kernel, defined by kernel.ld

page_allocator::page_allocator(const char *name) :
    kernel_module(name), lock(name) {
  char *p = (char*) PGROUNDUP((uint64 )end);
  while (p + PGSIZE <= (char*) PHYSTOP) {
    free(p);
    p += PGSIZE;
  }
}

void page_allocator::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

uint64 page_allocator::get_free_count() {
  lock_guard<spin_lock> g(lock);
  return free_list.get_count();
}

void* page_allocator::alloc() {
  lock_guard<spin_lock> g(lock);
  return free_list.pop();
}

void page_allocator::free(void *p) {
  if ((uint64) p % PGSIZE != 0)
    panic("free: unaligned page");
  lock_guard<spin_lock> g(lock);
  free_list.push((page*) p);
}

