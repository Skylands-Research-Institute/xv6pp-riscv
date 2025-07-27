#pragma once

#include "types.h"
#include "file_system.h"

#define PIPESIZE 512

class pipe final {
  friend class file_manager;

private:
  spin_lock lock;
  char data[PIPESIZE];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open

public:
  pipe();
  void close(bool writable);
  void dup();
  int read(uint64, int n);
  int stat(uint64 addr);
  int write(uint64, int n);
};

