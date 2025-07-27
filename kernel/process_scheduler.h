#pragma once

#include "kernel_module.h"

class process;

class process_scheduler final : public kernel_module {
public:
  explicit process_scheduler(const char *name);
  void init() override;
  [[noreturn]] void run() const;
  void yield(process *p) const;
  void sched(process *p) const;
};

