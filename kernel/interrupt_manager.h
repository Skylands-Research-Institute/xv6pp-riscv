#pragma once

#include "kernel_module.h"
#include "spin_lock.h"

class interrupt_manager final : public kernel_module {
private:
  spin_lock ticks_lock;
  uint ticks;

public:
  explicit interrupt_manager(const char *name);
  void init() override;
  void inithart();
  void prepare_return();
  void clock_tick();
  int sleep(uint ticks);
  void sleep(void *chan, spin_lock &lk);
  uint get_ticks() const {
    return ticks;
  }

  void kernel_interrupt();
  uint64 user_interrupt();
  int device_interrupt();

  void intr_on() const;
  void intr_off() const;
  int intr_get() const;

  int plic_claim(void);
  void plic_complete(int irq);
};

