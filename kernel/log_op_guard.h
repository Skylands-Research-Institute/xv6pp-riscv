#pragma once

#include "xv6pp.h"

class [[nodiscard]] log_op_guard {
public:
  log_op_guard() {
    kernel.log.begin_op();
  }
  ~log_op_guard() {
    kernel.log.end_op();
  }

  log_op_guard(const log_op_guard&) = delete;
  log_op_guard& operator=(const log_op_guard&) = delete;
};
