#pragma once

#include "lock_base.h"

class spin_lock: public lock_base {
private:
  int cpuid = -1;

public:
  explicit spin_lock(const char *name = "");

  void acquire();
  void release();
  bool holding() const;

  int get_cpuid() const {
    return cpuid;
  }

  spin_lock(const spin_lock&) = delete;
  spin_lock& operator=(const spin_lock&) = delete;
};

