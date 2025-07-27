#pragma once

#include "spin_lock.h"
#include "kernel_module.h"

#define INPUT_BUF_SIZE 128

class console_interface final : public kernel_module {
private:
  spin_lock lock;
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index

  static int consoleread(int user_dst, uint64 dst, int n);
  static int consolewrite(int user_src, uint64 src, int n);

public:
  explicit console_interface(const char *name);
  void init() override;
  void putc(int c);
  int read(int user_dst, uint64 dst, int n);
  int write(int user_src, uint64 src, int n);
  void interrupt(int c);
};


