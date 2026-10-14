# Verification plan

## Positive cases
- TCP payload round-trip at 0, 1, 64, 4096, 65536 bytes.
- Two parallel clients, each sends multiple checked messages.
- Bounded small pipelining window: in-order messages and matching sequences.
- Micro-benchmark produces parseable JSON metrics for different packet sizes and client counts.

## Negative cases
- Corrupt server response is rejected with a CRC error in the client.
- Malformed CRC from client closes the connection without crashing the server.
- Oversized incoming frame is rejected before memory allocation.
- Invalid command-line ranges exit with a non-zero code.
- Analytical model rejects invalid ranks, negative tensor sizes and zero link rate.

## Performance methodology
- C++ benchmark uses steady_clock for RTT percentiles (p50/p95/p99), and payload byte counts for throughput.
- `--warmup` iterations are excluded from measured payload/iteration totals.
- Tests run over loopback, and reported values vary with CPU scheduling and machine load.
- TCP message-framing, payload verification, checksums and userspace copies all affect measurements.
- The analytical collective model is a **separate** idealized alpha-beta estimate; it does not exchange tensors between machines.

## Limitations
- Not a GPU benchmark, ML training runtime, real AllReduce implementation or high-speed cluster simulator.
- Not RDMA, InfiniBand, RoCE, NIC driver, or actual NVIDIA hardware.
- Does not implement TLS or production authentication; binds to 127.0.0.1 by default.
- Pipelined clients with very large messages may exert socket backpressure and should use small windows.
- Server accepts a configured number of clients and exits once they disconnect; not a durable daemon.
