"""Functional, boundary, host power, management and regression test cases."""
import pytest
from src.bmc import BMC, ETH, EVENTS, HOST, HOST_RESET, BMC_RESET, ROOT


def default_config():
    return {"IPv4": {"Address": "10.20.30.22", "SubnetMask": "255.255.255.0",
                     "Gateway": "10.20.30.1"}, "VLANId": 200}


def test_discovery_root(client):
    code, root = client.get(ROOT)
    assert code == 200 and root["Systems"]["@odata.id"] == HOST
    assert root["Managers"]["@odata.id"] == BMC


def test_manager_metadata(client):
    code, data = client.get(BMC)
    assert code == 200 and data["Id"] == "BMC"
    assert data["Generation"] == 1
    assert data["EthernetInterfaces"]["@odata.id"] == ETH


def test_host_starts_powered_on(client):
    code, data = client.get(HOST)
    assert code == 200 and data["PowerState"] == "On" and data["BootCount"] >= 1


def test_host_off_then_on(client):
    status, data = client.post(HOST_RESET, {"ResetType": "ForceOff"})
    assert status == 200 and data["PowerState"] == "Off"
    assert client.get(HOST)[1]["PowerState"] == "Off"
    status, data = client.post(HOST_RESET, {"ResetType": "On"})
    assert status == 200 and data["PowerState"] == "On"
    assert data["BootCount"] >= 2


def test_host_restart_increases_boot_counter(client):
    before = client.get(HOST)[1]["BootCount"]
    code, data = client.post(HOST_RESET, {"ResetType": "ForceRestart"})
    assert code == 200 and data["PowerState"] == "On"
    assert data["BootCount"] == before + 1


def test_restarting_bmc_changes_generation_not_host_power(client):
    before = client.get(HOST)[1]["PowerState"]
    code, data = client.post(BMC_RESET, {"ResetType": "GracefulRestart"})
    assert code == 200 and data["Generation"] == 2
    assert client.get(BMC)[1]["Generation"] == 2
    assert client.get(HOST)[1]["PowerState"] == before


def test_restart_creates_an_auditable_event(client):
    client.post(HOST_RESET, {"ResetType": "ForceRestart"})
    code, log = client.get(EVENTS)
    assert code == 200 and log["Members@odata.count"] >= 1
    assert any("ForceRestart" in event["Message"] for event in log["Members"])


def test_get_management_nic(client):
    code, eth = client.get(ETH)
    assert code == 200 and eth["Id"] == "eth0"
    assert eth["IPv4"]["VLANId"] == 100


def test_patch_nic_address_and_vlan(client):
    config = default_config()
    code, result = client.patch(ETH, config)
    assert code == 200 and result["IPv4"]["Address"] == "10.20.30.22"
    assert client.get(ETH)[1]["IPv4"]["VLANId"] == 200


@pytest.mark.parametrize("vlan", [0, 4095, -1, "100", True, None])
def test_reject_invalid_vlan_atomically(client, vlan):
    before = client.get(ETH)[1]["IPv4"]
    config = default_config()
    config["VLANId"] = vlan
    assert client.patch(ETH, config)[0] == 400
    assert client.get(ETH)[1]["IPv4"] == before


@pytest.mark.parametrize("address,mask,gateway", [
    ("not-an-ip", "255.255.255.0", "10.20.30.1"),
    ("10.20.30.22", "255.255.255.0", "10.99.99.1"),
    ("10.20.30.0", "255.255.255.0", "10.20.30.1"),
    ("10.20.30.255", "255.255.255.0", "10.20.30.1"),
    ("10.20.30.22", "invalid-mask", "10.20.30.1"),
    ("10.20.30.22", "255.255.255.0", "10.20.30.22"),
])
def test_reject_bad_ipv4_topologies_without_partial_updates(client, address, mask, gateway):
    config = default_config()
    config["IPv4"] = {"Address": address, "SubnetMask": mask, "Gateway": gateway}
    old = client.get(ETH)[1]["IPv4"]
    assert client.patch(ETH, config)[0] == 400
    assert client.get(ETH)[1]["IPv4"] == old


def test_reject_extra_network_properties(client):
    data = default_config()
    data["unsafe"] = True
    assert client.patch(ETH, data)[0] == 400


@pytest.mark.parametrize("reset", ["ShutDown", "ColdBoot", "", 123, None])
def test_unsupported_reset_types_fail_without_side_effects(client, reset):
    before = client.get(HOST)[1]
    assert client.post(HOST_RESET, {"ResetType": reset})[0] == 400
    assert client.get(HOST)[1] == before


def test_unknown_resource_returns_json_404(client):
    status, data = client.get(ROOT + "/Unknown")
    assert status == 404 and "error" in data


def test_audit_event_ids_are_monotonic(client):
    client.post(HOST_RESET, {"ResetType": "ForceRestart"})
    client.patch(ETH, default_config())
    client.post(BMC_RESET, {"ResetType": "GracefulRestart"})
    ids = [int(record["Id"]) for record in client.get(EVENTS)[1]["Members"]]
    assert ids == sorted(set(ids)) and len(ids) >= 3
