# Portfolio Notes

## Suggested resume entry

**Mini Linux Runtime & Embedded Execution Toolkit — C, Linux, ARM64, ELF, QEMU**

- Built a modular Linux execution toolkit with a Unix-style shell, N-stage pipelines, I/O redirection, job control, ELF32/ELF64 inspection, static `PT_LOAD` mapping, virtual-memory visualization, `ptrace` tracing, custom memory allocation, raw x86 system-call examples, and local TCP command execution.
- Extended the system with a hardware-free embedded target simulator using periodic POSIX-thread tasks, bounded ring buffers, watchdog recovery, `epoll`/`timerfd`/`eventfd`/`signalfd`, PTY-based virtual serial transport, a custom CRC32-framed binary protocol, firmware-image validation/anti-rollback logic, AArch64 cross compilation, and QEMU execution.

## Strong interview discussion paths

### Linux execution
- What changes between `fork` and `exec`?
- How does an N-stage pipeline wire file descriptors?
- Why are process groups needed for shell job control?
- How are ELF `PT_LOAD` permissions translated into `mmap`/`mprotect` permissions?
- Why can `p_memsz` be larger than `p_filesz`?
- What does the ELF entry point mean?
- How does `ptrace` stop, inspect, and single-step a process?

### Embedded-oriented software
- Why use a bounded ring buffer instead of an unbounded queue?
- How do absolute monotonic deadlines reduce timer drift?
- How would `epoll`, `timerfd`, `eventfd`, and `signalfd` fit an embedded-Linux event loop?
- How is a byte-stream protocol framed and protected against corrupted data?
- How does a watchdog distinguish a temporarily slow task from a stalled task?
- What is the difference between simulated serial transport and a physical UART driver?
- What does cross compilation change compared with native compilation?
- What can QEMU validate, and what still requires real hardware?
- How can image CRC and semantic-version policy support safe firmware-update logic?

## Honesty boundary

This repository supports claims about C/Linux systems programming, ARM64 cross compilation, QEMU, concurrency, real-time-style timing, bounded buffers, protocol design, virtual serial transport, watchdog/state-machine logic, and firmware-image validation.

Do not use it to claim STM32, FreeRTOS, SPI/I2C/GPIO, DMA, JTAG/SWD, physical UART bring-up, or hardware debugging.
