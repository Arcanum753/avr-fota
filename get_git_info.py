import os
import sys
import subprocess
import datetime
import re
from pathlib import Path
Import("env")

# ============================================================
# КОНФИГУРАЦИЯ (как #define в Си)
# ============================================================

# Отладка
DEBUG = False  # Установите True для отладочного вывода

# Имена файлов со счётчиками
ROOT_COUNTER_FILE = "version_counter.txt"  # major и core в корне проекта
MODULE_COUNTER_FILE = "module_counter.txt"  # module и build в папке модуля

# Формат вывода
MAJOR_DIGITS = 1     # количество цифр для major (0)
CORE_DIGITS = 2      # количество цифр для core (015)
MODULE_DIGITS = 2    # количество цифр для module (007)
BUILD_DIGITS = 0     # количество цифр для build (1243)

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def debug_print(*args, **kwargs):
    """Выводит сообщение только если DEBUG = True"""
    if DEBUG:
        print(*args, **kwargs)

def get_module_name(env, src_dir):
    """
    Определяет активный модуль по src_filter или имени окружения.
    """
    # Способ 1: из src_filter
    src_filter = env.subst("${SRC_FILTER}")
    if not src_filter:
        src_filter = env.subst("${platformio.src_filter}")
    
    match = re.search(r'\+<module_([^>/]+)/?>', src_filter)
    if match:
        module_name = f"module_{match.group(1)}"
        if (src_dir / module_name).exists():
            debug_print(f"  Found module in src_filter: {module_name}")
            return module_name
        else:
            debug_print(f"  Warning: module {module_name} found in src_filter but directory missing")
    
    # Способ 2: по имени окружения
    env_lower = env.subst("$PIOENV").lower()
    parts = env_lower.split('-')
    if len(parts) >= 2:
        module_type = parts[-1]
        for d in src_dir.iterdir():
            if d.is_dir() and d.name.startswith("module_"):
                if module_type in d.name.lower():
                    debug_print(f"  Found module by env name: {d.name}")
                    return d.name
    
    debug_print(f"  No module found for this environment")
    return None

