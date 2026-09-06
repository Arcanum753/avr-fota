import os
import re
import sys
import subprocess
import datetime
import hashlib
from pathlib import Path
from typing import List
Import("env")

# ============================================================
# НАСТРОЙКИ (как #define в Си)
# ============================================================

# ========== ОТЛАДКА ==========
DEBUG = False  # Установите True для отладочного вывода

# ========== ПАПКИ ==========
SRC_FOLDER = "src"                       # папка с исходниками
CORE_PREFIX = "core_"                     # префикс ядерных модулей
MODULE_PREFIX = "module_"                 # префикс модулей
SUBMODULE_PREFIX = "submodule_"           # префикс субмодулей
DEVICE_PREFIX = "device_"                 # префикс девайс-модулей

# ========== ФОРМАТ ВЫВОДА ==========
SHOW_INFO = True                          # показывать информацию в консоль

# ========== ФОРМАТЫ ==========
DATE_FORMAT = "%Y.%m.%d %H:%M"            # формат даты: yyyy.mm.dd hh.mm

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def debug_print(*args, **kwargs):
    """Выводит сообщение только если DEBUG = True"""
    if DEBUG:
        print("[DEBUG MODULE]", *args, **kwargs)

def info_print(*args, **kwargs):
    """Выводит информационное сообщение"""
    if SHOW_INFO:
        print(*args, **kwargs)

def _git_cwd_and_path(project_dir, folder_path):
    """
    Возвращает (cwd, git_path) для git log по папке компонента.

    Если папка (или её предок) является корнем собственного git-репозитория
    (есть .git) — команда выполняется из этого корня с относительным путём.
    Иначе папка принадлежит репозиторию проекта (ядру).
    folder_path может быть абсолютным или относительным от project_dir
    (например "src/module_program/submodule_swd").
    """
    folder = Path(folder_path)
    if not folder.is_absolute():
        folder = project_dir / folder
    target = folder.resolve()

    probe = target
    while True:
        if (probe / ".git").exists():
            rel = os.path.relpath(str(target), str(probe)).replace("\\", "/")
            return str(probe), rel
        if probe.parent == probe:
            break
        probe = probe.parent

    if not Path(folder_path).is_absolute():
        return str(project_dir), folder_path.replace("\\", "/")
    rel = os.path.relpath(str(target), str(project_dir.resolve())).replace("\\", "/")
    return str(project_dir), rel


def get_folder_hash(project_dir, folder_path):
    """Возвращает хэш последнего коммита для папки."""
    try:
        cwd, git_path = _git_cwd_and_path(project_dir, folder_path)
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H", git_path],
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git error for {folder_path}: {e}")

    return None


def get_folder_code_hash(project_dir, folder_rel, header_filename):
    """
    Хэш последнего коммита, затрагивающего КОД папки компонента.

    Файл собственного заголовка версии (*_version.h) и .gitignore исключаются
    из pathspec, поэтому коммиты, меняющие только их, НЕ вызывают приращение версии.
    """
    cwd, git_path = _git_cwd_and_path(project_dir, folder_rel)

    if git_path == ".":
        paths = [".", f":(exclude){header_filename}", ":(exclude).gitignore"]
    else:
        paths = [git_path,
                 f":(exclude){git_path}/{header_filename}",
                 f":(exclude){git_path}/.gitignore"]

    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H", "--"] + paths,
            cwd=cwd,
            capture_output=True,
            text=True,
            check=False
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git code-hash error for {folder_rel}: {e}")

    return None


