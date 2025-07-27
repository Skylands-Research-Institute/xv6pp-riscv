#include "file_system_log.h"
#include "buffer_guard.h"
#include "lock_guard.h"
#include "xv6pp.h"

file_system_log::file_system_log(const char *name) :
    kernel_module(name), lock(name) {
}

void file_system_log::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

void file_system_log::init(int dev, struct superblock *sb) {
  start = sb->logstart;
  size = sb->nlog;
  this->dev = dev;
  recover_from_log();
}

void file_system_log::recover_from_log() {
  read_head();
  install_trans(1); // if committed, copy from log to disk
  header.n = 0;
  write_head(); // clear the log
}

void file_system_log::commit() {
  if (header.n > 0) {
    write_log();     // Write modified blocks from cache to log
    write_head();    // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    header.n = 0;
    write_head();    // Erase the transaction from the log
  }
}

void file_system_log::write_log() {
  for (int tail = 0; tail < header.n; tail++) {
    buffer_guard to(dev, start + tail + 1);
    buffer_guard from(dev, header.block[tail]);
    memmove(to->data, from->data, BSIZE);
    kernel.cache.write(to);
  }
}

void file_system_log::write_head() {
  buffer_guard buf(dev, start);
  log_header *hb = (log_header*) buf->data;
  hb->n = header.n;
  for (int i = 0; i < header.n; i++) {
    hb->block[i] = header.block[i];
  }
  kernel.cache.write(buf);
}

void file_system_log::install_trans(bool recovering) {
  for (int tail = 0; tail < header.n; tail++) {
    buffer_guard lbuf(dev, start + tail + 1); // read log block
    buffer_guard dbuf(dev, header.block[tail]); // read dst
    memmove(dbuf->data, lbuf->data, BSIZE);  // copy block to dst
    kernel.cache.write(dbuf);  // write dst to disk
    if (!recovering)
      kernel.cache.unpin(dbuf);
  }
}

void file_system_log::read_head() {
  buffer_guard buf(dev, start);
  log_header *lh = (log_header*) buf->data;
  header.n = lh->n;
  for (int i = 0; i < header.n; i++) {
    header.block[i] = lh->block[i];
  }
}

void file_system_log::log_write(buf *b) {
  lock_guard<spin_lock> g(lock);
  if (header.n >= LOGSIZE || header.n >= size - 1)
    panic("too big a transaction");
  if (outstanding < 1)
    panic("log_write outside of trans");
  int i;
  for (i = 0; i < header.n; i++) {
    if (header.block[i] == (int) b->blockno)   // log absorption
      break;
  }
  header.block[i] = b->blockno;
  if (i == header.n) {  // Add new block to log?
    kernel.cache.pin(b);
    header.n++;
  }
}

void file_system_log::begin_op() {
  lock_guard<spin_lock> g(lock);
  while (true) {
    if (committing) {
      kernel.interrupts.sleep(this, lock);
    } else if (header.n + (outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
      // this op might exhaust log space; wait for commit.
      kernel.interrupts.sleep(this, lock);
    } else {
      outstanding += 1;
      break;
    }
  }
}

void file_system_log::end_op() {
  bool do_commit = false;
  {
    lock_guard<spin_lock> g(lock);
    outstanding -= 1;
    if (committing)
      panic("committing");
    if (outstanding == 0) {
      do_commit = true;
      committing = 1;
    } else {
      // begin_op() may be waiting for log space,
      // and decrementing log.outstanding has decreased
      // the amount of reserved space.
      kernel.processes.wakeup(this);
    }
  }
  if (do_commit) {
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    lock_guard<spin_lock> g(lock);
    committing = false;
    kernel.processes.wakeup(this);
  }
}

