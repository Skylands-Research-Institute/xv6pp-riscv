#pragma once

#include "kernel_module.h"
#include "spin_lock.h"
#include "process.h"

class process_manager final : public kernel_module {
  friend class process;
  friend class process_scheduler;

private:
  spin_lock lock;
  spin_lock pid_lock;
  spin_lock wait_lock;
  int next_pid = 1;
  process processes[NPROC];
  process *initprocess = nullptr;

public:
  explicit process_manager(const char *name);
  void init() override;
  void dump();

  void mapstacks();
  void initial_process();

  int get_next_pid();

  struct process* alloc();
  void free(process*);

  pagetable_t create_pagetable(process *p);
  void free_pagetable(pagetable_t pagetable, uint64 sz);

  void reparent(process *p);
  int wait(uint64 addr);
  void wakeup(void *chan);
  int kill(int pid);

  static void forkret();
};