def get_commit_date(project_dir, folder_path, str_format=False):
    """Возвращает дату последнего коммита для указанной папки.
    Если str_format=True, возвращает в формате yyyy.mm.dd hh.mm.
    """
    try:
        cwd, git_path = _git_cwd_and_path(project_dir, folder_path)
        if str_format:
            result = subprocess.run(
                ["git", "log", "-1", "--format=%ad", f"--date=format:{DATE_FORMAT}", git_path],
                cwd=cwd,
                capture_output=True,
                text=True,
                check=False
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
        else:
            result = subprocess.run(
                ["git", "log", "-1", "--format=%ad", "--date=format:%Y-%m-%d %H:%M", git_path],
                cwd=cwd,
                capture_output=True,
                text=True,
                check=False
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git date error for {folder_path}: {e}")

    return None


def read_existing_header(version_file: Path, macro_prefix: str):
    """
    Читает из существующего *_version.h пару (version, code_hash).

    Закоммиченный заголовок — источник правды: версия не сбрасывается в свежем
    клоне, а приращение происходит только при изменении code-hash папки.
    """
    try:
        text = version_file.read_text(encoding='utf-8')
    except OSError:
        return None, None

    version = None
    m = re.search(rf"#define\s+{macro_prefix}_VERSION\s+(\d+)", text)
    if m:
        version = int(m.group(1))

    code_hash = None
    m = re.search(rf"#define\s+{macro_prefix}_COMMIT_HASH\s+\"([0-9a-fA-F]{{6,40}})\"", text)
    if m:
        code_hash = m.group(1)

    return version, code_hash


def generate_module_header(module_name, version, commit_date, commit_date_str, env_name, code_hash):
    """
    Генерирует содержимое заголовочного файла для модуля.
    """
    now = datetime.datetime.now()

    # Разбираем дату для отдельных компонентов (из стандартного формата)
    if commit_date:
        try:
            dt = datetime.datetime.strptime(commit_date, "%Y-%m-%d %H:%M")
            commit_year = dt.year
            commit_month = dt.month
            commit_day = dt.day
            commit_hour = dt.hour
            commit_minute = dt.minute
        except:
            commit_year = 1970
            commit_month = 1
            commit_day = 1
            commit_hour = 0
            commit_minute = 0
    else:
        commit_year = 1970
        commit_month = 1
        commit_day = 1
        commit_hour = 0
        commit_minute = 0
        commit_date_str = "1970.01.01 00.00"

    # Имя файла: module_name_version.h (например, core_ntp_version.h)
    macro_prefix = module_name.upper().replace('-', '_').replace('.', '_')
    guard_name = f"{macro_prefix}_VERSION_H"

    content = f'''// Auto-generated version file for module: {module_name}
// Generated: {now.strftime('%Y-%m-%d %H:%M')}
// Environment: {env_name}

#ifndef {guard_name}
#define {guard_name}

// ============================================================
// ВЕРСИЯ МОДУЛЯ {module_name}
// ============================================================

// Числовая версия (для сравнений)
#define {macro_prefix}_VERSION {version}

// Строковая версия
#define {macro_prefix}_VERSION_STR "{version}"

// Хэш последнего обработанного коммита кода (без учёта *_version.h)
#define {macro_prefix}_COMMIT_HASH "{code_hash}"

// ============================================================
// ДАТА ПОСЛЕДНЕГО ИЗМЕНЕНИЯ
// ============================================================

// Дата в формате yyyy.mm.dd hh.mm (как строка)
#define {macro_prefix}_COMMIT_DATE_STR "{commit_date_str}"

// Компоненты даты (для числовых операций)
#define {macro_prefix}_COMMIT_YEAR {commit_year}
#define {macro_prefix}_COMMIT_MONTH {commit_month}
#define {macro_prefix}_COMMIT_DAY {commit_day}
#define {macro_prefix}_COMMIT_HOUR {commit_hour}
#define {macro_prefix}_COMMIT_MINUTE {commit_minute}

// Полная дата в формате YYYY-MM-DD HH:MM (для отладки)
#define {macro_prefix}_COMMIT_DATE "{commit_date if commit_date else '1970-01-01 00:00'}"

// ============================================================
// ИНФОРМАЦИЯ О ГЕНЕРАЦИИ
// ============================================================

#define {macro_prefix}_GENERATED_TIME "{now.strftime('%Y-%m-%d %H:%M')}"
#define {macro_prefix}_GENERATED_TIMESTAMP "{now.strftime('%Y%m%d_%H%M%S')}"

#endif // {guard_name}
'''
    return content


# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def _has_own_code(folder: Path) -> bool:
    """Папка считается модулем, если в её корне есть код (.cpp/.h/.c/.hpp) или .ini."""
    try:
        for item in folder.iterdir():
            if item.is_file() and (item.suffix.lower() in (".cpp", ".h", ".c", ".hpp")
                                   or item.name.endswith(".ini")):
                return True
    except OSError:
        return False
    return False


def discover_module_dirs(src_dir: Path) -> List[Path]:
    """Собирает папки компонентов для генерации версий.

    - Папки верхнего уровня src/ с префиксами core_/module_/submodule_/device_, в корне
      которых есть код (это сами компоненты).
    - Если папка с префиксом не содержит кода в корне — это контейнер (например
      src/module_program/), тогда компонентами считаются её вложенные папки с теми же
      префиксами (src/module_program/module_prog, .../submodule_isp, ...).
    """
    found: List[Path] = []
    prefixes = (CORE_PREFIX, MODULE_PREFIX, SUBMODULE_PREFIX, DEVICE_PREFIX)

    def is_component_dir(p: Path) -> bool:
        return p.is_dir() and any(p.name.startswith(pr) for pr in prefixes)

    for item in sorted(src_dir.iterdir()):
        if not is_component_dir(item):
            continue
        if _has_own_code(item):
            found.append(item)
            continue
        # Контейнер: ищем вложенные компоненты (один уровень вглубь)
        for sub in sorted(item.iterdir()):
            if is_component_dir(sub):
                found.append(sub)
    return found


def generate_module_versions():
    """
    Генерирует файлы версий для всех core_* и module_*/device_* папок.

    Источник правды — закоммиченный *_version.h в репозитории компонента:
    - заголовок отсутствует      -> создаётся база с версией 0;
    - code-hash папки изменился  -> версия = предыдущая + 1 (файл переписывается);
    - code-hash не изменился     -> файл не трогается (идемпотентно, git чистый).

    code-hash считается по git-коммитам папки БЕЗ учёта её собственного *_version.h,
    поэтому коммит одного лишь заголовка не даёт приращения версии.
    """

    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    src_dir = project_dir / SRC_FOLDER

    info_print(f"\n{'='*60}")
    info_print(f"MODULE VERSION GENERATOR for: {env_name}")
    info_print(f"{'='*60}")

    if not src_dir.exists():
        info_print(f"ERROR: Source directory not found: {src_dir}")
        return

    # Собираем папки компонентов (в т.ч. вложенные в контейнерные папки)
    modules = discover_module_dirs(src_dir)

    if not modules:
        info_print("No core_* or module_* folders found")
        return

    info_print(f"Found {len(modules)} modules to process")
    info_print("-" * 60)

    generated_count = 0

    for module_path in modules:
        module_name = module_path.name
        module_rel = module_path.relative_to(project_dir).as_posix()   # напр. src/module_program/submodule_swd
        version_file = module_path / f"{module_name}_version.h"
        macro_prefix = module_name.upper().replace('-', '_').replace('.', '_')
        header_filename = f"{module_name}_version.h"

        info_print(f"Processing: {module_name}")

        old_version = None
        old_code_hash = None
        if version_file.exists():
            old_version, old_code_hash = read_existing_header(version_file, macro_prefix)

        # Хэш последнего коммита, меняющего код модуля (без учёта собственного *_version.h)
        code_hash = get_folder_code_hash(project_dir, module_rel, header_filename)

        changed = bool(code_hash) and code_hash != old_code_hash

        if version_file.exists() and not changed:
            info_print(f"  Module unchanged: version {old_version}")
            info_print("-" * 40)
            continue    # файл не перезаписываем — закоммиченный заголовок остаётся стабильным

        # Заголовка нет (база) либо был реальный коммит кода -> bump
        if old_version is None:
            new_version = 0
        else:
            new_version = old_version + 1
        info_print(f"  Module changed: version {old_version if old_version is not None else 'new'} -> {new_version}")

        commit_date = get_commit_date(project_dir, module_rel, str_format=False)  # для парсинга
        commit_date_str = get_commit_date(project_dir, module_rel, str_format=True)  # для строки
        if not commit_date_str:
            commit_date_str = "1970.01.01 00.00"

        content = generate_module_header(module_name, new_version, commit_date,
                                         commit_date_str, env_name, code_hash)

        try:
            version_file.write_text(content, encoding='utf-8')
            info_print(f"  Generated: {version_file.relative_to(project_dir)}")
            info_print(f"    Version: {new_version} (str: \"{new_version}\")")
            info_print(f"    Last commit: {commit_date_str}")
            generated_count += 1
        except Exception as e:
            info_print(f"  ERROR writing file: {e}")

        info_print("-" * 40)

    info_print(f"\nGenerated {generated_count} module version files")
    info_print(f"{'='*60}\n")

# ============================================================
# ЗАПУСК
# ============================================================
generate_module_versions()
