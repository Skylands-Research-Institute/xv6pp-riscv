#pragma once

#include "intrusive_slist.h"
#include "spin_lock.h"
#include "kernel_module.h"

class page_allocator final : public kernel_module {
private:
  class page {
  public:
    page *next;
  };
  spin_lock lock;
  intrusive_slist<page> free_list;

public:
  explicit page_allocator(const char *name);
  void init() override;
  void* alloc();
  void free(void *pa);
  uint64 get_free_count();
};
