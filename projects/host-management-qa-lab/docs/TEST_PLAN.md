# Verification and QA test plan

## System under test (SUT)

Two independent Python services: a TLS-only, Redfish-*inspired* management-controller HTTP API and an internal host-power agent. This is an educational implementation, not standards-compliant Redfish firmware and **not an NVIDIA BMC, NIC or physical network**.

## Requirements traceability

| QA objective | Implemented artifact | Objective evidence |
| --- | --- | --- |
| Manual + automated functional testing | `tests/test_functional.py`, manual procedures below | Real HTTP requests, power state and boot counter assertions |
| Regression and CI | `.github/workflows/host-management-qa-lab.yml`, pytest | CI JUnit XML artifact; test suite must pass on every change |
| Networking and testbed topology | `compose.yaml`: QA -> BMC -> host-agent | Distinct `mgmt_net` and `host_net`, topology DNS isolation check |
| BMC/host-management standard concepts | Redfish-style service root, Manager, System, NIC, Reset, EventLog | Read/write and negative API tests |
| Security test coverage | `tests/test_security.py` | Verified TLS trust, unauthorized access, Viewer/Admin RBAC |
| Negative and boundary test design | parametrized malformed JSON, IPv4, VLAN, ResetType tests | Rejection codes and unchanged state following bad input |
| Reporting and debug | GitHub Actions JUnit XML, descriptive test IDs | Repro commands + bug template below |
| Basic performance smoke | 20 sequential requests, 32 concurrent reads | Generous CI reliability budget; **not hardware throughput** |

## Environment and topology

Local runner tests can run with no root, using `localhost:0` dynamic ports, ephemeral self-signed certificate and two Python subprocesses. The Docker Compose testbed puts the management QA runner on `mgmt_net` and the host agent on separate `host_net`; only the BMC is connected to both. `getent hosts host-agent` from the management QA runner must fail. A real management controller is **not** required.

## Entry and exit criteria

1. Service starts with a nonproduction TLS certificate and long test-only bearer tokens.
2. Functional, integration and security test suites pass; Docker topology-only isolation test executes within Compose.
3. No unknown exceptions, hangs, leaking child processes, or unhandled reset actions.
4. Negative requests return predictable error statuses; invalid NIC updates are atomic.
5. Release evidence is attached to CI as JUnit XML; blocking bugs are documented with reproducible input.

## Representative test cases

| ID | Feature | Steps | Expected result |
|---|---|---|---|
| QA-01 | Discovery | GET `/redfish/v1` with Viewer token | Manager/System links, HTTP 200 |
| QA-02 | Power cycle | Admin POST ForceOff, GET Host, POST On, GET Host | Off then On; BootCount incremented |
| QA-03 | BMC lifecycle | Admin POST Manager.Reset, GET Manager | Generation incremented; host power unaffected |
| QA-04 | Management NIC | PATCH valid IPv4 and VLAN; GET NIC | New config observed; EventLog entry |
| QA-05 | Invalid subnet | PATCH gateway outside IPv4 subnet | HTTP 400, no partial configuration update |
| QA-06 | VLAN limits | PATCH VLAN 0, 4095, non-integer | HTTP 400 for each |
| QA-07 | Least privilege | Viewer POST ForceRestart | HTTP 403, unchanged host state |
| QA-08 | TLS identity | HTTPS without trusted CA | Certificate verification failure |
| QA-09 | Invalid JSON | Truncated or oversized POST body | HTTP 400, service remains healthy |
| QA-10 | Isolation | Compose `qa-runner`: resolve `host-agent` | DNS lookup must fail; BMC still reachable |
| QA-11 | Concurrent requests | 32 reads via 8 worker threads | Every result consistent, HTTP 200 |
| QA-12 | Performance smoke | 20 authenticated read requests | Completed inside generous 15s CI budget |

## Four manual QA exercises (execute locally)

Start the two services in their documented environment (`README.md` has Docker and test-suite commands). Use a TLS-trusting client (curl `--cacert cert.pem`) and the configured bearer token.

**Manual 1 — positive management discovery**
```bash
curl --cacert certs/cert.pem -H "Authorization: Bearer $BMC_VIEWER_TOKEN" \
  https://localhost:8443/redfish/v1
```
Record response code 200, resource links and timestamp.

**Manual 2 — negative authorization**
```bash
curl --cacert certs/cert.pem -i -X POST \
  -H "Authorization: Bearer $BMC_VIEWER_TOKEN" \
  -H 'Content-Type: application/json' \
  --data '{"ResetType":"ForceRestart"}' \
  https://localhost:8443/redfish/v1/Systems/Host/Actions/ComputerSystem.Reset
```
Expect 403 and verify host state remains unchanged.

**Manual 3 — management NIC config**
```bash
curl --cacert certs/cert.pem -i -X PATCH \
  -H "Authorization: Bearer $BMC_ADMIN_TOKEN" \
  -H 'Content-Type: application/json' \
  --data '{"IPv4":{"Address":"10.20.30.22","SubnetMask":"255.255.255.0","Gateway":"10.20.30.1"},"VLANId":200}' \
  https://localhost:8443/redfish/v1/Managers/BMC/EthernetInterfaces/eth0
```
Expect 200, then GET to confirm; record EventLog evidence. This updates an **in-memory simulated configuration**, not a real Linux NIC.

**Manual 4 — TLS verification**
```bash
curl -i https://localhost:8443/redfish/v1
```
Expect certificate validation error unless the development test CA was explicitly trusted. Never use `curl -k` as proof of TLS validation.

## Bug report format

- **Title:** feature + observed failure
- **Build/commit SHA and testbed:** local TLS fixture / Compose, Python version and OS
- **Requirement/test ID:** e.g., QA-05
- **Steps:** full API method, endpoint and safe sanitized body
- **Expected vs. actual:** status, response and persisted state
- **Evidence:** pytest -vv output, redacted logs, response and artifact links
- **Severity:** justify impact, recovery and repeatability
- **Suspected component:** test oracle / model / HTTP handler / agent / network
- **Re-test:** exact command after patch and linked CI result

## Limitations

This suite does not implement real Redfish resource schemas, IPMI, SMBus, physical host resets, NIC driver functionality, VLAN tagging, production TLS PKI, authentication refresh, DoS protection, hardware throughput tests or real-world network-security certification. `Docker Compose` topology is a teaching environment, not equivalent to a switch or enterprise network. Tokens in Compose are *demo tokens*, **never real secrets**.
