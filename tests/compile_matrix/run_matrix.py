#!/usr/bin/env python3
"""tests/compile_matrix/run_matrix.py

L3 — общий compile matrix ядра (без модулей/устройств):
  * сборка обязательных и опциональных env из envs.txt;
  * парсинг RAM/Flash/FS и сверка с size_limits.json (запас +5% задаётся порогом);
  * AC-19: modules_registry.cpp без #if и без module_/device_ вызовов;
  * AC-22: негативный тест negative_nocore/ должен падать.

Если Linux-тулчейнов ESP в ~/.platformio/packages нет (например, локальный WSL
без возможности скачать пакеты), сборка обязательных env помечается явным
`skip` с причиной, а не `fail` и не молчаливым `pass`: L3 в этом случае
валидируется в CI (ubuntu). AC-19/AC-22 не требуют тулчейнов и проверяются всегда.

Результат — JUnit XML (--out). Запускается только оркестратором.
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


def _bootstrap_pio_path():
    """Локальный WSL: pio в ~/.local/bin (pipx) или ~/.platformio/penv/bin."""
    for d in (os.path.join(os.path.expanduser("~"), ".local", "bin"),
              os.path.join(os.path.expanduser("~"), ".platformio", "penv", "bin")):
        if os.path.isdir(d):
            cur = os.environ.get("PATH", "")
            if d not in cur.split(os.pathsep):
                os.environ["PATH"] = d + os.pathsep + cur


_bootstrap_pio_path()


def read_env_list(path):
    required, optional = [], []
    section = "required"
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            if s.startswith("#"):
                if "опциональ" in s.lower():
                    section = "optional"
                continue
            env = s.split()[0]
            if section == "required":
                required.append(env)
            else:
                optional.append(env)
    return required, optional


def discover_envs(root):
    envs = set()
    try:
        res = subprocess.run(["pio", "project", "config", "--json-output"],
                             cwd=root, capture_output=True, text=True, timeout=180)
        if res.returncode == 0 and res.stdout.strip():
            data = json.loads(res.stdout)
            for item in data:
                if isinstance(item, list) and len(item) == 2:
                    name = item[0]
                    if isinstance(name, str) and name.startswith("env:"):
                        envs.add(name.split(":", 1)[1])
    except Exception:
        pass
    if envs:
        return envs
    # Fallback: regex по ini-файлам.
    patterns = [
        os.path.join(root, "platformio.ini"),
        os.path.join(root, "targets", "*.ini"),
    ]
    for pat in patterns:
        for path in glob.glob(pat):
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                for m in re.finditer(r"\[env:([^\]]+)\]", f.read()):
                    envs.add(m.group(1).strip())
    return envs


def toolchains_available():
    """Есть ли в ~/.platformio/packages хотя бы один Linux-тулчейн.

    Без него сборка esp32/esp8266 невозможна; это не ошибка кода, поэтому
    соответствующие env помечаются skip, а не fail.
    """
    pkg = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
    if not os.path.isdir(pkg):
        return False
    try:
        return any(n.startswith("toolchain-") for n in os.listdir(pkg))
    except OSError:
        return False


def build_env(root, env, timeout=1800):
    # Без -s: иначе PlatformIO может не напечатать RAM/Flash и гейт размеров
    # молча не сработает.
    cmd = ["pio", "run", "-e", env]
    try:
        res = subprocess.run(cmd, cwd=root, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return False, "timeout", ""
    out = (res.stdout or "") + "\n" + (res.stderr or "")
    return res.returncode == 0, out, ""


def parse_percent(out, label):
    m = re.search(label + r":\s+\[[^\]]*\]\s+([\d.]+)%", out)
    return float(m.group(1)) if m else None


def check_registry(root):
    """AC-19. Возвращает (True|False|None, сообщение); None — проверить нельзя."""
    path = os.path.join(root, "src", "modules_registry.cpp")
    if not os.path.exists(path):
        return None, "modules_registry.cpp не сгенерирован (нет сборок)"
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    text = "".join(lines)
    if "#if" in text:
        return False, "modules_registry.cpp содержит #if (AC-19)"
    # Комментарии не считаем кодом (в шапке есть module_registry_gen.py).
    code = "\n".join(l for l in lines if not l.lstrip().startswith("//"))
    for token in ("module_", "device_"):
        if re.search(r"\b" + token + r"[A-Za-z0-9_]*\s*[.(]", code):
            return False, "modules_registry.cpp содержит вызовы {}* (AC-19)".format(token)
    return True, "ok"


def regenerate_registry(root, env):
    """Перегенерировать modules_registry.cpp под core-env.

    AC-19 проверяет чистоту registry (без module_/device_/`#if`), что имеет
    смысл только для сборки «ядро без внешних компонентов». На диске же может
    лежать stale-файл от сборки с модулями/устройством, поэтому перегенерируем
    под первый обязательный core-env (чистый Python, без тулчейнов ESP).
    """
    gen = os.path.join(root, "python", "module_registry_gen.py")
    if not os.path.exists(gen):
        return False
    try:
        res = subprocess.run([sys.executable, gen, "--env", env],
                             cwd=root, capture_output=True, text=True, timeout=120)
        return res.returncode == 0
    except Exception:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    required, optional = read_env_list(os.path.join(root, "tests", "compile_matrix", "envs.txt"))
    available = discover_envs(root)

    limits = {}
    limits_path = os.path.join(root, "tests", "compile_matrix", "size_limits.json")
    if os.path.exists(limits_path):
        try:
            limits = json.load(open(limits_path, "r", encoding="utf-8"))
        except Exception:
            limits = {}

    cases = []  # (name, status, message)

    no_toolchain = not toolchains_available()
    if no_toolchain:
        print("[l3] Linux-тулчейны не найдены — сборка ESP пропущена (L3 валидируется в CI)")

    for env in required + optional:
        is_required = env in required
        if env not in available:
            if is_required:
                cases.append(("build:" + env, "fail", "env не определён в конфиге"))
            else:
                cases.append(("build:" + env, "skip", "env не определён (опциональный)"))
            continue

        if no_toolchain:
            cases.append(("build:" + env, "skip", "нет Linux-тулчейнов (L3 в CI)"))
            continue

        ok, out, _ = build_env(root, env)
        if not ok:
            cases.append(("build:" + env, "fail", "сборка не удалась"))
            continue

        ram = parse_percent(out, "RAM")
        flash = parse_percent(out, "Flash")
        if ram is None and flash is None:
            # Не молчаливый pass: размеры не распарсились — проверка не выполнена.
            cases.append(("size:" + env, "skip", "размеры RAM/Flash не распарсены"))
            continue
        msg = "RAM={}% Flash={}%".format(ram, flash)
        lim = limits.get(env, {}) if isinstance(limits, dict) else {}
        if ram is not None and "ram_percent" in lim and ram > float(lim["ram_percent"]):
            cases.append(("size:" + env, "fail", "RAM {}% > {}".format(ram, lim["ram_percent"])))
        elif flash is not None and "flash_percent" in lim and flash > float(lim["flash_percent"]):
            cases.append(("size:" + env, "fail", "Flash {}% > {}".format(flash, lim["flash_percent"])))
        else:
            cases.append(("build:" + env, "pass", msg))

    # AC-19 — registry core-only env должен быть без module_/device_/#if.
    core_env = required[0] if required else "TestCore32"
    if regenerate_registry(root, core_env):
        ac19_ok, ac19_msg = check_registry(root)
        if ac19_ok is None:
            cases.append(("ac19-registry", "skip", ac19_msg))
        else:
            cases.append(("ac19-registry", "pass" if ac19_ok else "fail", ac19_msg))
    else:
        cases.append(("ac19-registry", "skip", "не удалось перегенерировать registry под core-env"))

    # AC-22 — негативная сборка модуля без ядра (platform = native, тулчейны не нужны).
    neg_dir = os.path.join(root, "tests", "compile_matrix", "negative_nocore")
    if os.path.isdir(neg_dir):
        try:
            res = subprocess.run(["pio", "run", "-e", "negative_nocore"],
                                 cwd=neg_dir, capture_output=True, text=True, timeout=600)
            if res.returncode != 0:
                cases.append(("ac22-negative-nocore", "pass", "сборка без ядра упала, как ожидается"))
            else:
                cases.append(("ac22-negative-nocore", "fail", "сборка без ядра неожиданно прошла"))
        except Exception as e:  # noqa: BLE001
            cases.append(("ac22-negative-nocore", "fail", "ошибка запуска: {}".format(e)))
    else:
        cases.append(("ac22-negative-nocore", "skip", "negative_nocore/ отсутствует"))

    # JUnit
    failures = sum(1 for _, s, _ in cases if s == "fail")
    suite = ET.Element("testsuite", {
        "name": "l3-compile-matrix",
        "tests": str(len(cases)),
        "failures": str(failures),
        "errors": "0",
        "skipped": str(sum(1 for _, s, _ in cases if s == "skip")),
    })
    for name, status, msg in cases:
        tc = ET.SubElement(suite, "testcase", {"classname": "compile_matrix", "name": name})
        if status == "fail":
            el = ET.SubElement(tc, "failure")
            el.text = msg
        elif status == "skip":
            ET.SubElement(tc, "skipped")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    ET.ElementTree(suite).write(args.out, encoding="utf-8", xml_declaration=True)

    skipped = sum(1 for _, s, _ in cases if s == "skip")
    print("[l3] проверок: {}, fail: {}, skip: {}".format(len(cases), failures, skipped))
    for name, status, msg in cases:
        print("  {:<28} {:<5} {}".format(name, status, msg))

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
