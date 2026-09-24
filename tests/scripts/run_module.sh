#!/usr/bin/env bash
# tests/scripts/run_module.sh <module> <root> <out>
#
# Общая реализация контракта модульного скрипта (см. tests/core/<module>/run.sh):
# запускает все native-env, объявленные в tests/core/<module>/platformio.ini,
# затем (core_web) Node.js-тесты CVT и (при DEVICE_HOST) L4 pytest.
# L3 не запускается здесь — это общий шаг оркестратора.
#
# Exit code: 0 — зелено; 1 — фейлы; 2 — конфигурационная ошибка.

set -euo pipefail

MODULE="${1:?module required}"
ROOT="${2:?root required}"
OUT="${3:?out required}"
MODDIR="$ROOT/tests/core/$MODULE"

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

mkdir -p "$OUT"

rc=0
ran_anything=0

run_pio() {
    local env="$1"
    echo "=== [$MODULE] $env ==="
    if ( cd "$MODDIR" && pio test -e "$env" --junit-output-path "$OUT/${env}_junit.xml" ); then
        echo "[$MODULE] $env OK"
    else
        rc=1
    fi
}

if [[ -f "$MODDIR/platformio.ini" ]]; then
    if ! command -v pio > /dev/null 2>&1; then
        echo "[$MODULE] pio not found" >&2
        exit 2
    fi
    envs="$(grep -oE '^\[env:[^]]+\]' "$MODDIR/platformio.ini" | tr -d '\r' | sed -E 's/^\[env:([^]]+)\]$/\1/')"
    if [[ -z "$envs" ]]; then
        echo "[$MODULE] нет [env:*] в platformio.ini" >&2
        exit 2
    fi
    while IFS= read -r env; do
        [[ -z "$env" ]] && continue
        ran_anything=1
        run_pio "$env"
    done <<< "$envs"
fi

# core_web: CVT — JavaScript (см. §D8 плана), тестируется Node.js.
if [[ "$MODULE" == "core_web" && -d "$MODDIR/js" ]]; then
    ran_anything=1
    if command -v node > /dev/null 2>&1; then
        echo "=== [$MODULE] L1-JS (node --test) ==="
        if ( cd "$MODDIR" && node --test js/*.mjs ) > "$OUT/l1_js.log" 2>&1; then
            echo "[$MODULE] L1-JS OK"
        else
            cat "$OUT/l1_js.log"
            rc=1
        fi
    else
        echo "[$MODULE] Node.js not found - CVT JS tests skipped"
    fi
fi

# Покрытие — шаг проверки гейта (см. tests/scripts/gen_coverage.py).
# Для THRESHOLD_MODULES ненулевой код (нет данных / ниже порога) — фейл прогона.
if [[ -n "$PY" ]] && [[ -f "$ROOT/tests/scripts/gen_coverage.py" ]]; then
    ran_anything=1
    if ! "$PY" "$ROOT/tests/scripts/gen_coverage.py" --module "$MODULE" --root "$ROOT"; then
        echo "[$MODULE] coverage gate failed" >&2
        rc=1
    fi
fi

# L4 — HTTP API против реальной платы.
if [[ -d "$MODDIR/http_api" ]]; then
    ran_anything=1
    if [[ -z "${DEVICE_HOST:-}" ]]; then
        echo "[$MODULE] L4 skipped (no DEVICE_HOST)"
    elif ! command -v pytest > /dev/null 2>&1; then
        echo "[$MODULE] pytest not found" >&2
        exit 2
    else
        echo "=== [$MODULE] L4 (pytest) ==="
        if pytest "$MODDIR/http_api" --junitxml="$OUT/l4_junit.xml" > "$OUT/l4.log" 2>&1; then
            echo "[$MODULE] L4 OK"
        else
            cat "$OUT/l4.log"
            rc=1
        fi
    fi
fi

if [[ $ran_anything -eq 0 ]]; then
    echo "[$MODULE] нечего запускать (нет platformio.ini/js/http_api)" >&2
    exit 2
fi

if [[ $rc -eq 0 ]]; then
    echo "[$MODULE] OK"
else
    echo "[$MODULE] FAIL"
fi
exit $rc
