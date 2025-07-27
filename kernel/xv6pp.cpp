#include "xv6pp.h"

xv6pp::xv6pp() :
    cpus("CPUs"), allocator("page allocator"), cache("buffer cache"), console_device("console device"), console_driver(
        "console driver"), memory("virtual memory"), console("console"), disk_driver("disk driver"), disk("disk"), scheduler(
        "process scheduler"), processes("process manager"), interrupts("interrupt manager"), fmanager("file manager"), fsystem(
        "file system"), log("file system log") {
}
