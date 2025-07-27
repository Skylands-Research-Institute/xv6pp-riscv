#pragma once

#include "types.h"
#include "riscv.h"
#include "param.h"
#include "fs.h"
#include "spin_lock.h"

class file;

class process final {
  friend class process_manager;
  friend class process_scheduler;
  friend class interrupt_manager;

private:
  enum class process_state {
    UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE
  };

  spin_lock lock;

  // lock must be held when using these:
  process_state state = process_state::UNUSED;
  void *chan;   // If non-zero, sleeping on chan
  bool killed;  // If non-zero, have been killed
  int xstate;   // Exit status to be returned to parent's wait
  int pid;      // Process ID

  // wait_lock must be held when using this:
  process *parent;  // Parent process

  // these are private to the process, so lock need not be held.
  uint64 kstack;                // Virtual address of kernel stack
  uint64 sz;                    // Size of process memory (bytes)
  pagetable_t pagetable;        // User page table
  struct trapframe *trapframe;  // data page for trampoline.S
  struct context context;       // swtch() here to run process
  file *ofile[NOFILE];          // Open files
  struct inode *cwd;            // Current directory
  char name[16];                // Process name (debugging)

  static int loadseg(pagetable_t pt, uint64 va, struct inode *ip, uint offset, uint sz);

public:
  process();
  void free();
  int grow(int n);
  int fork();
  int exec(char *path, char **argv);
  void exit(int status);
  void sleep(void *chan, spin_lock &lk);
  bool get_killed();
  void set_killed(bool killed = true);
  int get_pid() const {
    return pid;
  }
  uint64 get_sz() const {
    return sz;
  }
  pagetable_t get_pagetable() const {
    return pagetable;
  }
  file* get_ofile(int fd) {
    return ofile[fd];
  }
  void set_ofile(int fd, file *f) {
    ofile[fd] = f;
  }
  struct inode* get_cwd() const {
    return cwd;
  }
  void set_cwd(struct inode *ip) {
    cwd = ip;
  }
  struct trapframe* get_trapframe() const {
    return trapframe;
  }
  const char* get_name() const {
    return name;
  }
};

