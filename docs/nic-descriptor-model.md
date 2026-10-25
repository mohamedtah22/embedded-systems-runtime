# NIC descriptor-ring firmware lab

A small C11 model of the boundary between driver code and a device. It runs
natively on Linux and as an AArch64 Linux program under QEMU user mode.
This is a software model, with register-like atomics and fixed packet storage;
it does not program physical DMA, MMIO registers, or a real interrupt controller.

## Ownership and ordering

TX descriptors follow `FREE -> DEVICE -> DONE -> FREE`. The driver fills a
buffer, publishes DEVICE with a release store, and cannot reuse that descriptor
until it has observed completion and reclaimed it. The device acquires ownership
before reading the payload. RX descriptors follow `DEVICE -> DONE -> DEVICE`:
receive copies a completed frame before reposting its buffer. A too-small caller
buffer leaves the completed descriptor available for retry.

Exactly one driver thread and one device thread own their respective cursors.
Release/acquire operations transfer buffer ownership between those threads.
The model is SPSC; multiple producers must add external serialization.

## Bounded resources and recovery

- Ring sizes 1 through 64, including non-powers-of-two; packet size 1-1536 bytes.
- Fixed descriptor/buffer arrays; no heap allocation in the model's data path.
- TX backpressure is explicit and does not overwrite outstanding descriptors.
- A full RX ring drops the incoming loopback frame, counts the drop, and still
  completes TX so the driver can reclaim resources.
- Each device poll is capped to one ring's worth of descriptors.
- Atomic interrupt-status bits report TX/RX/error events and clear on read.
- One-shot TX fault injection verifies error completion and subsequent recovery.
- Reset requires both threads to be quiescent; concurrent reset is unsupported.

## Run and verify

```bash
make nic-model-test
make nic-model-sanitize
make qemu-smoke   # requires AArch64 cross compiler and qemu-aarch64
```

The five assertion groups cover invalid sizes and poll budgets, MTU boundaries,
short receive buffers, TX reclaim/backpressure, RX exhaustion, 10,000 ordered
wraparound transfers through a three-slot ring, injected errors, interrupt
acknowledgement, quiescent reset, and 5,000 transfers between two concurrent
threads. These counts describe verification workloads, not measured hardware
throughput or latency guarantees. ASan/UBSan run in CI; environments where
LeakSanitizer cannot inspect `/proc` can use
`make ASAN_OPTIONS=detect_leaks=0 nic-model-sanitize`.

Source: `include/nic_model.h`, `firmware/nic_model.c`, `tests/test_nic_model.c`.
