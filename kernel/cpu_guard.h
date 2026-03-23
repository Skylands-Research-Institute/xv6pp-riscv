#pragma once

#include "xv6pp.h"

class cpu_guard {
public:
  cpu_guard() {
    kernel.cpus.push_off();
  }

  ~cpu_guard() {
    kernel.cpus.pop_off();
  }

  cpu_guard(const cpu_guard&) = delete;
  cpu_guard& operator=(const cpu_guard&) = delete;
};
