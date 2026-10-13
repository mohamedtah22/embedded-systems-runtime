#!/bin/sh
set -eu
# Run the demo in its own terminal. Never use its demo credentials anywhere else.
cd "$(dirname "$0")/.."
export HOST_AGENT_KEY='local-agent-demo-secret-2026'
export BMC_ADMIN_TOKEN='local-admin-demo-token-2026'
export BMC_VIEWER_TOKEN='local-viewer-demo-token-2026'
sh scripts/gen_dev_cert.sh certs localhost
python3 -m src.host_agent --port 8081 &
agent=$!
cleanup() {
    kill "$agent" "$bmc" 2>/dev/null || true
    wait "$agent" "$bmc" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
python3 -m src.bmc --port 8443 --agent-url http://127.0.0.1:8081 \
    --cert certs/cert.pem --key certs/key.pem &
bmc=$!
printf '\nLab ready on https://localhost:8443\n'
printf 'Use in another terminal:\n'
printf 'export BMC_ADMIN_TOKEN="local-admin-demo-token-2026"\n'
printf 'export BMC_VIEWER_TOKEN="local-viewer-demo-token-2026"\n'
printf 'curl --cacert certs/cert.pem -H "Authorization: Bearer $BMC_VIEWER_TOKEN" https://localhost:8443/redfish/v1\n'
printf 'Press Ctrl+C to stop both services.\n\n'
wait "$bmc"
