#!/usr/bin/env python3
"""Reproducible traffic and fault-injection demo with a JSON summary.

Examples:
  python3 python/traffic_lab.py --binary build/nver-datapath --count 100
  python3 python/traffic_lab.py --port 12345 --count 100 --json reports/run.json
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import select
import socket
import subprocess
import sys
import time
from contextlib import contextmanager

from nver_protocol import ACK, DATA, Packet, stats, transact


@contextmanager
def endpoint(binary: str | None, port: int):
    if not binary:
        yield ("127.0.0.1", port)
        return
    proc = subprocess.Popen([binary, "--port", "0"], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, bufsize=1)
    try:
        ready, _, _ = select.select([proc.stdout], [], [], 4)
        if not ready:
            raise RuntimeError("datapath startup timeout")
        line = proc.stdout.readline()
        match = re.fullmatch(r"READY udp=127\.0\.0\.1:(\d+)\n", line)
        if not match:
            raise RuntimeError(f"unexpected startup: {line!r}")
        yield ("127.0.0.1", int(match.group(1)))
    finally:
        proc.terminate()
        try:
            proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate(timeout=2)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--binary", help="path to compiled datapath (starts it automatically)")
    group.add_argument("--port", type=int, help="port of a running datapath")
    parser.add_argument("--count", type=int, default=100)
    parser.add_argument("--corrupt-every", type=int, default=10)
    parser.add_argument("--replay-every", type=int, default=13)
    parser.add_argument("--json", help="optional report path")
    args = parser.parse_args()
    if args.count < 1 or args.corrupt_every < 0 or args.replay_every < 0:
        parser.error("count must be positive; fault frequencies must be nonnegative")

    report = {}
    with endpoint(args.binary, args.port or 0) as address:
        started = time.perf_counter()
        accepted = 0
        injected_crc = injected_replay = 0
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            for seq in range(1, args.count + 1):
                payload = (f"network-traffic-{seq}:" + "x" * 64).encode()
                packet = Packet(DATA, 10, seq, payload)
                if args.corrupt_every and seq % args.corrupt_every == 0:
                    bad = bytearray(packet.encode())
                    bad[-1] ^= 0x80
                    sock.sendto(bad, address)
                    injected_crc += 1
                reply = transact(address, packet, timeout=1)
                if reply.kind != ACK or reply.sequence != seq or reply.payload != payload:
                    raise RuntimeError(f"verification mismatch at seq {seq}")
                accepted += 1
                if args.replay_every and seq % args.replay_every == 0:
                    sock.sendto(packet.encode(), address)
                    injected_replay += 1
        elapsed = time.perf_counter() - started
        deadline = time.monotonic() + 3
        while True:
            observed = stats(address)
            if observed["crc_errors"] >= injected_crc and observed["replay_errors"] >= injected_replay:
                break
            if time.monotonic() > deadline:
                raise RuntimeError(f"counters did not converge: {observed}")
            time.sleep(0.01)
        report = {
            "scenario": "udp-datapath-validation",
            "frames_verified": accepted,
            "crc_faults_injected": injected_crc,
            "replays_injected": injected_replay,
            "elapsed_seconds": round(elapsed, 4),
            "frames_per_second": round(accepted / elapsed, 1),
            "counters": observed,
            "result": "PASS" if observed["accepted"] == accepted and
            observed["crc_errors"] == injected_crc and
            observed["replay_errors"] == injected_replay else "FAIL",
        }
    output = json.dumps(report, indent=2)
    print(output)
    if args.json:
        path = pathlib.Path(args.json)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(output + "\n", encoding="utf-8")
    return 0 if report["result"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
