#pragma once

class flag_guard {
  bool &flag;

public:
  explicit flag_guard(bool &f) :
      flag(f) {
    bool expected = false;
    if (!__atomic_compare_exchange_n(&flag, &expected, true, /*weak=*/false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
      panic("flag_guard: flag is already set!");
    }
  }

  ~flag_guard() {
    bool expected = true;
    if (!__atomic_compare_exchange_n(&flag, &expected, false, /*weak=*/false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
      panic("flag_guard: flag was already cleared!");
    }
  }

  // Non-copyable, non-movable
  flag_guard(const flag_guard&) = delete;
  flag_guard& operator=(const flag_guard&) = delete;
};
