#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

./bin/mlrt serial-demo | grep -q 'CRC/framing check: OK'
./bin/mlrt rt-demo --duration 1 --period-ms 10 >/tmp/mlrt-rt-demo.out
grep -q 'Embedded Runtime Demo' /tmp/mlrt-rt-demo.out
./bin/mlrt event-demo --ticks 4 --period-ms 5 >/tmp/mlrt-event-demo.out
grep -q 'result: PASS' /tmp/mlrt-event-demo.out

payload="$(mktemp)"
image="$(mktemp)"
printf 'firmware-payload-123\n' > "$payload"
./bin/mlrt fw pack "$payload" "$image" --version 1.2.3 --arch arm64 >/tmp/mlrt-fw-pack.out
./bin/mlrt fw verify "$image" >/tmp/mlrt-fw-verify.out
grep -q 'status: VALID' /tmp/mlrt-fw-verify.out
./bin/mlrt fw check-update "$image" --current 1.0.0 >/tmp/mlrt-fw-policy.out
grep -q 'decision: ACCEPT' /tmp/mlrt-fw-policy.out
if ./bin/mlrt fw check-update "$image" --current 2.0.0 >/tmp/mlrt-fw-policy-reject.out; then
  echo 'expected anti-rollback rejection' >&2
  exit 1
fi
grep -q 'decision: REJECT' /tmp/mlrt-fw-policy-reject.out

sock="/tmp/mlrt-target-test-$$.sock"
./bin/mlrt target-sim --socket "$sock" --quiet >/tmp/mlrt-target.out 2>/tmp/mlrt-target.err &
pid=$!
cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  rm -f "$sock" "$payload" "$image"
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  [[ -S "$sock" ]] && break
  sleep 0.02
done
[[ -S "$sock" ]]

./bin/mlrt target status --socket "$sock" >/tmp/mlrt-status.out
grep -q 'state:               RUNNING' /tmp/mlrt-status.out
grep -q 'rate_hz:             100' /tmp/mlrt-status.out

./bin/mlrt target set-rate 50 --socket "$sock" | grep -q 'rate_hz=50'
./bin/mlrt target status --socket "$sock" | grep -q 'rate_hz:             50'
./bin/mlrt target fw-info --socket "$sock" | grep -q 'target-sim/1.0'
./bin/mlrt target inject-fault corrupt-crc --socket "$sock" >/dev/null
sleep 0.05
./bin/mlrt target status --socket "$sock" >/tmp/mlrt-status-crc.out
awk '/crc_errors:/ { if ($2+0 >= 1) ok=1 } END { exit ok?0:1 }' /tmp/mlrt-status-crc.out
./bin/mlrt target inject-fault freeze-worker --socket "$sock" >/dev/null
sleep 1.3
./bin/mlrt target status --socket "$sock" >/tmp/mlrt-status-after-fault.out
awk '/watchdog_recoveries:/ { if ($2+0 >= 1) ok=1 } END { exit ok?0:1 }' /tmp/mlrt-status-after-fault.out

./bin/embedded-target --self-test | grep -q 'protocol=PASS'

echo 'test_embedded: PASS'
