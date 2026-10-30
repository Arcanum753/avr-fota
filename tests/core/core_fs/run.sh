#!/usr/bin/env bash
# tests/core/core_fs/run.sh
# L1/L2 (native) + L4 (pytest при DEVICE_HOST).
set -euo pipefail
MODULE="core_fs"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$ROOT/test_reports/core/$MODULE"
mkdir -p "$OUT"
exec bash "$ROOT/tests/scripts/run_module.sh" "$MODULE" "$ROOT" "$OUT"
