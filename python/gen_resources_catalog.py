#!/usr/bin/env python3
"""Генерация статического каталога ресурсов шины (core_state) в образ ФС.

Зачем: эндпоинты `/macros/resources` и `/state/catalog` собирают JSON на лету
(JsonDocument + копия тела ~11 КБ) — на грани heap и периодически дают 503/висят.
Каталог ресурсов статичен по составу (набор regState/regFunc/regEnum на сборку),
поэтому его можно сгенерировать на этапе сборки и положить файлом в ФС
(`/macros/resources.json`). Страница макросов грузит список из файла, а
актуальные значения подтягивает по клику (`/state/info`).

Источник данных — исходники компонентов, включённых в сборку:
  * ядровые `src/core_*` (компилируются всегда),
  * `module_*`/`submodule_*`/`device_*` из `src_filter` выбранного env.

Скрипт вызывается из `4_fs_builder.py` после копирования web-файлов модулей.
Может запускаться вручную: python python/gen_resources_catalog.py --env <env>
"""

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

SRC_FOLDER = "src"
CORE_PREFIX = "core_"
MODULE_PREFIX = "module_"
SUBMODULE_PREFIX = "submodule_"
DEVICE_PREFIX = "device_"
WEB_FOLDER_NAME = "web"
CATALOG_REL = ("macros", "resources.json")   # путь внутри образа ФС

KIND_MAP = {
    "BOOL": "BOOL",
    "I32": "I32",
    "F32": "F32",
    "STR": "STR",
    "TIME": "TIME",
    "ENUM": "ENUM",
}

# Регистрации ресурсов могут вызываться и как core_state.regXxx(...), и как bare regXxx(...).
_P = r"(?:\w+\s*\.\s*)?"
RE_SETNS = re.compile(_P + r'setNamespace\(\s*"([^"]+)"\s*\)')
RE_STATE = re.compile(
    _P + r'regState\(\s*"([^"]+)"\s*,\s*BusValue::(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(true|false)')
RE_STATEAS = re.compile(
    _P + r'regStateAs\(\s*"([^"]+)"\s*,\s*BusValue::(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*(true|false)')
RE_ENUM = re.compile(_P + r'regEnum\(\s*"([^"]+)"\s*,\s*([A-Za-z0-9_]+)\s*,\s*(\w+)\s*,\s*"((?:[^"\\]|\\.)*)"')
RE_FUNC = re.compile(_P + r'regFunc\(\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,\s*"((?:[^"\\]|\\.)*)"')
RE_EVENT = re.compile(_P + r'regEvent\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"')


def log(msg: str) -> None:
    print(f"[ResCatalog] {msg}")


def read_ini_namespace(ini_path: Path) -> Optional[str]:
    if not ini_path.exists():
        return None
    text = ini_path.read_text(encoding="utf-8", errors="ignore")
    in_registry = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            in_registry = (s.lower() == "[registry]")
            continue
        if in_registry and s.startswith("namespace"):
            _, _, v = s.partition("=")
            return v.strip()
    return None


def default_namespace(dir_name: str) -> str:
    name = dir_name
    for p in (MODULE_PREFIX, SUBMODULE_PREFIX, DEVICE_PREFIX, CORE_PREFIX):
        if name.startswith(p):
            name = name[len(p):]
            break
    return name.replace("-", "_")


def unescape(s: str) -> str:
    return s.replace('\\"', '"').replace("\\\\", "\\")


def parse_src_filter(src_filter: str) -> List[str]:
    """Относительные пути папок компонентов из src_filter (+<path/name/>)."""
    out: List[str] = []
    for token in re.findall(r"\+<([^>]+)>", src_filter or ""):
        parts = [p for p in token.split("/") if p]
        if not parts:
            continue
        name = parts[-1]
        if not (name.startswith(MODULE_PREFIX) or name.startswith(SUBMODULE_PREFIX)
                or name.startswith(DEVICE_PREFIX)):
            continue
        out.append("/".join(parts))
    return out


def enum_values(text: str, var: str) -> List[str]:
    m = re.search(r"\b" + re.escape(var) + r"\s*\[[^\]]*\]\s*=\s*\{([^}]*)\}", text, re.S)
    if not m:
        return []
    return re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))


