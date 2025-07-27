#pragma once

// Primary template assumes T has `.acquire()` and `.release()` methods
template<typename T, typename Enable = void>
class [[nodiscard]] lock_guard {
private:
  T &l;

public:
  explicit lock_guard(T &l) :
      l(l) {
    l.acquire();
  }

  ~lock_guard() {
    l.release();
  }

  lock_guard(const lock_guard&) = delete;
  lock_guard& operator=(const lock_guard&) = delete;
};
