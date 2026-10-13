"""Concurrent management, basic latency checks, and topology assertions."""
import os
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

from src.bmc import BMC, ETH, HOST, HOST_RESET
from src.client import BmcClient


def test_32_parallel_management_reads_remain_consistent(lab):
    def read(_):
        c = BmcClient(lab["url"], lab["cert"], lab["viewer"])
        return c.get(BMC)
    with ThreadPoolExecutor(max_workers=8) as pool:
        outcomes = list(pool.map(read, range(32)))
    assert all(code == 200 and response["Id"] == "BMC" for code, response in outcomes)


def test_simple_management_read_latency_smoke(client):
    started = time.monotonic()
    for _ in range(20):
        assert client.get(BMC)[0] == 200
    elapsed = time.monotonic() - started
    # Generous timing budget, not an Ethernet hardware benchmark.
    assert elapsed < 15, f"Management API became unexpectedly slow: {elapsed:.1f}s"


def test_power_off_is_visible_via_management_network(client):
    assert client.post(HOST_RESET, {"ResetType": "ForceOff"})[0] == 200
    assert client.get(HOST)[1]["PowerState"] == "Off"
    assert client.post(HOST_RESET, {"ResetType": "On"})[0] == 200
    assert client.get(HOST)[1]["PowerState"] == "On"


def test_compose_qa_client_cannot_resolve_isolated_host_agent():
    if os.environ.get("VERIFY_ISOLATED_TOPOLOGY") != "1":
        import pytest
        pytest.skip("Docker Compose topology-only check")
    probe = subprocess.run(["getent", "hosts", "host-agent"], capture_output=True, timeout=3)
    assert probe.returncode != 0, "Host data-plane service leaked into the QA management network"


def test_management_nic_interface_reports_up(client):
    code, data = client.get(ETH)
    assert code == 200 and data["LinkStatus"] == "Up"
