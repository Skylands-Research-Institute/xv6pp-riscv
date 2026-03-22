#pragma once

#include "param.h"
#include "spin_lock.h"
#include "kernel_module.h"

struct buf;
struct superblock;
class log_op_guard;

class file_system_log final : public kernel_module {
  friend class log_op_guard;

private:
  spin_lock lock;
  int start;
  int size;
  int outstanding;
  bool committing;
  int dev;
  struct log_header {
    int n;
    int block[LOGSIZE];
  } header;

  void recover_from_log();
  void commit();
  void write_log();
  void write_head();
  void install_trans(bool recovering);
  void read_head();
  void begin_op();
  void end_op();

public:
  explicit file_system_log(const char *name);
  void init() override;
  void init(int dev, struct superblock *sb);
  void log_write(buf *b);
};
