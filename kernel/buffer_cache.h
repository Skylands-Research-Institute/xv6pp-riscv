#pragma once

#include "intrusive_dlist.h"
#include "sleep_lock.h"
#include "kernel_module.h"
#include "file_system.h"
#include "fs.h"
#include "param.h"

struct buf {
  int valid;   // has data been read from disk?
  int disk;    // does disk "own" buf?
  uint dev;
  uint blockno;
  sleep_lock lock;
  uint refcnt;
  struct buf *prev; // LRU cache list
  struct buf *next;
  uchar data[BSIZE];
};

class buffer_cache final : public kernel_module {
private:
  spin_lock lock;
  intrusive_dlist<buf> cache;
  buf storage[NBUF];

public:
  explicit buffer_cache(const char *name);
  void init() override;

private:
  friend class buffer_guard;
  friend class file_system;
  friend class file_system_log;

  buf* read(uint device, uint block);
  void relse(buf *b);
  void write(buf *b);
  void pin(buf *b);
  void unpin(buf *b);
};

