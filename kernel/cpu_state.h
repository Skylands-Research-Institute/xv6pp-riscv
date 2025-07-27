#pragma once

class process;
class spin_lock;

class cpu_state final {
  friend class interrupt_manager;
  friend class cpu_manager;

private:
  int cpuid = 0;
  process *curproc = nullptr;
  struct context context;     // swtch() here to enter scheduler().
  int noff = 0;               // Depth of push_off() nesting.
  bool intena = false;        // Were interrupts enabled before push_off()?
  bool hkintr = false;        // is cpu handling a kernel interrupt?

public:
  cpu_state() {
  }

  int get_cpuid() const {
    return cpuid;
  }

  void set_process(process *p) {
    curproc = p;
  }

  process* get_process() const {
    return curproc;
  }

  struct context* get_context() {
    return &context;
  }

  int get_noff() const {
    return noff;
  }

  void set_noff(int n) {
    noff = n;
  }

  bool get_intena() const {
    return intena;
  }

  void set_intena(bool i) {
    intena = i;
  }
};

