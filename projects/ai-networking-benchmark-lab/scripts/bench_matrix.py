#!/usr/bin/env python3
"""Run deterministic local TCP benchmarks and save machine-readable results.

Results are loopback payload measurements, not GPU, NIC, RDMA or physical-network metrics.
"""
import argparse
import json
import pathlib
import re
import select
import subprocess
import time
from contextlib import contextmanager


@contextmanager
def server(binary: str, clients: int):
    p = subprocess.Popen([binary, "server", "--port", "0", "--max-clients", str(clients)],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    try:
        ready, _, _ = select.select([p.stdout], [], [], 5)
        if not ready:
            raise RuntimeError("TCP server did not announce readiness")
        line = p.stdout.readline().strip()
        match = re.fullmatch(r"READY tcp=127\.0\.0\.1:(\d+)", line)
        if not match:
            raise RuntimeError(f"unexpected TCP server output: {line!r}")
        yield int(match.group(1))
    finally:
        if p.poll() is None:
            p.terminate()
        try:
            p.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()


def run_case(binary: str, payload: int, iterations: int, clients: int):
    with server(binary, clients) as port:
        cmds = [[binary, "bench", "--host", "127.0.0.1", "--port", str(port),
                 "--iters", str(iterations), "--bytes", str(payload), "--warmup", "5"]
                for _ in range(clients)]
        started = time.perf_counter()
        jobs = [subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                for cmd in cmds]
        try:
            results = []
            for p in jobs:
                out, err = p.communicate(timeout=25)
                if p.returncode != 0:
                    raise RuntimeError(f"benchmark process failed ({p.returncode}): {err}")
                result = json.loads(out)
                if not result.get("verified"):
                    raise RuntimeError("payload verification failed")
                results.append(result)
        finally:
            for p in jobs:
                if p.poll() is None:
                    p.kill()
                p.communicate()
        return {
            "transport": "TCP/IPv4 loopback", "clients": clients, "payload_bytes": payload,
            "iterations_per_client": iterations,
            "wall_seconds": round(time.perf_counter() - started, 6),
            "total_verified": clients * iterations,
            "client_measurements": results,
        }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default="./build/tensor_transport")
    ap.add_argument("--iters", type=int, default=80)
    ap.add_argument("--out", default="reports/benchmarks.json")
    args = ap.parse_args()
    if not 1 <= args.iters <= 10000:
        ap.error("--iters must be in [1,10000]")

    cases = [run_case(args.binary, size, args.iters, clients)
             for size, clients in [(64, 1), (4096, 1), (4096, 3), (65536, 1)]]
    data = {"method": "loopback TCP round-trip with CRC32 frame validation",
            "not_measured": "RDMA, GPU, hardware NIC throughput, end-to-end fabric bandwidth",
            "cases": cases}
    out = pathlib.Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {len(cases)} cases, {sum(x['total_verified'] for x in cases)} verified messages; {out}")


if __name__ == "__main__":
    main()
