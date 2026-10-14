"""Black-box testing against real socket processes, not network mocks."""
import json
import pathlib
import re
import select
import socket
import struct
import subprocess
import sys
import time
import zlib
from contextlib import contextmanager

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]


@contextmanager
def live_server(binary, *, clients=1, extra=()):
    p = subprocess.Popen([binary, "server", "--port", "0", "--max-clients", str(clients), *extra],
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1)
    try:
        ready, _, _ = select.select([p.stdout], [], [], 4)
        assert ready, "server startup timeout"
        line = p.stdout.readline().strip()
        match = re.fullmatch(r"READY tcp=127\.0\.0\.1:(\d+)", line)
        assert match, line
        yield p, int(match.group(1))
    finally:
        if p.poll() is None:
            p.terminate()
        try:
            p.communicate(timeout=4)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()


def run_client(binary, port, *, size=128, iterations=8, warmup=1, window=1):
    return subprocess.run([binary, "bench", "--port", str(port), "--bytes", str(size),
                           "--iters", str(iterations), "--warmup", str(warmup),
                           "--window", str(window)], capture_output=True, text=True, timeout=12)


@pytest.mark.parametrize("size", [0, 1, 64, 4096, 65536])
def test_correct_roundtrips_and_latency(binary, size):
    with live_server(binary) as (p, port):
        result = run_client(binary, port, size=size)
        assert result.returncode == 0, result.stderr
        report = json.loads(result.stdout)
        assert report["verified"] is True
        assert report["payload_bytes"] == size
        assert report["iters"] == 8
        assert 0 <= report["rtt_p50_us"] <= report["rtt_p95_us"]


def test_small_pipeline_window(binary):
    with live_server(binary) as (_, port):
        result = run_client(binary, port, size=32, iterations=9, warmup=0, window=3)
        assert result.returncode == 0, result.stderr
        assert json.loads(result.stdout)["verified"] is True


def test_two_concurrent_clients(binary):
    with live_server(binary, clients=2) as (_, port):
        cmd = [binary, "bench", "--port", str(port), "--bytes", "256", "--iters", "15", "--warmup", "0"]
        p1 = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        p2 = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        for p in (p1, p2):
            out, err = p.communicate(timeout=12)
            assert p.returncode == 0, err
            assert json.loads(out)["verified"] is True


def test_explicit_server_fault_is_detected(binary):
    with live_server(binary, extra=("--corrupt-every", "1")) as (_, port):
        result = run_client(binary, port, size=64, iterations=2, warmup=0)
        assert result.returncode != 0
        assert "bad payload CRC" in result.stderr


def wire(payload=b"hello", seq=1, *, crc=None, magic=0x46414252, length=None):
    checksum = zlib.crc32(payload) if crc is None else crc
    return struct.pack("!5I", magic, 1, seq, len(payload) if length is None else length, checksum) + payload


def test_malformed_crc_rejected_without_crash(binary):
    with live_server(binary) as (server, port):
        with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
            s.sendall(wire(b"abcd", crc=42))
            s.settimeout(1)
            assert s.recv(1) == b""
        server.wait(timeout=4)
        assert server.returncode == 0


def test_oversized_frame_rejected(binary):
    with live_server(binary) as (server, port):
        with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
            s.sendall(wire(b"", length=1048577)[:20])
            s.settimeout(1)
            assert s.recv(1) == b""
        server.wait(timeout=4)
        assert server.returncode == 0


def test_invalid_cli_arguments(binary):
    bad_cases = [["bench", "--port", "0"], ["bench", "--port", "5555", "--bytes", "2000000"],
                 ["server", "--max-clients", "0"]]
    for opts in bad_cases:
        p = subprocess.run([binary, *opts], capture_output=True, text=True, timeout=5)
        assert p.returncode != 0
        assert "ERROR" in p.stderr


def test_benchmark_matrix(binary, tmp_path):
    output = tmp_path / "benchmark.json"
    p = subprocess.run([sys.executable, "scripts/bench_matrix.py", "--binary", binary,
                        "--iters", "8", "--out", str(output)], cwd=ROOT,
                       capture_output=True, text=True, timeout=40)
    assert p.returncode == 0, p.stderr
    data = json.loads(output.read_text())
    assert len(data["cases"]) == 4
    assert all(x["total_verified"] > 0 for x in data["cases"])
