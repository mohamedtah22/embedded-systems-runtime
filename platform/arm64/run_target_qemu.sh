#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

: "${QEMU_AARCH64:=qemu-aarch64}"
: "${AARCH64_SYSROOT:=/usr/aarch64-linux-gnu}"

if [[ ! -x ./bin/embedded-target-arm64 ]]; then
  make arm64-target
fi

exec "$QEMU_AARCH64" -L "$AARCH64_SYSROOT" ./bin/embedded-target-arm64 "$@"
