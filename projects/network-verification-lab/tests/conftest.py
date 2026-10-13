"""Launch a real C++ process for every test: no mocks of the datapath."""
from __future__ import annotations

import os
import pathlib
import re
import select
import subprocess
import time

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "nver-datapath"


@pytest.fixture(scope="session", autouse=True)
def compile_datapath():
    subprocess.run(["make", "build"], cwd=ROOT, check=True, capture_output=True, text=True)


@pytest.fixture
def server(compile_datapath):
    proc = subprocess.Popen([str(BINARY), "--bind", "127.0.0.1", "--port", "0"],
                            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, bufsize=1)
    try:
        ready, _, _ = select.select([proc.stdout], [], [], 4.0)
        if not ready:
            raise RuntimeError("datapath did not start within 4 seconds")
        first_line = proc.stdout.readline().strip()
        match = re.fullmatch(r"READY udp=127\.0\.0\.1:(\d+)", first_line)
        if not match:
            raise RuntimeError(f"unexpected startup message: {first_line!r}")
        yield ("127.0.0.1", int(match.group(1)))
    finally:
        proc.terminate()
        try:
            proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate(timeout=2)


def wait_counter(server, key: str, minimum: int, timeout: float = 2.0):
    from nver_protocol import stats
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        observed = stats(server)
        if observed[key] >= minimum:
            return observed
        time.sleep(0.01)
    raise AssertionError(f"{key} did not reach {minimum} within {timeout}s; last={observed}")
