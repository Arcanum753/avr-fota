#!/usr/bin/env bash
# tests/core/core_terminal/run.sh
# Запускает все применимые тесты модуля core_terminal (L2).
# Exit code: 0 — зелено; 1 — фейлы; 2 — конфигурационная ошибка.

set -euo pipefail
MODULE="core_terminal"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
OUT="$ROOT/test_reports/core/$MODULE"
mkdir -p "$OUT"
exec bash "$ROOT/tests/scripts/run_module.sh" "$MODULE" "$ROOT" "$OUT"
