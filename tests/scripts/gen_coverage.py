#!/usr/bin/env python3
"""tests/scripts/gen_coverage.py

Необязательный шаг покрытия для модуля ядра. Если установлен gcovr и есть
gcda-данные (сборка с --coverage) — пишет coverage.md в test_reports/core/<module>/.
Для common/core_ota/core_led дополнительно проверяется порог 80% (exit 2).

Зависимости — stdlib + опциональный gcovr.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

THRESHOLD_MODULES = {"common", "core_ota", "core_led"}
THRESHOLD = 80.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", required=True)
    ap.add_argument("--root", required=True)
    args = ap.parse_args()

    out_dir = os.path.join(args.root, "test_reports", "core", args.module)
    os.makedirs(out_dir, exist_ok=True)
    out_md = os.path.join(out_dir, "coverage.md")

    gcovr = shutil.which("gcovr")
    if not gcovr:
        with open(out_md, "w", encoding="utf-8") as f:
            f.write("# Coverage: {}\n\n`.gcovr` не найден — покрытие skipped.\n".format(args.module))
        if args.module in THRESHOLD_MODULES:
            # Для THRESHOLD_MODULES отсутствие gcovr — ошибка: гейт не проверить.
            print("[gen_coverage] gcovr не найден — гейт покрытия не проверен", file=sys.stderr)
            return 2
        print("[gen_coverage] gcovr не найден — skipped")
        return 0

    filt = os.path.join(args.root, "src", args.module)
    cmd = [gcovr, "--root", args.root, "--filter", filt,
           "--txt-metric", "line", "--txt", "--gcov-ignore-parse-errors"]
    try:
        res = subprocess.run(cmd, cwd=os.path.join(args.root, "tests", "core", args.module),
                             capture_output=True, text=True, timeout=300)
    except Exception as e:  # noqa: BLE001
        res = None
        err = str(e)

    strict = args.module in THRESHOLD_MODULES

    if res is None or res.returncode != 0:
        with open(out_md, "w", encoding="utf-8") as f:
            f.write("# Coverage: {}\n\nДанные покрытия недоступны (нет gcda или ошибка gcovr).\n"
                    .format(args.module))
        print("[gen_coverage] нет данных покрытия для {}".format(args.module))
        # Для THRESHOLD_MODULES отсутствие данных — ошибка (гейт не проверить).
        return 2 if strict else 0

    text = res.stdout
    percent = None
    for line in text.splitlines():
        m = re.search(r"TOTAL\s+.*?(\d+(?:\.\d+)?)%", line)
        if m:
            percent = float(m.group(1))

    with open(out_md, "w", encoding="utf-8") as f:
        f.write("# Coverage: {}\n\n".format(args.module))
        if percent is not None:
            f.write("Итоговое линейное покрытие: **{:.1f}%**\n\n".format(percent))
        f.write("```\n{}\n```\n".format(text.strip()))

    if percent is None:
        print("[gen_coverage] {}: gcovr отработал, но строки TOTAL нет".format(args.module),
              file=sys.stderr)
        return 2 if strict else 0

    print("[gen_coverage] {}: {:.1f}%".format(args.module, percent))

    if strict and percent < THRESHOLD:
        print("[gen_coverage] {} покрытие ниже {:.0f}%".format(args.module, THRESHOLD),
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
