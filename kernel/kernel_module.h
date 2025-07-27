#pragma once

#include "types.h"
#include "defs.h"

class kernel_module {
public:
  enum class log_level {
    NONE, ERROR, WARN, INFO, DEBUG
  };

  virtual void init() = 0;

  const char* get_name() const {
    return name;
  }

  void set_current_level(log_level l) {
    current_level = l;
  }

  void log(log_level level, const char *fmt, ...) __attribute__((format(printf, 3, 4))) {
    if (static_cast<int>(level) > static_cast<int>(current_level))
      return;
    switch (level) {
    case log_level::ERROR:
      printf("[ERROR] ");
      break;
    case log_level::WARN:
      printf("[WARN ] ");
      break;
    case log_level::INFO:
      printf("[INFO ] ");
      break;
    case log_level::DEBUG:
      printf("[DEBUG] ");
      break;
    default:
      break;
    }

    printf("%s: ", name);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }

protected:
  const char *name;
  log_level current_level = log_level::INFO;

  kernel_module(const char *n = "NONE", log_level level = log_level::INFO) :
      name(n), current_level(level) {
  }
};

