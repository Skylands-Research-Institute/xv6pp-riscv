#pragma once

#include "kernel_module.h"

class virtual_memory final : public kernel_module {
private:
  pagetable_t kernel_pagetable = nullptr;

  void freewalk(pagetable_t);

public:
  explicit virtual_memory(const char *name);
  void init() override;
  void inithart();
  void map(pagetable_t, uint64, uint64, uint64, int);
  int mappages(pagetable_t, uint64, uint64, uint64, int);
  pagetable_t create(void);
  void first(pagetable_t, uchar*, uint);
  uint64 alloc(pagetable_t, uint64, uint64, int);
  uint64 dealloc(pagetable_t, uint64, uint64);
  void unmap(pagetable_t, uint64, uint64, int);
  pte_t* walk(pagetable_t, uint64, int);
  int copy(pagetable_t, pagetable_t, uint64);
  void free(pagetable_t, uint64);
  void clear(pagetable_t, uint64);
  uint64 walkaddr(pagetable_t, uint64);
  int copyout(pagetable_t, uint64, char*, uint64);
  int copyin(pagetable_t, char*, uint64, uint64);
  int copyinstr(pagetable_t, char*, uint64, uint64);
  int either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
  int either_copyin(void *dst, int user_src, uint64 src, uint64 len);

  pagetable_t get_kernel_pagetable() const {
    return kernel_pagetable;
  }
};


