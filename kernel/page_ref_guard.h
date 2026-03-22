#pragma once

#include "xv6pp.h"

class [[nodiscard]] page_ref_guard {
private:
  void *page;

public:
  explicit page_ref_guard(void *page = nullptr) :
      page(page) {
  }

  ~page_ref_guard() {
    reset();
  }

  void *get() const {
    return page;
  }

  explicit operator bool() const {
    return page != nullptr;
  }

  void *release() {
    void *tmp = page;
    page = nullptr;
    return tmp;
  }

  void reset(void *new_page = nullptr) {
    if (page)
      kernel.allocator.free(page);
    page = new_page;
  }

  page_ref_guard(const page_ref_guard&) = delete;
  page_ref_guard& operator=(const page_ref_guard&) = delete;
};
