#pragma once

#include "kernel_module.h"

struct buf;

class block_device_driver: public kernel_module {
protected:
  explicit block_device_driver(const char *name) :
      kernel_module(name) {
  }

public:
  virtual void read(buf *b) = 0;
  virtual void write(buf *b) = 0;
  virtual void handle_interrupt() = 0;
};

class block_device final: public kernel_module {
private:
  block_device_driver *driver = nullptr;
  const char *name = nullptr;

public:
  explicit block_device(const char *name) :
      kernel_module(name) {
  }

  void set_driver(block_device_driver *d) {
    driver = d;
  }

  void init() override {
    log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
  }

  void read(buf *b) {
    driver->read(b);
  }

  void write(buf *b) {
    driver->write(b);
  }

  void handle_interrupt() {
    driver->handle_interrupt();
  }
};

