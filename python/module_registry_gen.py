#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Генератор modules_registry.cpp/.h для avr-fota.

Ручной запуск под выбранный env:
    python python/module_registry_gen.py --env esp32_clock-mech

Если аргумент --env не задан, скрипт сам определяет "текущий" env:
  1. Переменная окружения PLATFORMIO_ENV (устанавливается PlatformIO IDE).
  2. .vscode/launch.json -> projectEnvName (env, выбранный в PlatformIO IDE).
  3. Иначе — первый env из списка [env:...] в конфигах.

Скрипт:
  1. Читает platformio.ini и все extra_configs.
  2. Для выбранного env собирает полный src_filter (с учётом интерполяции ${...}).
  3. По +<префикс>name/> определяет включённые модули (module_*, submodule_*, device_*).
  4. Для каждого включённого модуля читает секцию [registry] из его <имя>.ini.
  5. Формирует src/modules_registry.cpp и src/modules_registry.h.

ВАЖНО: результат привязан к ОДНОМУ выбранному env (файл без #if defined).
Перед сборкой другого env перезапустите скрипт с новым --env.
"""

import argparse
import configparser
import glob
import json
import os
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

PLATFORMIO_INI = "platformio.ini"
SRC_FOLDER = "src"
REGISTRY_SECTION = "registry"

MODULE_PREFIX = "module_"
SUBMODULE_PREFIX = "submodule_"
DEVICE_PREFIX = "device_"
CORE_PREFIX = "core_"


def log_info(msg: str):
    print(f"[registry_gen] {msg}")


def log_warning(msg: str):
    print(f"[registry_gen] WARNING: {msg}")


def log_error(msg: str):
    print(f"[registry_gen] ERROR: {msg}")


# ============================================================
# Парсинг ini с интерполяцией ${...}
# ============================================================

def load_ini_files(project_dir: Path) -> "configparser.ConfigParser":
    fp = configparser.ConfigParser(interpolation=None, delimiters=("=",))
    fp.optionxform = str
    fp.allow_no_value = True

    main_ini = project_dir / PLATFORMIO_INI
    if not main_ini.exists():
        log_error(f"{PLATFORMIO_INI} not found in {project_dir}")
        sys.exit(1)

    to_read = [main_ini]

    # Извлекаем extra_configs
    probe = configparser.ConfigParser(interpolation=None, delimiters=("=",))
    probe.optionxform = str
    probe.read(main_ini, encoding="utf-8")
    if probe.has_option("platformio", "extra_configs"):
        for line in probe.get("platformio", "extra_configs").splitlines():
            line = line.strip()
            if line and not line.startswith(";"):
                p = Path(line)
                if not p.is_absolute():
                    p = project_dir / p
                # Поддержка glob-масок в extra_configs (напр. src/*/*.ini, src/*/*/*.ini).
                if any(ch in line for ch in "*?["):
                    for match in sorted(glob.glob(str(p))):
                        m = Path(match).resolve()
                        if m.exists() and m not in to_read:
                            to_read.append(m)
                else:
                    p = p.resolve()
                    if p.exists() and p not in to_read:
                        to_read.append(p)

    for f in to_read:
        try:
            fp.read(f, encoding="utf-8")
        except Exception as e:
            log_error(f"Failed to read {f}: {e}")
            sys.exit(1)
    return fp


def resolve_option(cp: "configparser.ConfigParser", section: str, option: str,
                   stack: Optional[List[str]] = None) -> str:
    """Рекурсивно подставляет ${...} (вкл. ${env:xxx.key}). stack — защита от циклов."""
    if stack is None:
        stack = []
    key_marker = f"{section}.{option}"
    if key_marker in stack:
        log_error(f"Circular interpolation: {' -> '.join(stack + [key_marker])}")
        return ""
    stack = stack + [key_marker]

    if not cp.has_section(section) or not cp.has_option(section, option):
        return ""
    raw = cp.get(section, option)
    if raw is None:
        return ""

    def repl(match: "re.Match") -> str:
        ref = match.group(1)
        if "." not in ref:
            return match.group(0)
        name, opt = ref.split(".", 1)
        # ${env:xxx.opt} -> секция [env:xxx]; ${sec.opt} -> секция [sec]
        return resolve_option(cp, name, opt, stack)

    return re.sub(r"\$\{([^}]+)\}", repl, raw)


def resolve_src_filter(cp: "configparser.ConfigParser", env_name: str) -> str:
    section = f"env:{env_name}"
    if cp.has_option(section, "src_filter"):
        return (resolve_option(cp, section, "src_filter") or "").strip()
    if cp.has_option(section, "extends"):
        ext = (resolve_option(cp, section, "extends") or "").strip()
        if ext.startswith("env:"):
            ext = ext[4:]
        if ext and ext != env_name:
            return resolve_src_filter(cp, ext)
    return ""


def resolve_build_flags(cp: "configparser.ConfigParser", env_name: str) -> str:
    section = f"env:{env_name}"
    if cp.has_option(section, "build_flags"):
        return (resolve_option(cp, section, "build_flags") or "").strip()
    if cp.has_option(section, "extends"):
        ext = (resolve_option(cp, section, "extends") or "").strip()
        if ext.startswith("env:"):
            ext = ext[4:]
        if ext and ext != env_name:
            return resolve_build_flags(cp, ext)
    return ""


def get_all_env_names(cp: "configparser.ConfigParser") -> List[str]:
    envs = []
    for sec in cp.sections():
        if sec.startswith("env:") and sec != "env":
            envs.append(sec[len("env:"):])
    return envs


def parse_src_filter(src_filter: str) -> List[Tuple[str, str]]:
    """Возвращает список (имя_компонента, rel_путь_папки_от src).

    Компонент — папка с префиксом module_/submodule_/device_. Путь может быть
    вложенным (например +<module_program/submodule_swd/>): последний сегмент
    токена — имя компонента, предыдущие — контейнерный путь внутри src/.
    """
    components = set()
    for token in re.findall(r"\+<([^>]+)>", src_filter or ""):
        parts = [s for s in token.split("/") if s]
        if not parts:
            continue
        name = parts[-1]
        prefix_ok = (name.startswith(MODULE_PREFIX)
                     or name.startswith(SUBMODULE_PREFIX)
                     or name.startswith(DEVICE_PREFIX))
        if not prefix_ok:
            continue
        rel_dir = "/".join(parts)   # напр. "module_udp" или "module_program/submodule_swd"
        components.add((name, rel_dir))
    return sorted(components)


def read_registry_ini(project_dir: Path, module_name: str,
                      rel_dir: Optional[str] = None) -> Optional[Dict[str, str]]:
    """Читает секцию [registry] из src/<rel_dir>/<module_name>.ini.

    rel_dir — папка компонента от src/ (может быть вложенной, напр.
    "module_program/submodule_swd"); если не задана, используется module_name.
    """
    rel = rel_dir or module_name
    ini = project_dir / SRC_FOLDER / rel / f"{module_name}.ini"
    if not ini.exists():
        return None
    cp = configparser.ConfigParser(interpolation=None, delimiters=("=",))
    cp.optionxform = str
    cp.read(ini, encoding="utf-8")
    if not cp.has_section(REGISTRY_SECTION):
        return None
    d: Dict[str, str] = {}
    for opt in cp.options(REGISTRY_SECTION):
        val = cp.get(REGISTRY_SECTION, opt, raw=True)
        d[opt] = val.strip() if val else ""
    return d


def detect_current_env(project_dir: Path, env_names: List[str]) -> Optional[str]:
    """
    Определяет "текущий" env:
      1. Переменная окружения PLATFORMIO_ENV (PlatformIO IDE).
      2. Последний собранный env в .pio/build/ (по времени модификации) —
         надёжнее, чем launch.json, который не всегда отражает выбор в IDE.
      3. .vscode/launch.json -> projectEnvName.
    Возвращает None, если не удалось определить.
    """
    # 1. Переменная окружения PLATFORMIO_ENV
    from_env = os.environ.get("PLATFORMIO_ENV")
    if from_env:
        if from_env in env_names:
            return from_env
        log_warning(f"PLATFORMIO_ENV '{from_env}' not in env list — ignored")

    # 2. Последний собранный env в .pio/build/ (по mtime каталога)
    build_dir = project_dir / ".pio" / "build"
    if build_dir.exists():
        built = []
        for entry in build_dir.iterdir():
            if entry.is_dir() and entry.name in env_names:
                try:
                    built.append((entry.stat().st_mtime, entry.name))
                except OSError:
                    pass
        if built:
            built.sort(reverse=True)
            return built[0][1]

    # 3. .vscode/launch.json -> projectEnvName
    launch = project_dir / ".vscode" / "launch.json"
    if launch.exists():
        try:
            # Убираем однострочные //-комментарии (launch.json генерируется с ними),
            # затем парсим как JSON.
            lines = []
            for line in launch.read_text(encoding="utf-8").splitlines():
                stripped = line.strip()
                if stripped.startswith("//"):
                    continue
                lines.append(line)
            data = json.loads("\n".join(lines))
            configs = data.get("configurations", [])
            for cfg in configs:
                env_cfg = cfg.get("projectEnvName")
                if env_cfg:
                    if env_cfg in env_names:
                        return env_cfg
                    log_warning(f"launch.json projectEnvName '{env_cfg}' not in env list — ignored")
        except Exception as e:
            log_warning(f"Cannot parse {launch}: {e}")

    return None


# ============================================================
# Генерация файлов
# ============================================================

def h_content() -> str:
    return (
        "#ifndef _MODULES_REGISTRY_h\n"
        "#define _MODULES_REGISTRY_h\n"
        "\n"
        "// Файл генерируется python/module_registry_gen.py под выбранный env.\n"
        "// Не редактировать вручную. При смене env — перезапустить генератор.\n"
        "\n"
        '#include "mod_context.h"\n'
        "\n"
        "void core_begin(ModContext& ctx);\n"
        "void modules_begin(ModContext& ctx);\n"
        "void dev_begin(ModContext& ctx);\n"
        "\n"
        "void core_register_resources();\n"
        "void modules_register_resources();\n"
        "void dev_register_resources();\n"
        "\n"
        "void core_web_Init();\n"
        "void modules_web_Init();\n"
        "void dev_web_Init();\n"
        "\n"
        "void core_loop();\n"
        "void modules_loop();\n"
        "void dev_loop();\n"
        "\n"
        "#endif // _MODULES_REGISTRY_h\n"
    )


def cpp_content(env_name: str,
                includes: List[str],
                core_begin: List[str], modules_begin: List[str], dev_begin: List[str],
                core_res: List[str], modules_res: List[str], dev_res: List[str],
                core_web: List[str], modules_web: List[str], dev_web: List[str],
                core_loop: List[str], modules_loop: List[str], dev_loop: List[str]) -> str:
    L: List[str] = []
    L.append('#include "modules_registry.h"')
    L.append('// core_web/FSWebServerLib.h включается первым: предоставляет AsyncWebServerRequest,')
    L.append('// класс AsyncFSWebServer (ESPHTTPServer) и Arduino-типы для всех заголовков модулей.')
    L.append('#include "core_web/FSWebServerLib.h"')
    L.append("")
    L.append("// Файл генерируется python/module_registry_gen.py под выбранный env.")
    L.append(f"// Сгенерировано для env: {env_name}")
    L.append("// Не редактировать вручную. При смене env — перезапустить генератор.")
    L.append("")
    for inc in includes:
        L.append(f"#include {inc}")
    if includes:
        L.append("")

    L.append("// Определение глобального контекста приложения (extern из mod_context.h).")
    L.append("ModContext g_ctx;")
    L.append("")

    def func(name: str, calls: List[str]) -> None:
        L.append(f"void {name}(ModContext& ctx) {{" if "begin" in name else f"void {name}() {{")
        for c in calls:
            L.append(f"    {c}")
        L.append("}")
        L.append("")

    func("core_begin", core_begin)
    func("modules_begin", modules_begin)
    func("dev_begin", dev_begin)
    func("core_register_resources", core_res)
    func("modules_register_resources", modules_res)
    func("dev_register_resources", dev_res)
    func("core_web_Init", core_web)
    func("modules_web_Init", modules_web)
    func("dev_web_Init", dev_web)
    func("core_loop", core_loop)
    func("modules_loop", modules_loop)
    func("dev_loop", dev_loop)

    return "\n".join(L)


# ============================================================
# Основная логика
# ============================================================

def main():
    parser = argparse.ArgumentParser(description="Генератор modules_registry для avr-fota")
    parser.add_argument("--env", dest="env", type=str, default=None,
                        help="Имя env (например esp32_clock-mech). Если не задан — "
                             "используется текущий выбранный env (PLATFORMIO_ENV / launch.json) "
                             "или первый из списка.")
    args = parser.parse_args()

    project_dir = Path(os.getcwd()).resolve()
    if project_dir.name == "python":
        project_dir = project_dir.parent
    # Поднимаемся до директории с platformio.ini
    while not (project_dir / PLATFORMIO_INI).exists() and project_dir.parent != project_dir:
        project_dir = project_dir.parent
    if not (project_dir / PLATFORMIO_INI).exists():
        log_error(f"Cannot find {PLATFORMIO_INI}")
        sys.exit(1)
    os.chdir(project_dir)

    cp = load_ini_files(project_dir)
    env_names = get_all_env_names(cp)
    if not env_names:
        log_error("No [env:...] sections found")
        sys.exit(1)

    if args.env:
        if args.env not in env_names:
            log_error(f"Env '{args.env}' not found. Available: {', '.join(env_names)}")
            sys.exit(1)
        env_name = args.env
        log_info(f"Env from --env: {env_name}")
    else:
        env_name = detect_current_env(project_dir, env_names)
        if env_name is None:
            env_name = env_names[0]
            log_warning(f"Current env not detected — using first: {env_name}")
        else:
            log_info(f"Current env detected: {env_name}")

    log_info(f"Selected env: {env_name}")

    src_filter = resolve_src_filter(cp, env_name)
    build_flags = resolve_build_flags(cp, env_name)
    included = parse_src_filter(src_filter)
    is_otaclient = "MODULE_OTACLIENT" in build_flags

    log_info(f"src_filter: {src_filter}")
    log_info(f"included modules: {included}")
    log_info(f"otaclient: {is_otaclient}")

    # ---- Ядра ----
    includes: List[str] = [
        '"core_sys/core_sys.h"',
        '"core_wifi/core_wifi.h"',
        '"core_ntp/core_ntp.h"',
        '"core_json/core_json.h"',
        '"core_ota/core_ota.h"',
        '"core_terminal/core_terminal.h"',
        '"core_state/core_state.h"',
        '"core_task/core_task.h"',
    ]
    core_begin = [
        # core_json должен инициализироваться первым: его _fs используется
        # core_sys/core_wifi/core_ntp (загрузка конфигов) и всем остальным begin(ctx).
        "core_json.begin(ctx);",
        # core_sys идёт сразу после core_json: загружает identity/auth и заполняет
        # ctx.hostname / ctx.password до core_wifi и mDNS.
        "core_sys.begin(ctx);",
        # Шина и задачи инициализируются до регистрации ресурсов.
        "core_state.begin(ctx);",
        "core_task.begin(ctx);",
        # Регистрация ресурсов ядра/модулей/устройств до begin() периферии.
        "core_register_resources();",
        "core_wifi.begin(ctx);",
        "core_ntp.begin(ctx);",
        # Терминал без класса: базовые команды регистрируются здесь,
        # слоты модулей применяются лениво в первом вызове TerminalLoop().
        "TerminalInit();",
    ]
    core_web = [
        "core_sys.web_Init();",
        "core_wifi.web_Init();",
        "core_ntp.web_Init();",
        "core_json.web_Init();",
        "core_state.web_Init();",
    ]
    core_loop: List[str] = [
        # Шина раздаёт события и таймауты до обработки остальных задач.
        "core_state.loop();",
        "core_task.loop();",
        # Терминал читает сериал первым в цикле; первый вызов также применяет
        # слоты модулей (TerminalRegisterModule).
        "TerminalLoop();",
    ]

    if is_otaclient:
        # module_otaclient — OTA-клиент; заголовок подключается через регистрируемые модули.
        core_begin.append("module_otaclient.begin(ctx);")
        core_web.append("module_otaclient.web_Init();")
        core_loop.append("module_otaclient.loop();")
    else:
        core_begin.append("core_ota.begin(ctx);")
        core_web.append("core_ota.web_Init();")
        core_loop.append("core_ota.loop();")

    # ---- Ресурсы ядровых ----
    def core_registry(core_name: str, default_ns: str, default_res: int):
        reg = read_registry_ini(project_dir, core_name, core_name)
        if reg is None:
            return default_ns, default_res, 0, -1
        ns = reg.get("namespace", default_ns).strip() or default_ns
        res = 1 if reg.get("res", "").strip() == "1" else default_res
        priv = 1 if reg.get("priv", "").strip() == "1" else 0
        try:
            prio = int(reg.get("prio", "-1").strip() or "-1")
        except ValueError:
            prio = -1
        return ns, res, priv, prio

    core_res: List[str] = []
    for cname, dns, dres in [
        ("core_sys", "system", 1),
        ("core_state", "system", 1),
        ("core_wifi", "wifi", 1),
        ("core_ntp", "time", 1),
        ("core_ota", "ota", 1),
    ]:
        ns, res, priv, prio = core_registry(cname, dns, dres)
        if not res:
            continue
        core_res.append(f'core_state.setNamespace("{ns}");')
        # Ядро всегда привилегированное.
        core_res.append("core_state.setPrivileged(true);")
        core_res.append(f"core_state.setModulePrio({prio});")
        core_res.append(f"{cname}.register_resources();")
    if core_res:
        core_res.append("core_state.clearNamespace();")

    # ---- Модули / субмодули / устройства ----
    modules_begin: List[str] = []
    modules_web: List[str] = []
    modules_loop: List[str] = []
    dev_begin: List[str] = []
    dev_web: List[str] = []
    dev_loop: List[str] = []
    modules_res_entries: List[Tuple[int, int, str, str, int]] = []
    dev_res_entries: List[Tuple[int, int, str, str, int]] = []
    res_seq = 0
    seen_includes = set(includes)

    for mod, rel_dir in included:
        ini_path = project_dir / SRC_FOLDER / rel_dir / f"{mod}.ini"
        if not ini_path.exists():
            # Могут быть модули без ini (например module_prog) — у них нет [registry], пропускаем.
            log_warning(f"Module {mod}: {ini_path} not found — skipping")
            continue
        reg = read_registry_ini(project_dir, mod, rel_dir)
        if reg is None:
            log_warning(f"Module {mod}: [registry] section not found — skipping")
            continue
        obj = reg.get("object", "").strip()
        if not obj:
            log_warning(f"Module {mod}: registry.object empty — skipping")
            continue
        web_flag = reg.get("web", "0").strip() == "1"
        loop_flag = reg.get("loop", "0").strip() == "1"
        res_flag = reg.get("res", "0").strip() == "1"
        ns = reg.get("namespace", "").strip()
        priv = 1 if reg.get("priv", "").strip() == "1" else 0
        try:
            prio = int(reg.get("prio", "50").strip() or "50")
        except ValueError:
            prio = 50

        hdr = f'"{rel_dir}/{mod}.h"'
        if hdr not in seen_includes:
            seen_includes.add(hdr)
            includes.append(hdr)

        is_device = mod.startswith(DEVICE_PREFIX)

        if res_flag:
            entry = (prio, res_seq, obj, ns, priv)
            res_seq += 1
            if is_device:
                dev_res_entries.append(entry)
            else:
                modules_res_entries.append(entry)

        if mod == "module_otaclient" and is_otaclient:
            # OTA-клиент уже обработан в core-группах — не дублируем в modules.
            continue

        if is_device:
            dev_begin.append(f"{obj}.begin(ctx);")
            if web_flag:
                dev_web.append(f"{obj}.web_Init();")
            if loop_flag:
                dev_loop.append(f"{obj}.loop();")
        else:
            if mod == "module_udp":
                # begin() UDP вызывается из core_wifi при подключении — в registry не дублируем.
                pass
            else:
                modules_begin.append(f"{obj}.begin(ctx);")
            if web_flag:
                modules_web.append(f"{obj}.web_Init();")
            if loop_flag:
                modules_loop.append(f"{obj}.loop();")

    # Регистрация ресурсов идёт до begin() периферии соответствующей группы.
    modules_begin.insert(0, "modules_register_resources();")
    dev_begin.insert(0, "dev_register_resources();")

    def build_res_calls(entries) -> List[str]:
        # prio: больше = важнее; при равенстве сохраняется исходный порядок (FCFS).
        ordered = sorted(entries, key=lambda e: (-e[0], e[1]))
        calls: List[str] = []
        for prio, seq, obj, ns, priv in ordered:
            if ns:
                calls.append(f'core_state.setNamespace("{ns}");')
            if priv:
                calls.append("core_state.setPrivileged(true);")
            calls.append(f"core_state.setModulePrio({prio});")
            calls.append(f"{obj}.register_resources();")
        if calls:
            calls.append("core_state.clearNamespace();")
        return calls

    modules_res = build_res_calls(modules_res_entries)
    dev_res = build_res_calls(dev_res_entries)

    h_out = project_dir / SRC_FOLDER / "modules_registry.h"
    cpp_out = project_dir / SRC_FOLDER / "modules_registry.cpp"

    write_file(h_out, h_content())
    write_file(cpp_out, cpp_content(
        env_name,
        includes,
        core_begin, modules_begin, dev_begin,
        core_res, modules_res, dev_res,
        core_web, modules_web, dev_web,
        core_loop, modules_loop, dev_loop,
    ))

    log_info("Done. Generated:")
    log_info(f"  {h_out}")
    log_info(f"  {cpp_out}")
    log_info("ВАЖНО: файл привязан к выбранному env. При смене env перезапустите скрипт.")


def write_file(path: Path, content: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with open(path, "w", encoding="utf-8") as f:
            f.write(content)
        log_info(f"Written: {path}")
    except Exception as e:
        log_error(f"Failed to write {path}: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
