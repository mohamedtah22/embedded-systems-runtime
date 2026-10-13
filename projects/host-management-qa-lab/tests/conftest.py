"""Black-box tests: real TLS sockets, real HTTP servers and a tiny host agent."""
from __future__ import annotations

import os
import pathlib
import re
import select
import socket
import subprocess
import sys
import time
from contextlib import contextmanager

import pytest

from src.client import BmcClient

ADMIN = "lab-admin-test-token-2026"
VIEWER = "lab-viewer-test-token-2026"
AGENT = "lab-agent-private-key-2026"


@contextmanager
def started(args, env):
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                         text=True, bufsize=1, env=env)
    try:
        ready, _, _ = select.select([p.stdout], [], [], 5)
        if not ready:
            raise RuntimeError("Service did not start within 5 seconds")
        line = p.stdout.readline().strip()
        match = re.fullmatch(r"READY (?:host|bmc)=([^:]+):(\d+)", line)
        if not match:
            raise RuntimeError(f"Bad ready line {line!r}; stderr={p.stderr.read() if p.poll() is not None else ''}")
        yield f"{match.group(1)}:{match.group(2)}"
    finally:
        p.terminate()
        try:
            p.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate(timeout=2)


@pytest.fixture(scope="session")
def lab(tmp_path_factory):
    remote = os.environ.get("BMC_BASE_URL")
    if remote:
        cert = pathlib.Path(os.environ.get("BMC_CA_CERT", "/certs/cert.pem"))
        for _ in range(40):
            if cert.exists():
                break
            time.sleep(.1)
        yield {"url": remote, "cert": cert,
               "admin": os.environ.get("BMC_ADMIN_TOKEN", ADMIN),
               "viewer": os.environ.get("BMC_VIEWER_TOKEN", VIEWER), "agent": None}
        return
    certs = tmp_path_factory.mktemp("qa_certs")
    subprocess.run(["sh", "scripts/gen_dev_cert.sh", str(certs), "localhost"], check=True)
    env = dict(os.environ, HOST_AGENT_KEY=AGENT, BMC_ADMIN_TOKEN=ADMIN, BMC_VIEWER_TOKEN=VIEWER)
    with started([sys.executable, "-m", "src.host_agent", "--port", "0"], env) as agent:
        with started([sys.executable, "-m", "src.bmc", "--port", "0", "--agent-url",
                      "http://" + agent, "--cert", str(certs / "cert.pem"),
                      "--key", str(certs / "key.pem")], env) as bmc:
            yield {"url": "https://" + bmc, "cert": certs / "cert.pem",
                   "admin": ADMIN, "viewer": VIEWER, "agent": agent}


@pytest.fixture
def client(lab):
    return BmcClient(lab["url"], lab["cert"], lab["admin"])


@pytest.fixture
def viewer(lab):
    return BmcClient(lab["url"], lab["cert"], lab["viewer"])


@pytest.fixture(autouse=True)
def fresh_fixture(lab):
    """Reset lab state, not the TLS or network processes, for every test case."""
    api = BmcClient(lab["url"], lab["cert"], lab["admin"])
    for attempt in range(50):
        try:
            code, body = api.post("/_lab/reset", {})
            if code == 200:
                return
        except Exception:
            pass
        time.sleep(.1)
    raise RuntimeError("BMC lab fixture did not respond to reset action")
