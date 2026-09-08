# Limitations and Scope

## Shell

The shell is intentionally not a full POSIX grammar. It supports pipelines, redirection, background execution, quotes, history, and core job-control built-ins, but not command substitution, glob expansion, shell variables, heredocs, logical `&&/||`, subshell syntax, or scripting-language constructs.

## Loader

The user-space loader exposes ELF/program-loading mechanics rather than replacing the kernel's `execve` implementation.

- Dynamic executables with `PT_INTERP` are rejected.
- Default fixed-address mapping uses `MAP_FIXED_NOREPLACE` to avoid silently destroying the loader's own mappings.
- `--force-fixed` switches to `MAP_FIXED` for purpose-built educational binaries whose virtual addresses are known to be safe.
- General process startup requires stack construction, auxiliary vectors, TLS, relocations, and/or an interpreter; direct entry transfer is intended for compatible freestanding static binaries.
- ELF32 inspection/mapping is supported. Direct ELF32 execution requires a compatible 32-bit process build.

## Tracer

The tracer is a small educational `ptrace` client. It does not implement source-level DWARF debugging, remote injection, stealth, persistence, credential access, or privilege escalation. Attaching to unrelated processes may be blocked by Linux ptrace policy.

## Networking

The TCP command server binds to `127.0.0.1` only. This prevents the demonstration from becoming an externally exposed remote shell. Commands are framed, output-capped, and executed through the project's own noninteractive pipeline engine.

## Allocator

The custom allocator demonstrates free lists, splitting, coalescing, mmap arenas, and synchronization. It does not currently return fully free arenas to the OS or implement advanced size classes, per-thread arenas, quarantine, hardened metadata, `realloc`, or `calloc`.

## Embedded Simulation

The embedded extension is intentionally **hardware-free**.

It implements real Linux mechanisms such as AArch64 cross compilation, QEMU user-mode execution, PTYs, Unix-domain sockets, POSIX threads, monotonic timers, `epoll`, `timerfd`, `eventfd`, `signalfd`, bounded ring buffers, a custom framed binary protocol, CRC32, watchdog timing, state-machine recovery, firmware-image integrity checks, and anti-rollback version policy.

It does **not** claim:

- STM32 or any specific MCU
- GPIO/SPI/I2C/UART peripheral register programming
- DMA
- JTAG/SWD
- FreeRTOS
- device-driver development
- physical board bring-up
- hard real-time guarantees

The PTY layer models serial-style byte transport; it is not a physical UART. Sensor samples and fault scenarios are generated in software. The firmware container is an educational image format, not a vendor flash format.
