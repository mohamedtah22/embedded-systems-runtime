# Example defect report — QA-05 (hypothetical training scenario)

**Status:** demonstration scenario only. The shipped code rejects this request correctly; this is **not** a claim that a real NVIDIA product or current lab build contains the defect.

**Title:** Manager EthernetInterface PATCH accepts an IPv4 gateway outside the configured subnet.

**Priority:** high for management reachability.

**Reproduction:** Set `Address=10.20.30.22`, `SubnetMask=255.255.255.0`, `Gateway=10.99.99.1`, `VLANId=200` and send an authenticated PATCH to `/redfish/v1/Managers/BMC/EthernetInterfaces/eth0`.

**Expected:** HTTP 400; GET returns the previous network configuration with **no partial update**.

**Hypothetical faulty behavior:** HTTP 200; gateway outside of local subnet incorrectly stored, potentially breaking management connectivity on a real host.

**Automated regression:** `python -m pytest -vv tests/test_functional.py -k reject_bad_ipv4`. This asserts both the error response and state immutability.

**Root-cause hypothesis:** input model validates the gateway as a syntactically valid IPv4 address without validating the address/subnet relationship.

**Fix verification:** validate via Python `ipaddress` membership checks before mutation; rerun the complete CI suite and preserve the JUnit XML artifact.
