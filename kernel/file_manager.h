#pragma once

#include "kernel_module.h"
#include "spin_lock.h"
#include "file.h"
#include "pipe.h"

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1

class file_manager final : public kernel_module {
  friend class file;
  friend class pipe;

private:
  spin_lock file_lock;
  file files[NFILE];
  spin_lock pipe_lock;
  pipe pipes[NFILE / 2]; // each pipe consumes 2 files

public:
  explicit file_manager(const char *name);
  void init() override;
  file* alloc_file();
  int alloc_pipe(file **f1, file **f2);
};

