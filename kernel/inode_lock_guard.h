#pragma once

#include "xv6pp.h"

struct adopt_inode_lock_t {
  explicit adopt_inode_lock_t() = default;
};

inline constexpr adopt_inode_lock_t adopt_inode_lock{};

class [[nodiscard]] inode_lock_guard {
private:
  inode *ip;
  bool locked;

public:
  explicit inode_lock_guard(inode *ip = nullptr) :
      ip(ip), locked(ip != nullptr) {
    if (locked)
      kernel.fsystem.ilock(ip);
  }

  inode_lock_guard(inode *ip, adopt_inode_lock_t) :
      ip(ip), locked(ip != nullptr) {
  }

  ~inode_lock_guard() {
    unlock();
  }

  void unlock() {
    if (locked) {
      kernel.fsystem.iunlock(ip);
      locked = false;
    }
  }

  void reset(inode *new_ip = nullptr) {
    unlock();
    ip = new_ip;
    locked = ip != nullptr;
    if (locked)
      kernel.fsystem.ilock(ip);
  }

  void adopt(inode *new_ip = nullptr) {
    unlock();
    ip = new_ip;
    locked = ip != nullptr;
  }

  void release() {
    ip = nullptr;
    locked = false;
  }

  inode *get() const {
    return ip;
  }

  explicit operator bool() const {
    return ip != nullptr;
  }

  inode_lock_guard(const inode_lock_guard&) = delete;
  inode_lock_guard& operator=(const inode_lock_guard&) = delete;
};
