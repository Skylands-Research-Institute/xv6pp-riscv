#pragma once

#include "file.h"

class [[nodiscard]] file_ref_guard {
private:
  file *f;

public:
  explicit file_ref_guard(file *f = nullptr) :
      f(f) {
  }

  ~file_ref_guard() {
    reset();
  }

  file *get() const {
    return f;
  }

  explicit operator bool() const {
    return f != nullptr;
  }

  file *release() {
    file *tmp = f;
    f = nullptr;
    return tmp;
  }

  void reset(file *new_f = nullptr) {
    if (f)
      f->close();
    f = new_f;
  }

  file_ref_guard(const file_ref_guard&) = delete;
  file_ref_guard& operator=(const file_ref_guard&) = delete;
};
