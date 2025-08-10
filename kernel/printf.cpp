//
// formatted console output -- printf, panic.
//

#include <stdarg.h>

#include "file_manager.h"
#include "xv6pp.h"
#include "spin_lock.h"

volatile int panicked = 0;

// lock to avoid interleaving concurrent printf's.
static struct {
  spin_lock lock;
  int locking;
} pr;

static char digits[] = "0123456789abcdef";

static void printint(long long xx, int base, int sign) {
  char buf[32];
  int i;
  unsigned long long x;

  if (sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  do {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    kernel.console.putc(buf[i]);
}

static void printptr(uint64 x) {
  kernel.console.putc('0');
  kernel.console.putc('x');
  for (uint64 i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    kernel.console.putc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console.
int printf(const char *fmt, ...) {
  va_list ap;
  int i, cx, c0, c1, c2, locking;
  char *s;

  locking = pr.locking;
  if (locking)
    pr.lock.acquire();

  va_start(ap, fmt);
  for (i = 0; (cx = fmt[i] & 0xff) != 0; i++) {
    if (cx != '%') {
      kernel.console.putc(cx);
      continue;
    }
    i++;
    c0 = fmt[i + 0] & 0xff;
    c1 = c2 = 0;
    if (c0)
      c1 = fmt[i + 1] & 0xff;
    if (c1)
      c2 = fmt[i + 2] & 0xff;
    if (c0 == 'd') {
    printint(va_arg(ap, int), 10, 1);
  } else if (c0 == 'l' && c1 == 'd') {
    printint(va_arg(ap, uint64), 10, 1);
    i += 1;
  } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
    printint(va_arg(ap, uint64), 10, 1);
    i += 2;
  } else if (c0 == 'u') {
  printint(va_arg(ap, int), 10, 0);
} else if (c0 == 'l' && c1 == 'u') {
  printint(va_arg(ap, uint64), 10, 0);
  i += 1;
} else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
  printint(va_arg(ap, uint64), 10, 0);
  i += 2;
} else if (c0 == 'x') {
printint(va_arg(ap, int), 16, 0);
} else if (c0 == 'l' && c1 == 'x') {
printint(va_arg(ap, uint64), 16, 0);
i += 1;
} else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
printint(va_arg(ap, uint64), 16, 0);
i += 2;
} else if (c0 == 'p') {
printptr (va_arg(ap, uint64));} else if(c0 == 's') {
  if((s = va_arg(ap, char*)) == 0)
  s = (char*)"(null)";
  for(; *s; s++)
  kernel.console.putc(*s);
} else if(c0 == '%') {
  kernel.console.putc('%');
} else if(c0 == 0) {
  break;
} else {
  // Print unknown % sequence to draw attention.
  kernel.console.putc('%');
  kernel.console.putc(c0);
}
}
  va_end(ap);

  if (locking)
    pr.lock.release();

  return 0;
}

void panic(const char *s) {
  pr.locking = 0;
  printf("panic: ");
  printf("%s\n", s);
  panicked = 1; // freeze uart output from other CPUs
  for (;;)
    ;
}

void printfinit(void) {
  pr.locking = 1;
}

int vprintf(const char *fmt, va_list ap) {
  int i, cx, c0, c1, c2;
  char *s;

  if (pr.locking)
    pr.lock.acquire();

  for (i = 0; (cx = fmt[i] & 0xff) != 0; i++) {
    if (cx != '%') {
      kernel.console.putc(cx);
      continue;
    }

    i++;
    c0 = fmt[i + 0] & 0xff;
    c1 = c2 = 0;
    if (c0)
      c1 = fmt[i + 1] & 0xff;
    if (c1)
      c2 = fmt[i + 2] & 0xff;

    if (c0 == 'd') {
    printint(va_arg(ap, int), 10, 1);
  } else if (c0 == 'l' && c1 == 'd') {
    printint(va_arg(ap, uint64), 10, 1);
    i += 1;
  } else if (c0 == 'l' && c1 == 'l' && c2 == 'd') {
    printint(va_arg(ap, uint64), 10, 1);
    i += 2;
  } else if (c0 == 'u') {
  printint(va_arg(ap, int), 10, 0);
} else if (c0 == 'l' && c1 == 'u') {
  printint(va_arg(ap, uint64), 10, 0);
  i += 1;
} else if (c0 == 'l' && c1 == 'l' && c2 == 'u') {
  printint(va_arg(ap, uint64), 10, 0);
  i += 2;
} else if (c0 == 'x') {
printint(va_arg(ap, int), 16, 0);
} else if (c0 == 'l' && c1 == 'x') {
printint(va_arg(ap, uint64), 16, 0);
i += 1;
} else if (c0 == 'l' && c1 == 'l' && c2 == 'x') {
printint(va_arg(ap, uint64), 16, 0);
i += 2;
} else if (c0 == 'p') {
printptr (va_arg(ap, uint64));} else if(c0 == 's') {
  if((s = va_arg(ap, char*)) == 0)
  s = (char*)"(null)";
  for(; *s; s++)
  kernel.console.putc(*s);
} else if(c0 == '%') {
  kernel.console.putc('%');
} else {
  kernel.console.putc('%');
  kernel.console.putc(c0);
}
}

  if (pr.locking)
    pr.lock.release();

  return 0;
}

