#pragma once

#include "buffer_cache.h"
#include "xv6pp.h"

class buffer_guard {
private:
  buf *b;

public:
  buffer_guard(int dev, int blockno) :
      b(kernel.cache.read(dev, blockno)) {
  }

  ~buffer_guard() {
    if (b)
      kernel.cache.relse(b);
  }

  operator buf*() const {
    return b;
  }

  buf* operator->() const {
    return b;
  }

  buf& operator*() const {
    return *b;
  }

  buffer_guard(const buffer_guard&) = delete;
  buffer_guard& operator=(const buffer_guard&) = delete;
};

