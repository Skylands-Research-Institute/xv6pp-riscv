#include "console_interface.h"
#include "lock_guard.h"
#include "xv6pp.h"

#define BACKSPACE 0x100

static inline constexpr char C(char c) {
  return (char) (c - '@');
}

int console_interface::consoleread(int user_dst, uint64 dst, int n) {
  return kernel.console.read(user_dst, dst, n);
}

int console_interface::consolewrite(int user_src, uint64 src, int n) {
  return kernel.console.write(user_src, src, n);
}

console_interface::console_interface(const char *name) :
    kernel_module(name) {
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}

void console_interface::init() {
  log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
}

void console_interface::putc(int c) {
  if (c == BACKSPACE) {
    // if the user typed backspace, overwrite with a space.
    kernel.console_device.putc_sync('\b');
    kernel.console_device.putc_sync(' ');
    kernel.console_device.putc_sync('\b');
  } else {
    kernel.console_device.putc_sync(c);
  }
}

int console_interface::read(int user_dst, uint64 dst, int n) {
  lock_guard<spin_lock> g(lock);
  auto target = n;
  while (n > 0) {
    // wait until interrupt handler has put some
    // input into cons.buffer.
    while (r == w) {
      if (kernel.cpus.curproc()->get_killed())
        return -1;
      kernel.interrupts.sleep(&r, lock);
    }
    auto c = buf[r++ % INPUT_BUF_SIZE];
    if (c == C('D')) {  // end-of-file
      if (n < target) {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        r--;
      }
      break;
    }
    // copy the input byte to the user-space buffer.
    auto cbuf = c;
    if (kernel.memory.either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;
    dst++;
    --n;
    if (c == '\n') {
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  return target - n;
}

int console_interface::write(int user_src, uint64 src, int n) {
  int i;
  for (i = 0; i < n; i++) {
    char c;
    if (kernel.memory.either_copyin(&c, user_src, src + i, 1) == -1)
      break;
    kernel.console_device.putc(c);
  }
  return i;
}

void console_interface::interrupt(int c) {
  lock_guard<spin_lock> g(lock);
  switch (c) {
  case C('P'):  // Print process list.
    kernel.processes.dump();
    break;
  case C('U'):  // Kill line.
    while (e != w && buf[(e - 1) % INPUT_BUF_SIZE] != '\n') {
      e--;
      kernel.console.putc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    if (e != w) {
      e--;
      kernel.console.putc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && e - r < INPUT_BUF_SIZE) {
      c = (c == '\r') ? '\n' : c;
      // echo back to the user.
      kernel.console.putc(c);
      // store for consumption by consoleread().
      buf[e++ % INPUT_BUF_SIZE] = c;
      if (c == '\n' || c == C('D') || e - r == INPUT_BUF_SIZE) {
        // wake up consoleread() if a whole line (or end-of-file)
        // has arrived.
        w = e;
        kernel.processes.wakeup(&r);
      }
    }
    break;
  }
}

