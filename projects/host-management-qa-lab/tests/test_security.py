"""Security negative cases; no claims of production security certification."""
import json
import ssl
from urllib.error import URLError
from urllib.request import urlopen

import pytest

from src.bmc import BMC, ETH, EVENTS, HOST, HOST_RESET, ROOT
from src.client import BmcClient
from test_functional import default_config


def test_unauthenticated_get_is_denied(lab):
    client = BmcClient(lab["url"], lab["cert"], "")
    assert client.get(ROOT)[0] == 401


def test_wrong_token_is_denied(lab):
    client = BmcClient(lab["url"], lab["cert"], "wrong-token-that-has-enough-length")
    assert client.get(HOST)[0] == 401


def test_viewer_may_read_but_not_patch(viewer):
    assert viewer.get(ETH)[0] == 200
    assert viewer.patch(ETH, default_config())[0] == 403


def test_viewer_may_not_restart_host(viewer):
    assert viewer.get(HOST)[0] == 200
    assert viewer.post(HOST_RESET, {"ResetType": "ForceRestart"})[0] == 403


def test_viewer_may_not_reboot_bmc(viewer):
    assert viewer.post(BMC + "/Actions/Manager.Reset", {"ResetType": "GracefulRestart"})[0] == 403


def test_viewer_may_read_events(viewer):
    assert viewer.get(EVENTS)[0] == 200


def test_untrusted_certificate_fails_tls_verification(lab):
    with pytest.raises((ssl.SSLCertVerificationError, URLError)):
        urlopen(lab["url"] + ROOT, timeout=3)


def test_correct_ca_validates_certificate_and_hostname(client):
    assert client.get(ROOT)[0] == 200


@pytest.mark.parametrize("garbage", [b"{", b"[]", b"null", b'"str"', b"", b"{" + b"x" * 5000 + b"}"])
def test_malformed_or_oversized_json_rejected(client, garbage):
    assert client.post(HOST_RESET, garbage)[0] == 400


def test_wrong_content_type_does_not_change_host_state(client):
    initial = client.get(HOST)[1]
    code, _ = client.post(HOST_RESET, json.dumps({"ResetType": "ForceOff"}).encode(), content_type="text/plain")
    assert code == 400
    assert client.get(HOST)[1] == initial


def test_invalid_hostname_in_address_is_rejected(client):
    bad = default_config()
    bad["IPv4"]["Address"] = "127.0.0.1; rm -rf /"
    assert client.patch(ETH, bad)[0] == 400


def test_internal_host_agent_rejects_requests_without_secret(lab):
    if not lab["agent"]:
        pytest.skip("Host-agent direct connection intentionally blocked in Compose topology")
    from urllib.error import HTTPError
    with pytest.raises(HTTPError) as error:
        urlopen("http://" + lab["agent"] + "/internal/state", timeout=2)
    assert error.value.code == 403


def test_error_responses_never_reflect_authorization_token(lab):
    token = lab["admin"]
    client = BmcClient(lab["url"], lab["cert"], token)
    _, body = client.patch(ETH, {"garbage": token})
    assert token not in json.dumps(body)
