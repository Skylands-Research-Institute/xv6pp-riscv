#pragma once

#include "kernel_module.h"

class char_device_driver: public kernel_module {
protected:
  char_device_driver(const char *name) :
      kernel_module(name) {
  }

public:
  virtual void putc(int c) = 0;
  virtual void putc_sync(int c) = 0;
  virtual int getc() = 0;
  virtual void handle_interrupt() = 0;
};

class char_device final: public kernel_module {
private:
  char_device_driver *driver = nullptr;

public:
  explicit char_device(const char *name) :
      kernel_module(name) {
  }

  void set_driver(char_device_driver *d) {
    driver = d;
  }

  void init() override {
    log(log_level::INFO, "init, size=%ld\n", sizeof(*this));
  }

  void putc(int c) {
    driver->putc(c);
  }

  void putc_sync(int c) {
    driver->putc_sync(c);
  }

  int getc() {
    return driver->getc();
  }

  void handle_interrupt() {
    driver->handle_interrupt();
  }

};

