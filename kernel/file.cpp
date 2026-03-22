#include "types.h"
#include "stat.h"

#include "file.h"
#include "xv6pp.h"
#include "spin_lock.h"
#include "inode_lock_guard.h"
#include "inode_ref_guard.h"
#include "lock_guard.h"
#include "log_op_guard.h"

file::file() {
  type = file_type::FD_NONE;
  ref = 0;
  readable = 0;
  writable = 0;
  pp = 0;
  ip = 0;
  off = 0;
  major = -1;
}

void file::close() {
  file ff(*this);
  {
    lock_guard < spin_lock > g(kernel.fmanager.file_lock);
    if (ref < 1)
      panic("fileclose");
    if (--ref > 0) {
      return;
    }
    ref = 0;
    type = FD_NONE;
  }
  if (ff.type == FD_PIPE) {
    ff.pp->close(ff.writable);
  } else if (ff.type == FD_INODE || ff.type == FD_DEVICE) {
    log_op_guard log_guard;
    inode_ref_guard ip_ref(ff.ip);
  }
}

void file::dup() {
  lock_guard < spin_lock > g(kernel.fmanager.file_lock);
  if (ref < 1)
    panic("filedup");
  ref++;
}

int file::read(uint64 addr, int n) {
  int r = 0;
  if (readable == 0)
    return -1;
  if (type == FD_PIPE) {
    r = pp->read(addr, n);
  } else if (type == FD_DEVICE) {
    if (major < 0 || major >= NDEV || !devsw[major].read)
      return -1;
    r = devsw[major].read(1, addr, n);
  } else if (type == FD_INODE) {
    inode_lock_guard ip_guard(ip);
    if ((r = kernel.fsystem.readi(ip, 1, addr, off, n)) > 0)
      off += r;
  } else {
    panic("fileread");
  }
  return r;
}

int file::stat(uint64 addr) {
  struct stat st;
  if (type == FD_INODE || type == FD_DEVICE) {
    inode_lock_guard ip_guard(ip);
    kernel.fsystem.stati(ip, &st);
    if (kernel.memory.copyout(kernel.cpus.curproc()->get_pagetable(), addr,
        (char*) &st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

int file::write(uint64 addr, int n) {
  int r, ret = 0;
  if (writable == 0)
    return -1;
  if (type == FD_PIPE) {
    ret = pp->write(addr, n);
  } else if (type == FD_DEVICE) {
    if (major < 0 || major >= NDEV || !devsw[major].write)
      return -1;
    ret = devsw[major].write(1, addr, n);
  } else if (type == FD_INODE) {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n) {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;
      log_op_guard log_guard;
      inode_lock_guard ip_guard(ip);
      if ((r = kernel.fsystem.writei(ip, 1, addr + i, off, n1)) > 0)
        off += r;
      if (r != n1) {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }
  return ret;
}
