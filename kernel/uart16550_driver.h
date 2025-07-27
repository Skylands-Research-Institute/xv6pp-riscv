#pragma once

#include "types.h"
#include "spin_lock.h"
#include "char_device.h"

#define UART_TX_BUF_SIZE 32

class uart16550_driver final : public char_device_driver {
private:
  spin_lock lock;
  char uart_tx_buf[UART_TX_BUF_SIZE];
  uint64 uart_tx_w; // write next to uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE]
  uint64 uart_tx_r; // read next from uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE]

  void uartstart();

public:
  explicit uart16550_driver(const char *name);
  void init() override;
  void putc(int c);
  void putc_sync(int c);
  int getc();
  void handle_interrupt();
};

