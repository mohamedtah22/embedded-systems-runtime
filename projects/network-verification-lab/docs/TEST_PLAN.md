# Verification strategy & limitations

## Verification objective

Validate a *software-only* packet-processing target from outside the process, using real UDP sockets. Tests must identify both false acceptance of bad frames and rejection of valid frames, while checking observable diagnostic counters.

## Packet format

All multibyte fields are big-endian. Header is exactly 24 bytes:

| Offset | Bytes | Field | Constraint |
|---|---:|---|---|
| 0 | 4 | magic | `0x4e564552` (`NVER`) |
| 4 | 1 | version | `1` |
| 5 | 1 | type | `1=DATA`, `2=ACK`, `3=STATS_REQUEST`, `4=STATS_REPLY`, `5=RESET_REQUEST`, `6=RESET_REPLY` |
| 6 | 2 | flags | `0` |
| 8 | 4 | session | `uint32` |
| 12 | 4 | sequence | `uint32` |
| 16 | 2 | payload length | `0..1400` |
| 18 | 2 | reserved | `0` |
| 20 | 4 | CRC32 | IEEE CRC32 of bytes `0..19` concatenated with payload |
| 24 | N | payload | raw bytes |

The CRC field is **not** included in its own CRC computation. Invalid datagrams are intentionally silent (drop without reply); tests expect UDP read timeouts and then check server counters.

## Test matrix

| Scenario | Expected |
|---|---|
| Valid DATA packet | ACK with identical session/sequence/payload; accepted counter increments |
| Duplicate / older sequence in one session | No ACK; replay_errors increments |
| First sequence in different session | ACK (session isolation) |
| Invalid CRC | No ACK; crc_errors increments |
| Missing header / inconsistent frame size / oversized payload | No ACK; length_errors increments |
| Bad magic, version, flags, reserved bits | No ACK; header_errors increments |
| Unexpected type / invalid control request | No ACK; type_errors increments |
| STATS_REQUEST | STATS_REPLY with machine-readable counters |
| RESET_REQUEST | RESET_REPLY; clears counters and replay history |
| 6 parallel client sessions | Every echoed payload is correct; no cross-talk |
| Fixed-seed corruption of 100 packets | Exactly 100 CRC errors; process remains healthy |

## CI and reproducibility

`pytest` starts a separate datapath subprocess bound to `127.0.0.1:0` for every integration test. The process prints its actual ephemeral port on stdout. Fixtures guarantee cleanup even when assertions fail. No mocks stand in for network I/O. The fuzz-like test uses a hard-coded seed, so failures can be reproduced.

The GitHub Actions workflow installs Python, builds C++ with `-Wall -Wextra -Wpedantic -Werror`, executes the test suite, runs the CLI demo with faults enabled, and uploads JUnit + JSON reports. A full CI run is the authoritative evidence of status after publication.

## Explicit limitations

- This is UDP request/reply **verification**, not L2/L3 switching, routing, kernel bypass, RDMA, DPDK, or a DOCA API implementation.
- Loopback traffic in CI is not a NIC or DPU performance measurement.
- Server processes packets on a single thread; concurrent clients test session isolation, not parallel packet processing.
- The demo uses in-memory state without authentication, persisted counters, retransmissions, resource quotas, or resilience to sequence-number wrap-around; sessions should be short-lived.
- UDP drop tests use short local timeouts; tests assert counters to distinguish deliberate validation rejection from a silent network failure.
- Validation counters include STATS requests; requests themselves affect the `datagrams` field.
- The executable binds to loopback by default, and should not be exposed directly as a production network service.

## Debugging playbook

1. Run a single failed test: `PYTHONPATH=python pytest -vv tests/test_datapath.py -k crc`.
2. Reproduce with `python/traffic_lab.py` and a fixed packet count.
3. Inspect the JSON `counters` and compare `accepted`, `invalid`, and fault categories.
4. Optionally capture traffic: `sudo tcpdump -ni lo udp port <actual-port> -w trace.pcap`.
5. Confirm whether the failure originated in header validation, length/CRC validation, replay detection, or the Python oracle.