def parse_component(src_root: Path, rel_dir: str) -> List[dict]:
    """Возвращает список ресурсов одного компонента (ns/name/full/kind/desc/rw/e/f/a/enum)."""
    comp_dir = src_root / rel_dir
    if not comp_dir.is_dir():
        return []
    dir_name = comp_dir.name
    ns = read_ini_namespace(comp_dir / f"{dir_name}.ini") or default_namespace(dir_name)

    out: List[dict] = []
    cpp_files = sorted(comp_dir.rglob("*.cpp"))
    texts = {cpp: cpp.read_text(encoding="utf-8", errors="ignore") for cpp in cpp_files}
    # Массивы значений ENUM могут быть определены в другом .cpp компонента —
    # ищем по всему объединённому тексту, а не только в файле регистрации.
    all_text = "\n".join(texts.values())
    for cpp in cpp_files:
        text = texts[cpp]
        events: List[Tuple[int, str, dict]] = []

        for m in RE_SETNS.finditer(text):
            events.append((m.start(), "ns", {"ns": m.group(1)}))
        for m in RE_STATEAS.finditer(text):
            full = m.group(1)
            fn, _, field = full.rpartition(".")
            events.append((m.start(), "state", {
                "ns": fn or ns, "name": field or full, "full": full,
                "kind": KIND_MAP.get(m.group(2), m.group(2)),
                "desc": unescape(m.group(3)), "rw": m.group(4) == "true"}))
        for m in RE_STATE.finditer(text):
            events.append((m.start(), "state", {
                "ns": ns, "name": m.group(1), "full": f"{ns}.{m.group(1)}",
                "kind": KIND_MAP.get(m.group(2), m.group(2)),
                "desc": unescape(m.group(3)), "rw": m.group(4) == "true"}))
        for m in RE_ENUM.finditer(text):
            vals = enum_values(all_text, m.group(3))
            events.append((m.start(), "state", {
                "ns": ns, "name": m.group(1), "full": f"{ns}.{m.group(1)}",
                "kind": "ENUM", "desc": unescape(m.group(4)), "rw": True, "enum": vals}))
        for m in RE_FUNC.finditer(text):
            events.append((m.start(), "func", {
                "ns": ns, "name": m.group(1), "full": f"{ns}.{m.group(1)}",
                "desc": unescape(m.group(3))}))
        for m in RE_EVENT.finditer(text):
            events.append((m.start(), "event", {
                "ns": ns, "name": m.group(1), "full": f"{ns}.{m.group(1)}",
                "desc": unescape(m.group(2))}))

        events.sort(key=lambda e: e[0])
        for _, role, e in events:
            if role == "ns":
                ns = e["ns"]
                continue
            # пересобрать full для setNamespace-контекста
            e["ns"] = ns
            e["full"] = f"{ns}.{e['name']}"
            e["role"] = role
            out.append(e)
    return out


def merge(entries: List[dict]) -> List[dict]:
    """Сливает state/func/event с одним full-именем в одну запись каталога."""
    by_full: Dict[str, dict] = {}
    order: List[str] = []

    def blank(full: str) -> dict:
        ns, _, name = full.rpartition(".")
        return {"ns": ns, "name": name, "full": full, "kind": "NONE",
                "desc": "", "rw": False, "e": False, "f": False, "a": False}

    for e in entries:
        full = e["full"]
        if full not in by_full:
            by_full[full] = blank(full)
            order.append(full)
        r = by_full[full]
        role = e.get("role")
        if role == "state":
            r["kind"] = e.get("kind", r["kind"])
            r["rw"] = e.get("rw", False)
            if e.get("enum"):
                r["enum"] = e["enum"]
        if role == "func":
            r["f"] = True
        if role == "event":
            r["e"] = True
        if e.get("desc") and not r["desc"]:
            r["desc"] = e["desc"]

    return [by_full[f] for f in
            sorted(order, key=lambda x: (x.rpartition(".")[0], x.rpartition(".")[2]))]


def generate_resources_catalog(src_root: Path, target_web_dir: Path, src_filter: str,
                               included_modules: Optional[List[str]] = None) -> Optional[Path]:
    """Строит /macros/resources.json в подготовленном каталоге ФС."""
    rel_dirs: List[str] = []
    src_root = Path(src_root)
    if src_root.is_dir():
        for item in sorted(src_root.iterdir()):
            if item.is_dir() and item.name.startswith(CORE_PREFIX):
                rel_dirs.append(item.name)
    for rel in parse_src_filter(src_filter):
        if rel not in rel_dirs:
            rel_dirs.append(rel)
    if included_modules:
        for rel in included_modules:
            if rel not in rel_dirs:
                rel_dirs.append(rel)

    entries: List[dict] = []
    for rel in rel_dirs:
        entries.extend(parse_component(src_root, rel))

    resources = merge(entries)
    mods = set()
    if included_modules:
        mods.update(Path(r).name for r in included_modules)
    mods.update(Path(r).name for r in parse_src_filter(src_filter))
    editor = 1 if "module_editor" in mods else 0

    doc = {"features": {"editor": editor}, "resources": resources}
    out_path = Path(target_web_dir).joinpath(*CATALOG_REL)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(doc, ensure_ascii=False, separators=(",", ":")),
                        encoding="utf-8")
    log(f"{out_path} ({len(resources)} resources, {out_path.stat().st_size} bytes)")
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", required=False, help="env (для справки)")
    ap.add_argument("--project", default=".", help="корень проекта")
    ap.add_argument("--out", default=None, help="каталог вывода (по умолчанию web_debug/web_<env>)")
    ap.add_argument("--src-filter", default="", help="строка src_filter")
    args = ap.parse_args()

    project = Path(args.project).resolve()
    src_root = project / SRC_FOLDER
    src_filter = args.src_filter
    if not src_filter and args.env:
        # прочтать src_filter из ini нельзя просто — оставляем пустым (ядро всё равно просканируется)
        pass
    target = Path(args.out) if args.out else (project / "web_debug" / f"web_{args.env or 'manual'}")
    generate_resources_catalog(src_root, target, src_filter)
    return 0


if __name__ == "__main__":
    sys.exit(main())
