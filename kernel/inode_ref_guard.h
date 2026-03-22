#pragma once

#include "xv6pp.h"

class [[nodiscard]] inode_ref_guard {
private:
  inode *ip;

public:
  explicit inode_ref_guard(inode *ip = nullptr) :
      ip(ip) {
  }

  ~inode_ref_guard() {
    reset();
  }

  inode *get() const {
    return ip;
  }

  explicit operator bool() const {
    return ip != nullptr;
  }

  inode *release() {
    inode *tmp = ip;
    ip = nullptr;
    return tmp;
  }

  void reset(inode *new_ip = nullptr) {
    if (ip)
      kernel.fsystem.iput(ip);
    ip = new_ip;
  }

  inode_ref_guard(const inode_ref_guard&) = delete;
  inode_ref_guard& operator=(const inode_ref_guard&) = delete;
};
