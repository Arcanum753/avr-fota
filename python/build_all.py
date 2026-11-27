#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
build_all.py — сборка всех env проекта по циклу.

Для каждого env из всех конфигов:
  1. Имитирует выбор env в PlatformIO IDE: устанавливает PLATFORMIO_ENV=<env>.
  2. Запускает python/module_registry_gen.py БЕЗ аргументов (автоопределение env
     через PLATFORMIO_ENV — ровно как при запуске по кнопке "Run Python File").
  3. Запускает pio run -e <env>.

Использование:
    python python/build_all.py                  # собрать все env
    python python/build_all.py --env esp32-rgb  # собрать только один env
    python python/build_all.py --skip Test32_cOta --skip esp32-swd  # пропустить перечисленные
    python python/build_all.py --no-build       # только перегенерировать registry, без сборки
    python python/build_all.py --dry-run        # только показать план (не запускать)

Скрипт работает из любого каталога (корень проекта определяется сам).
Выход: 0 — все env собраны; 1 — есть ошибки (в конце выводится список неудачных).
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple

PROJECT = Path(__file__).resolve().parent.parent
GEN_SCRIPT = PROJECT / "python" / "module_registry_gen.py"


def get_env_names() -> List[str]:
    """Возвращает список всех [env:...] из platformio.ini + extra_configs."""
    sys.path.insert(0, str(PROJECT / "python"))
    import module_registry_gen as gen

    cp = gen.load_ini_files(PROJECT)
    envs = gen.get_all_env_names(cp)
    if not envs:
        print("[build_all] ERROR: no [env:...] sections found")
        sys.exit(1)
    return envs


def run_step(cmd: List[str], env: Optional[dict] = None) -> int:
    """Запускает команду, выводя поток в консоль. Возвращает returncode."""
    print(f"[build_all] RUN: {' '.join(cmd)}", flush=True)
    result = subprocess.run(cmd, cwd=str(PROJECT), env=env)
    return result.returncode


def build_one_env(env_name: str, no_build: bool) -> bool:
    """Полный цикл для одного env. Возвращает True при успехе."""
    print("=" * 70)
    print(f"CYCLE: select env={env_name}, run generator (no args), build")
    print("=" * 70, flush=True)

    # 1. Имитация выбора env в PlatformIO IDE
    gen_env = dict(os.environ)
    gen_env["PLATFORMIO_ENV"] = env_name

    # 2. Генератор БЕЗ аргументов (как по кнопке Run Python File)
    rc = run_step([sys.executable, str(GEN_SCRIPT)], env=gen_env)
    if rc != 0:
        print(f"[build_all] generator FAILED for {env_name}")
        return False

    # 3. Сборка (если не --no-build)
    if not no_build:
        rc = run_step(["pio", "run", "-e", env_name])
        if rc != 0:
            print(f"[build_all] BUILD FAILED for {env_name}")
            return False

    return True


def main():
    parser = argparse.ArgumentParser(description="Сборка всех env по циклу (генератор registry + pio run)")
    parser.add_argument("--env", dest="only_env", type=str, default=None,
                        help="Собрать только указанный env")
    parser.add_argument("--skip", dest="skip", type=str, action="append", default=[],
                        help="Пропустить env (можно указывать несколько раз)")
    parser.add_argument("--no-build", dest="no_build", action="store_true",
                        help="Только перегенерировать registry, без pio run")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true",
                        help="Показать план (какие env будут собраны), ничего не запускать")
    args = parser.parse_args()

    envs = get_env_names()

    if args.only_env:
        if args.only_env not in envs:
            print(f"[build_all] ERROR: env '{args.only_env}' not found. Available: {', '.join(envs)}")
            sys.exit(1)
        envs = [args.only_env]

    skip_set = set(args.skip)
    envs = [e for e in envs if e not in skip_set]

    if args.dry_run:
        print("[build_all] DRY-RUN. Envs to process:")
        for e in envs:
            print(f"  - {e}")
        sys.exit(0)

    if not envs:
        print("[build_all] ERROR: nothing to build (all envs skipped or empty list)")
        sys.exit(1)

    failed: List[Tuple[str, str]] = []
    total = len(envs)

    for i, env_name in enumerate(envs, start=1):
        print(f"[build_all] Progress: {i}/{total}")
        if not build_one_env(env_name, args.no_build):
            failed.append(env_name)

    print("\n" + "=" * 70)
    if failed:
        print(f"[build_all] FAILED ({len(failed)}/{total}): {failed}")
        sys.exit(1)
    else:
        print(f"[build_all] ALL {total} ENVS BUILT SUCCESSFULLY")
        sys.exit(0)


if __name__ == "__main__":
    main()
