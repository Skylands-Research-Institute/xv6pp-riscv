#pragma once

class pipe;

class file final {
  friend class file_manager;

public:
  enum file_type {
    FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE
  };

private:
  file_type type;
  uint ref;            // reference count
  bool readable;
  bool writable;
  pipe *pp;           // FD_PIPE
  struct inode *ip;   // FD_INODE and FD_DEVICE
  uint off;           // FD_INODE
  short major;        // FD_DEVICE

public:
  file();
  void close();
  void dup();
  int read(uint64, int n);
  int stat(uint64 addr);
  int write(uint64, int n);

  void set_type(file_type t) {
    type = t;
  }

  void set_major(short m) {
    major = m;
  }

  void set_ip(struct inode *p) {
    ip = p;
  }

  void set_readable(bool r) {
    readable = r;
  }

  void set_writable(bool w) {
    writable = w;
  }

  void set_off(uint o) {
    off = o;
  }
};


