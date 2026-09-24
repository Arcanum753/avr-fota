#!/usr/bin/env bash
# tests/core/core_sys/run.sh
# Запускает все применимые тесты модуля core_sys (L1, L2, L4).
# Exit code: 0 — зелено; 1 — фейлы; 2 — конфигурационная ошибка.

set -euo pipefail
MODULE="core_sys"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$ROOT/test_reports/core/$MODULE"
mkdir -p "$OUT"
exec bash "$ROOT/tests/scripts/run_module.sh" "$MODULE" "$ROOT" "$OUT"
