# Linux + Embedded Execution Model

The project follows two connected paths: the Linux program-execution path and an embedded-style target-control path.

## Linux execution path

A command enters the shell parser. External commands are created with `fork`, wired through file descriptors and pipes, grouped for job control, and replaced with executables through `execvp`. The ELF tools then expose what the operating system normally interprets behind `exec`: ELF headers, sections, loadable segments, virtual addresses, memory permissions, and the entry point.

The static loader maps compatible `PT_LOAD` segments with `mmap`, handles BSS zero-fill, applies final permissions, and can transfer control to a compatible freestanding entry point. The `ptrace` module observes another process at the register/instruction level.

## Embedded simulation path

The same repository adds a software-only target system:

```text
Linux host CLI
    -> framed binary protocol
    -> PTY virtual UART or Unix-domain socket
    -> embedded target simulator
    -> periodic producer
    -> bounded ring buffer
    -> processing task
    -> watchdog/state-machine recovery
```

The protocol uses a fixed header, message type, payload length, sequence number, and CRC32. The persistent target can be queried, reconfigured, reset, and fault-injected from the host CLI.

## Event-driven Linux runtime

`event-demo` shows an embedded-Linux style event loop using:

- `epoll`
- `timerfd`
- `eventfd`
- `signalfd`

This demonstrates how periodic timers, worker notifications, and signals can be handled through one descriptor-driven loop instead of a polling loop.

## ARM target path

The standalone target simulator can be cross-compiled with `aarch64-linux-gnu-gcc` and executed under `qemu-aarch64`. The target-side code therefore runs as an actual AArch64 Linux executable even when no ARM board is available.

## Why the two sides belong together

The project connects system concepts that are often taught separately:

- process creation and execution
- file descriptors and IPC
- executable formats and virtual memory
- tracing and register state
- bounded memory and concurrency
- event-driven embedded Linux patterns
- serial-style framing and integrity checks
- target/host protocols
- watchdog recovery
- cross compilation and architecture portability

The result is one user-space systems project spanning Linux runtime internals and firmware-adjacent embedded software patterns without claiming physical hardware work.
