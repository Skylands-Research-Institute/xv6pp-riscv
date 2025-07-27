#pragma once

#include "lock_base.h"
#include "spin_lock.h"

class sleep_lock: public lock_base {
private:
  mutable spin_lock lk;
  int pid;

public:
  explicit sleep_lock(const char *name = "");

  void acquire();
  void release();
  bool holding() const;

  sleep_lock(const sleep_lock&) = delete;
  sleep_lock& operator=(const sleep_lock&) = delete;
};