def read_root_counters(project_dir):
    """
    Читает значения из version_counter.txt (major и core)
    """
    counter_file = project_dir / ROOT_COUNTER_FILE
    counters = {"major": 0, "core": 0}
    
    if counter_file.exists():
        try:
            with open(counter_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if '=' in line:
                        key, value = line.split('=')
                        if key in counters:
                            counters[key] = int(value)
            debug_print(f"  Root counters: major={counters['major']}, core={counters['core']}")
        except Exception as e:
            debug_print(f"  Error reading root counter file: {e}")
    else:
        debug_print(f"  Root counter file not found: {counter_file}")
    
    return counters

def read_module_counters(module_dir):
    """
    Читает значения из module_counter.txt (module и build)
    """
    counters = {"module": 0, "build": 0}
    
    if not module_dir:
        return counters
    
    counter_file = module_dir / MODULE_COUNTER_FILE
    if counter_file.exists():
        try:
            with open(counter_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if '=' in line:
                        key, value = line.split('=')
                        if key in counters:
                            counters[key] = int(value)
            debug_print(f"  Module counters: module={counters['module']}, build={counters['build']}")
        except Exception as e:
            debug_print(f"  Error reading module counter file: {e}")
    else:
        debug_print(f"  Module counter file not found: {counter_file}")
    
    return counters

def get_git_info(project_dir):
    """
    Получает информацию из Git.
    """
    info = {
        "branch": "unknown",
        "commit": "unknown",
        "full_commit": "unknown",
        "tag": "no-tag",
        "dirty": False
    }
    
    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
    except:
        debug_print("  Git not found")
        return info
    
    # Ветка
    try:
        info["branch"] = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_dir),
            text=True
        ).strip()
        debug_print(f"  Git branch: {info['branch']}")
    except:
        pass
    
    # Короткий хэш
    try:
        info["commit"] = subprocess.check_output(
            ["git", "log", "-1", "--format=%h"],
            cwd=str(project_dir),
            text=True
        ).strip()
        debug_print(f"  Git commit: {info['commit']}")
    except:
        pass
    
    # Полный хэш
    try:
        info["full_commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(project_dir),
            text=True
        ).strip()
    except:
        pass
    
    # Тег
    try:
        info["tag"] = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            cwd=str(project_dir),
            text=True,
            stderr=subprocess.DEVNULL
        ).strip()
        debug_print(f"  Git tag: {info['tag']}")
    except:
        try:
            describe = subprocess.check_output(
                ["git", "describe", "--tags", "--long"],
                cwd=str(project_dir),
                text=True,
                stderr=subprocess.DEVNULL
            ).strip()
            parts = describe.rsplit('-', 2)
            if len(parts) == 3:
                info["tag"] = f"{parts[0]}-{parts[1]}"
            else:
                info["tag"] = describe
            debug_print(f"  Git nearest tag: {info['tag']}")
        except:
            debug_print("  No git tags found")
    
    # Dirty статус
    try:
        status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=str(project_dir),
            text=True
        ).strip()
        info["dirty"] = bool(status)
        debug_print(f"  Git dirty: {info['dirty']}")
    except:
        pass
    
    return info

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def generate_version_header():
    """
    Генерирует version.h с информацией о версии (без инкремента).
    Только читает существующие файлы счётчиков.
    """
    
    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    src_dir = project_dir / "src"
    version_file = src_dir / "version.h"
    
    print(f"\n{'='*60}")
    print(f"GIT INFO GENERATOR for: {env_name}")
    print(f"{'='*60}")
    
    # Получаем Git информацию
    git_info = get_git_info(project_dir)
    
    # Определяем активный модуль
    module_name = get_module_name(env, src_dir)
    debug_print(f"  Active module: {module_name if module_name else 'none'}")
    
    # Читаем корневые счётчики (major, core) - только чтение, без записи
    root_counters = read_root_counters(project_dir)
    
    # Читаем счётчики модуля (module, build) - только чтение, без записи
    module_counters = {"module": 0, "build": 0}
    if module_name:
        module_dir = src_dir / module_name
        module_counters = read_module_counters(module_dir)
    
    # Формируем полную версию с ведущими нулями
    major_str = f"{root_counters['major']:0{MAJOR_DIGITS}d}"
    core_str = f"{root_counters['core']:0{CORE_DIGITS}d}"
    module_str = f"{module_counters['module']:0{MODULE_DIGITS}d}"
    build_str = f"{module_counters['build']:0{BUILD_DIGITS}d}"
    
    full_version = f"{major_str}.{core_str}.{module_str}.{build_str}"
    
    # Генерируем version.h
    content = f'''// Auto-generated version file
// Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// Версия в формате 0.C.M.B (из файлов счётчиков)
// ============================================================

// Глобальная версия проекта (из version_counter.txt)
#define PROJECT_VERSION_MAJOR {root_counters['major']}

// Версия ядра (из version_counter.txt)
#define CORE_VERSION {root_counters['core']:03d}

// Версия модуля (из module_counter.txt в папке модуля)
#define MODULE_VERSION {module_counters['module']:03d}

// Номер сборки (из module_counter.txt в папке модуля)
#define BUILD_NUMBER {module_counters['build']}

// ============================================================
// Версии как строки с ведущими нулями (для форматирования)
// ============================================================

#define CORE_VERSION_STR "{root_counters['core']:03d}"
#define MODULE_VERSION_STR "{module_counters['module']:03d}"
#define BUILD_NUMBER_STR "{module_counters['build']:04d}"

// Полная версия в формате 0.C.M.B
#define FIRMWARE_VERSION "{full_version}"
#define FIRMWARE_VERSION_STR "{full_version}"

// ============================================================
// Git информация
// ============================================================

#define GIT_BRANCH "{git_info['branch']}"
#define GIT_COMMIT "{git_info['commit']}"
#define GIT_COMMIT_FULL "{git_info['full_commit']}"
#define GIT_TAG "{git_info['tag']}"
#define GIT_DIRTY {str(git_info['dirty']).lower()}

// ============================================================
// Информация о сборке
// ============================================================

#define BUILD_ENV "{env_name}"
#define BUILD_TIME "{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
#define BUILD_TIMESTAMP "{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
#define ACTIVE_MODULE "{module_name if module_name else 'none'}"

// ============================================================
// Удобные макросы для проверок
// ============================================================

#if GIT_DIRTY
#define IS_GIT_DIRTY true
#else
#define IS_GIT_DIRTY false
#endif

#endif // VERSION_H
'''
    
    # Записываем файл
    version_file.write_text(content, encoding='utf-8')
    print(f"Version file created: {version_file}")
    print(f"Firmware version: {full_version}")
    print(f"{'='*60}\n")
# ============================================================
# ЗАПУСК
# ============================================================
generate_version_header()