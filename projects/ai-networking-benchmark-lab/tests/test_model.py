import json
import subprocess
import pytest


def calculate(binary, *options):
    return subprocess.run([binary, *options], capture_output=True, text=True, timeout=5)


def test_default_model(model_binary):
    p = calculate(model_binary)
    assert p.returncode == 0
    j = json.loads(p.stdout)
    assert j["model"] == "idealized_alpha_beta"
    assert j["ring_allreduce_ms"] > 0
    assert j["tree_allreduce_ms"] > 0


def test_tiny_tensor_latency_effect(model_binary):
    common = ["--ranks", "4", "--tensor-mib", "0.001", "--gbps", "100"]
    fast = json.loads(calculate(model_binary, *common, "--latency-us", "1").stdout)
    slow = json.loads(calculate(model_binary, *common, "--latency-us", "100").stdout)
    assert slow["ring_allreduce_ms"] > fast["ring_allreduce_ms"]


def test_more_bandwidth_improves_ideal_time(model_binary):
    common = ["--ranks", "8", "--tensor-mib", "512"]
    slow = json.loads(calculate(model_binary, *common, "--gbps", "10").stdout)
    fast = json.loads(calculate(model_binary, *common, "--gbps", "100").stdout)
    assert fast["ring_allreduce_ms"] < slow["ring_allreduce_ms"]


@pytest.mark.parametrize("args", [
    ["--ranks", "1"], ["--ranks", "3.5"], ["--gbps", "0"],
    ["--tensor-mib", "-2"], ["--latency-us", "-4"]
])
def test_invalid_model_parameters(model_binary, args):
    p = calculate(model_binary, *args)
    assert p.returncode != 0
