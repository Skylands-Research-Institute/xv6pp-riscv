#include "uart16550_driver.h"
#include "page_allocator.h"
#include "lock_guard.h"
#include "sleep_lock.h"
#include "xv6pp.h"

// the UART control registers are memory-mapped
// at address UART0. this macro returns the
// address of one of the registers.

static inline volatile unsigned char* Reg(uint64 reg) {
  return (unsigned char*) (UART0 + reg);
}

static inline unsigned char ReadReg(uint64 reg) {
  return *Reg(reg);
}

static inline void WriteReg(uint64 reg, unsigned char v) {
  *Reg(reg) = v;
}

// the UART control registers.
// some have different meanings for
// read vs write.
// see http://byterunner.com/16550.html
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

extern volatile int panicked;

uart16550_driver::uart16550_driver(const char *name) :
    char_device_driver(name) {
  init();
}

void uart16550_driver::init() {
  // disable interrupts.
  WriteReg(IER, 0x00);

  // special mode to set baud rate.
  WriteReg(LCR, LCR_BAUD_LATCH);

  // LSB for baud rate of 38.4K.
  WriteReg(0, 0x03);

  // MSB for baud rate of 38.4K.
  WriteReg(1, 0x00);

  // leave set-baud mode,
  // and set word length to 8 bits, no parity.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // reset and enable FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // enable transmit and receive interrupts.
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

void uart16550_driver::uartstart() {
  while (1) {
    if (uart_tx_w == uart_tx_r) {
      // transmit buffer is empty.
      ReadReg(ISR);
      return;
    }

    if ((ReadReg(LSR) & LSR_TX_IDLE) == 0) {
      // the UART transmit holding register is full,
      // so we cannot give it another byte.
      // it will interrupt when it's ready for a new byte.
      return;
    }

    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;

    // maybe uartputc() is waiting for space in the buffer.
    kernel.processes.wakeup(&uart_tx_r);

    WriteReg(THR, c);
  }
}

void uart16550_driver::putc(int c) {
  lock_guard<spin_lock> g(lock);
  if (panicked) {
    for (;;)
      ;
  }
  while (uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE) {
    // buffer is full.
    // wait for uartstart() to open up space in the buffer.
    kernel.interrupts.sleep(&uart_tx_r, lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
}

int uart16550_driver::getc() {
  if (ReadReg(LSR) & LSR_RX_READY) {
    // input data is ready.
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

void uart16550_driver::handle_interrupt() {
  // read and process incoming characters.
  while (1) {
    int c = kernel.console_device.getc();
    if (c == -1)
      break;
    kernel.console.interrupt(c);
  }

  // send buffered characters.
  lock_guard<spin_lock> g(lock);
  uartstart();
}

// alternate version of uartputc() that doesn't
// use interrupts, for use by kernel printf() and
// to echo characters. it spins waiting for the uart's
// output register to be empty.
void uart16550_driver::putc_sync(int c) {
  kernel.cpus.push_off();

  if (panicked) {
    for (;;)
      ;
  }

  // wait for Transmit Holding Empty to be set in LSR.
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  kernel.cpus.pop_off();
}

