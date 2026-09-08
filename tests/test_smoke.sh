#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MLRT="$ROOT/bin/mlrt"

out="$($MLRT shell -c "printf hello | tr a-z A-Z")"
[[ "$out" == "HELLO" ]]

map_out="$($MLRT map "$MLRT")"
grep -q "Memory Mapping Plan" <<<"$map_out"

port=$((25000 + ($$ % 10000)))
server_log="$(mktemp)"
"$MLRT" server --port "$port" --once >"$server_log" 2>&1 &
server_pid=$!
cleanup() { kill "$server_pid" 2>/dev/null || true; rm -f "$server_log"; }
trap cleanup EXIT
for _ in $(seq 1 50); do
  if grep -q "listening" "$server_log"; then break; fi
  sleep 0.05
done
response="$($MLRT client --port "$port" -c "printf network-ok")"
grep -q "exit_status=0" <<<"$response"
grep -q "network-ok" <<<"$response"
wait "$server_pid"
trap - EXIT
rm -f "$server_log"

echo "test_smoke: PASS"
