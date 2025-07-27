#pragma once

#include "memlayout.h"
#include "types.h"
#include "riscv.h"
#include "page_allocator.h"
#include "buffer_cache.h"
#include "char_device.h"
#include "uart16550_driver.h"
#include "console_interface.h"
#include "process_scheduler.h"
#include "virtual_memory.h"
#include "virtio_driver.h"
#include "block_device.h"
#include "process_manager.h"
#include "interrupt_manager.h"
#include "cpu_manager.h"
#include "file_manager.h"
#include "file_system.h"
#include "file_system_log.h"

class xv6pp final {
public:
  xv6pp();
  cpu_manager cpus;
  page_allocator allocator;
  buffer_cache cache;
  char_device console_device;
  uart16550_driver console_driver;
  virtual_memory memory;
  console_interface console;
  virtio_driver disk_driver;
  block_device disk;
  process_scheduler scheduler;
  process_manager processes;
  interrupt_manager interrupts;
  file_manager fmanager;
  file_system fsystem;
  file_system_log log;
};

extern xv6pp &kernel;


