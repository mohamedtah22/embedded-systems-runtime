# Network Verification Lab

[![Verification CI](https://github.com/mohamedtah22/embedded-systems-runtime/actions/workflows/network-verification-lab.yml/badge.svg)](https://github.com/mohamedtah22/embedded-systems-runtime/actions/workflows/network-verification-lab.yml)

**A reproducible, black-box network-protocol verification project built with C++17, Python, pytest, and GitHub Actions.** Designed to practice the kind of systems-debugging, packet-validation, fault-injection, and regression-testing skills relevant to software verification and network infrastructure engineering.

> **Scope:** This is an educational UDP datapath and its *software verification harness*. It is **not** the NVIDIA DOCA SDK, a BlueField DPU emulator, a production Ethernet forwarding stack, or a hardware-accelerated datapath. No specialized hardware or root privileges are needed for CI.

## What this project demonstrates

| Engineering area | Implementation | How it is verified |
|---|---|---|
| Network protocol | Big-endian 24-byte header, session IDs, sequence IDs, binary payloads | Exact packet round-trip, serialization tests |
| Data integrity | CRC32 over header (without CRC field) + payload | Corrupted header CRC and payload rejection |
| Input validation | Version, magic, flags, lengths, type validation | Truncated, oversized, unknown-type, malformed input tests |
| Stateful datapath | Per-session anti-replay sequence tracking | Duplicate, out-of-order, independent-session tests |
| Linux systems | Native C++17 UDP socket server on loopback | Real subprocess with ephemeral port; no network mocks |
| Concurrent traffic | Multiple clients in parallel, shared datapath | Six concurrent sessions / 90 verified packets |
| Fault injection | Fixed-seed bit flip campaigns + intentional replay | Assertions on error counters and health after faults |
| Test automation | Python `pytest` and a reproducible CLI traffic generator | Automated build, full suite, JUnit results on GitHub Actions |

## Architecture

```text
  +------------------------+       UDP wire protocol      +------------------------+
  | Python test harness    | ---------------------------> | C++ datapath            |
  | pytest / traffic_lab   | <--------------------------- | 127.0.0.1:ephemeral     |
  |                        |       ACK / stats / reset   |                        |
  | protocol oracle        |                             | parser + CRC + sessions |
  +------------------------+                             +------------------------+
           |                                                         |
           +--------------- validate counters, payloads -------------+
           |
  GitHub Actions: compile -> pytest -> traffic demo -> JUnit / JSON artifacts
```

## Quick start (Linux)

Requires Linux, `g++` with C++17 support, GNU `make`, Python 3.10+, and `pip`. Uses local UDP and an ephemeral port, so it requires **no sudo and no DPU**.

```bash
# From this directory
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements-dev.txt
make build
make test
make demo
```

Expected test result at the time this project was created: **42 pytest tests passed locally**. To produce a machine-readable traffic summary:

```bash
PYTHONPATH=python python3 python/traffic_lab.py \
  --binary ./build/nver-datapath --count 100 \
  --corrupt-every 10 --replay-every 13 \
  --json reports/traffic-summary.json
```

The demo exits nonzero if observed accepted, CRC-error, or replay counters do not match the intentionally injected traffic. `frames_per_second` is merely a demo measurement on the executing machine, **not** a hardware throughput benchmark.

## Test cases

- Valid data: zero-length payload, single byte, binary bytes including NUL, MTU-sized payload (1400 bytes), large session/sequence IDs.
- Invalid data: truncated datagram and payload, corrupt CRC, wrong magic or version, reserved field, nonzero flags, oversized payload and unknown packet types.
- State: duplicate sequences and older sequences dropped per session; reset clears counters and sequence history.
- Automation: isolated real server fixture for each test; deterministic 100-frame corruption campaign; concurrent clients; machine-readable counters.

Detailed design and limitations: [docs/TEST_PLAN.md](docs/TEST_PLAN.md).

## Repository layout

```text
src/datapath.cpp          C++17 packet-processing service and counters
python/nver_protocol.py   Python binary packet codec + request helpers
python/traffic_lab.py     Deterministic packet and fault-generation CLI
tests/conftest.py         Launch/teardown of a real isolated C++ service
tests/test_datapath.py    Black-box protocol and negative tests
tests/test_stress.py      Parallel client and repeatable fuzz-like tests
docs/TEST_PLAN.md         Test strategy, assumptions and limitations
Makefile                  Native build/test/demo commands
requirements-dev.txt      pytest dependency
```

GitHub Actions workflow: [`../../.github/workflows/network-verification-lab.yml`](../../.github/workflows/network-verification-lab.yml) **from the repository root** (the project lives under `projects/network-verification-lab/`).

## What I would extend next

- Add `tc netem` packet-loss and reordering tests behind an *optional, privileged* Linux runner.
- Extend the network model with multiple protocol flows and a reference datapath oracle.
- Instrument p50/p95 response latency and export Prometheus-style metrics.
- If BlueField hardware becomes available: write a *separate* hardware-backed integration suite against the real DOCA runtime.

## License

MIT — see [LICENSE](LICENSE).


## Resource and sequence policy

The server admits at most 64 active sessions. New sessions beyond that bound
are rejected and counted as `session_errors`; established sessions continue.
Reset releases all session state. Sequence numbers use 32-bit serial arithmetic:
wraparound is accepted for forward distances below half the number space;
duplicates, older packets, and ambiguous half-range jumps are rejected.
This educational loopback protocol does not authenticate senders; CRC32 checks
integrity, not identity. Control reset is intentionally available to local tests.
