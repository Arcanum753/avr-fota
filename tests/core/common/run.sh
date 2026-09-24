#!/usr/bin/env bash
# tests/core/common/run.sh
# Запускает все применимые тесты модуля common (L1). L3 — из оркестратора.
# Exit code: 0 — зелено; 1 — фейлы; 2 — конфигурационная ошибка.

set -euo pipefail
MODULE="common"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$ROOT/test_reports/core/$MODULE"
mkdir -p "$OUT"
exec bash "$ROOT/tests/scripts/run_module.sh" "$MODULE" "$ROOT" "$OUT"
