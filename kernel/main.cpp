#include "xv6pp.h"

inline void*
operator new(unsigned long, void *ptr) noexcept {
  return ptr;
}

alignas(xv6pp) static char kernel_buf[sizeof(xv6pp)];
xv6pp &kernel = reinterpret_cast<xv6pp&>(kernel_buf);

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
extern "C" void main() {
  auto cpuid = cpu_manager::cpuid();
  if (!cpuid) {
    new (&kernel) xv6pp;
    // Install disk and console drivers.
    kernel.disk.set_driver(&kernel.disk_driver);
    kernel.console_device.set_driver(&kernel.console_driver);
    // Initialize printf.
    printfinit();
    // Initialize kernel modules.
    kernel.console_device.init();
    kernel.console.init();
    kernel.cpus.init();
    kernel.scheduler.init();
    kernel.allocator.init();
    kernel.memory.init();
    kernel.memory.inithart();
    kernel.processes.init();
    kernel.interrupts.init();
    kernel.interrupts.inithart();
    kernel.cache.init();
    kernel.fmanager.init();
    kernel.fsystem.init();
    kernel.disk_driver.init();
    kernel.disk.init();
    __sync_synchronize();
    started = 1;
  } else {
    while (started == 0)
      ;
    __sync_synchronize();
    // CPU-specific initialization.
    kernel.memory.inithart();
    kernel.interrupts.inithart();
  }
  // Run the process scheduler on each CPU forever...
  kernel.scheduler.run();
}

