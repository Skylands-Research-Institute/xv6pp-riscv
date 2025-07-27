# xv6++

**xv6++** is a modern C++ re-implementation of the xv6 operating system, which itself is a pedagogical re-implementation of Dennis Ritchie's and Ken Thompson's UNIX Version 6 (v6). xv6++ retains xv6’s educational clarity while adopting idiomatic C++ design patterns and object-oriented abstractions. It runs on a RISC-V multiprocessor and is intended for use in operating systems courses and instructional research.

## Highlights

- Core kernel subsystems restructured as C++ classes
- Use of RAII, intrusive lists, and modular encapsulation
- Designed to minimize runtime overhead and maximize code clarity
- Fully compatible with xv6 user-space programs
- Maintains xv6’s minimalism while enabling C++-based OS education

## Educational Purpose

xv6++ is designed to help instructors and students explore OS internals using C++ syntax and abstractions already familiar from introductory programming. It provides a bridge between systems programming and modern C++ concepts without hiding the core mechanisms of a traditional Unix-style kernel.

## Project Background

xv6++ is based on MIT’s [xv6](https://pdos.csail.mit.edu/6.1810/xv6/), developed by Frans Kaashoek and Robert Morris for MIT’s 6.1810 course. xv6 was inspired by John Lions’s commentary on UNIX Version 6.

This C++ version is authored by **John Sissler** at **Sussex County Community College (Newton, NJ)**, with support from the **Skylands Research Institute**.

## Building and Running

To build xv6++, you will need:

- The RISC-V newlib toolchain: [https://github.com/riscv/riscv-gnu-toolchain](https://github.com/riscv/riscv-gnu-toolchain)
- QEMU with RISC-V 64-bit support (`riscv64-softmmu`)
- GNU Make and a POSIX-compliant shell

### Build and Run

```bash
make qemu
```

This will compile and launch xv6++ in QEMU.

## Directory Overview

- `kernel/` – Core kernel code reimplemented in C++
- `user/` – xv6 user programs (mostly unchanged)
- `mkfs/` – File system creation code

## License

xv6++ is distributed under the same terms as xv6. See the `LICENSE` file for details.

## Feedback and Contributions

This project is primarily intended for teaching and research. Suggestions for clarifications and simplifications are welcome.

To report issues or offer feedback, please contact:

**John Sissler**  
jsissler@sussex.edu  
Sussex County Community College  
Newton, NJ

