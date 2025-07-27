#pragma once

class lock_base {
protected:
  volatile int locked = 0;
  const char *name = nullptr;

  explicit lock_base(const char *name) :
      name(name) {
  }

public:
  [[nodiscard]] int is_locked() const {
    return locked;
  }
  const char* get_name() const {
    return name;
  }

  lock_base(const lock_base&) = delete;
  lock_base& operator=(const lock_base&) = delete;
};


