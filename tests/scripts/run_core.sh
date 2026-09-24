#!/usr/bin/env bash
# tests/scripts/run_core.sh [--only <module>] [--with-device] [--no-l3]
#
# Оркестратор тестов ядра: запускает модульные run.sh (L1/L2/L4), затем
# общий L3 (compile matrix) и формирует сводный отчёт.
#
#   --only <module>  — запустить только один модуль
#   --with-device    — требовать DEVICE_HOST (иначе exit 2)
#   --no-l3          — не запускать compile matrix
#
# Exit code: 0 — всё зелено; 1 — есть фейлы; 2 — конфигурационная ошибка.

set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SUMMARY="$ROOT/test_reports/core/_summary"
mkdir -p "$SUMMARY"

# pio/gcovr/pytest могут быть в ~/.local/bin (pipx) или ~/.platformio/penv/bin;
# неинтерактивный PATH (bash без login-shell) их не содержит.
for _d in "$HOME/.local/bin" "$HOME/.platformio/penv/bin"; do
    if [[ -d "$_d" ]]; then
        PATH="$_d:$PATH"
        export PATH
    fi
done

# python может называться python3 (Ubuntu 24.04+).
PY=""
if command -v python3 > /dev/null 2>&1; then
    PY="python3"
elif command -v python > /dev/null 2>&1; then
    PY="python"
fi

MODULES=(
  common
  core_web
  core_sys
  core_wifi
  core_ntp
  core_ota
  core_json
  core_led
  core_terminal
  core_state
  core_task
)

WITH_L3=1
WITH_DEVICE=0
ONLY=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --only) shift; ONLY="${1:-}" ;;
        --with-device) WITH_DEVICE=1 ;;
        --with-l3) WITH_L3=1 ;;
        --no-l3) WITH_L3=0 ;;
        *) echo "[orchestrator] unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

if [[ -n "$ONLY" ]]; then
    found=0
    for m in "${MODULES[@]}"; do
        [[ "$m" == "$ONLY" ]] && found=1
    done
    if [[ $found -eq 0 ]]; then
        echo "[orchestrator] unknown module: $ONLY" >&2
        exit 2
    fi
fi

if [[ $WITH_DEVICE -eq 1 && -z "${DEVICE_HOST:-}" ]]; then
    echo "--with-device requires DEVICE_HOST" >&2
    exit 2
fi

declare -A results
fail=0

for m in "${MODULES[@]}"; do
    if [[ -n "$ONLY" && "$m" != "$ONLY" ]]; then continue; fi
    script="$ROOT/tests/core/$m/run.sh"
    if [[ ! -f "$script" ]]; then
        echo "[orchestrator] SKIP $m (no run.sh)"
        results[$m]="skip"
        continue
    fi
    if bash "$script"; then
        results[$m]="ok"
    else
        results[$m]="fail"
        fail=1
    fi
done

if [[ $WITH_L3 -eq 1 && -z "$ONLY" ]]; then
    if [[ -z "$PY" ]]; then
        echo "[orchestrator] python not found - L3 skipped" >&2
        results[l3]="fail"
        fail=1
    elif "$PY" "$ROOT/tests/compile_matrix/run_matrix.py" \
            --root "$ROOT" --out "$SUMMARY/l3_junit.xml"; then
        results[l3]="ok"
    else
        results[l3]="fail"
        fail=1
    fi
fi

if [[ -n "$PY" && -f "$ROOT/tests/scripts/gen_report.py" ]]; then
    "$PY" "$ROOT/tests/scripts/gen_report.py" \
        --in "$ROOT/test_reports/core" \
        --out "$SUMMARY/report.html" || true
fi

printf '\n=== Core test summary ===\n'
for m in "${MODULES[@]}"; do
    if [[ -n "$ONLY" && "$m" != "$ONLY" ]]; then continue; fi
    printf '%-16s %s\n' "$m" "${results[$m]:-n/a}"
done
if [[ -n "${results[l3]:-}" ]]; then printf '%-16s %s\n' l3 "${results[l3]}"; fi

exit $fail
