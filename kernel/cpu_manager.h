#pragma once

#include "kernel_module.h"
#include "cpu_state.h"

class cpu_manager final : public kernel_module {
private:
  cpu_state cpus[NCPU];

public:
  explicit cpu_manager(const char *name);
  void init() override;

  // Must be called with interrupts disabled,
  // to prevent race with process being moved
  // to a different CPU.
  static int cpuid() {
    return r_tp();
  }

  // Return this CPU's cpu struct.
  // Interrupts must be disabled.
  cpu_state& cpu() {
    return cpus[cpuid()];
  }

  static bool boot_cpu() {
    return cpuid() == 0;
  }

  process* curproc();

  bool holding(spin_lock &lk);

  void push_off();
  void pop_off();
};

