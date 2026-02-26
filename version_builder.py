import os
import sys
import subprocess
import datetime
import re
from pathlib import Path
Import("env")

# ============================================================
# НАСТРОЙКИ И ОПЦИИ (как #define в Си)
# ============================================================

# ========== ОТЛАДКА ==========
DEBUG = False  # Установите True для отладочного вывода

# ========== ФОРМАТ ВЕРСИИ ==========
# Формат: MAJOR.CORE.MODULE.BUILD
MAJOR_DIGITS = 1     # количество цифр для major (0)
CORE_DIGITS = 2      # количество цифр для core (015)
MODULE_DIGITS = 2    # количество цифр для module (007)
BUILD_DIGITS = 3     # количество цифр для build (1243)

# Ведущие нули для отдельных компонентов
CORE_LEADING_ZEROS = False      # добавлять ведущие нули к core
MODULE_LEADING_ZEROS = False     # добавлять ведущие нули к module
BUILD_LEADING_ZEROS = False      # добавлять ведущие нули к build

# ========== ФАЙЛЫ СЧЁТЧИКОВ ==========
ROOT_COUNTER_FILE = "version_counter.txt"   # major и core в корне проекта
MODULE_COUNTER_FILE = "module_counter.txt"  # module и build в папке модуля
HASH_STORAGE_FILE = ".version_hashes"       # файл для хранения git хэшей

# ========== РЕЖИМЫ РАБОТЫ ==========
# auto: определяется автоматически
# always: всегда инкрементировать build при сборке
# never: никогда не инкрементировать
BUILD_INCREMENT_MODE = "auto"  # auto, always, never

# ========== GIT ИНТЕГРАЦИЯ ==========
ENABLE_GIT_INFO = True          # добавлять Git информацию в version.h
TRACK_CORE_CHANGES = True       # отслеживать изменения в core для инкремента
TRACK_MODULE_CHANGES = True     # отслеживать изменения в module для инкремента

# ========== ЗАЩИТА ОТ ДВОЙНОГО ЗАПУСКА ==========
ENABLE_DOUBLE_RUN_PROTECTION = True  # предотвращает множественный запуск при одной сборке

# ========== ФОРМАТ ВЫВОДА ==========
SHOW_BUILD_INFO = True           # показывать информацию о сборке в консоль
SHOW_GIT_INFO = True             # показывать Git информацию в консоль

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def debug_print(*args, **kwargs):
    """Выводит сообщение только если DEBUG = True"""
    if DEBUG:
        print("[DEBUG]", *args, **kwargs)

def info_print(*args, **kwargs):
    """Выводит информационное сообщение"""
    if SHOW_BUILD_INFO:
        print(*args, **kwargs)

def git_print(*args, **kwargs):
    """Выводит Git информацию"""
    if SHOW_GIT_INFO and SHOW_BUILD_INFO:
        print(*args, **kwargs)

# ============================================================
# ЗАЩИТА ОТ ДВОЙНОГО ЗАПУСКА
# ============================================================

def check_double_run_protection():
    """Проверяет, не запускался ли скрипт уже для этой сборки"""
    if not ENABLE_DOUBLE_RUN_PROTECTION:
        return False
    
    is_fs_build = False
    for arg in sys.argv:
        if 'buildfs' in arg or ('--target' in arg and 'fs' in ' '.join(sys.argv).lower()):
            is_fs_build = True
            break
    
    if not is_fs_build and os.environ.get("PLATFORMIO_VERSION_BUILDER_RAN") == "1":
        debug_print("Version builder already ran for firmware build, skipping...")
        return True
    
    return False

def set_double_run_flag():
    """Устанавливает флаг выполнения скрипта"""
    if ENABLE_DOUBLE_RUN_PROTECTION:
        is_fs_build = any('buildfs' in arg or ('--target' in arg and 'fs' in ' '.join(sys.argv).lower()) for arg in sys.argv)
        if not is_fs_build:
            os.environ["PLATFORMIO_VERSION_BUILDER_RAN"] = "1"

# ============================================================
# ОПРЕДЕЛЕНИЕ РЕАЛЬНОЙ СБОРКИ
# ============================================================

def is_real_build():
    """
    Определяет, выполняется ли реальная сборка прошивки (не ФС).
    """
    cmd_line = ' '.join(sys.argv).lower()
    
    if 'buildfs' in cmd_line:
        debug_print("FS build detected - not incrementing counters")
        return False
    
    if BUILD_INCREMENT_MODE == "never":
        debug_print("Build increment disabled by config")
        return False
    
    if BUILD_INCREMENT_MODE == "always":
        debug_print("Build increment forced by config")
        return True
    
    build_indicators = ['build', 'upload', 'program']
    for indicator in build_indicators:
        if indicator in cmd_line:
            debug_print(f"Found firmware build indicator: {indicator}")
            return True
    
    debug_print("No firmware build indicators found")
    return False

# ============================================================
# ОПРЕДЕЛЕНИЕ МОДУЛЯ
# ============================================================

def get_module_name(env, src_dir):
    """
    Определяет активный модуль:
    1. Сначала по src_filter (самый точный способ)
    2. Если не нашли, по имени окружения (swd/isp/gpio)
    """
    # Способ 1: из src_filter
    src_filter = env.subst("${SRC_FILTER}")
    if not src_filter:
        src_filter = env.subst("${platformio.src_filter}")
    
    match = re.search(r'\+<module_([^>/]+)/?>', src_filter)
    if match:
        module_name = f"module_{match.group(1)}"
        if (src_dir / module_name).exists():
            debug_print(f"Found module in src_filter: {module_name}")
            return module_name
        else:
            debug_print(f"Warning: module {module_name} found in src_filter but directory missing")
    
    # Способ 2: по имени окружения
    env_lower = env.subst("$PIOENV").lower()
    parts = env_lower.split('-')
    if len(parts) >= 2:
        module_type = parts[-1]
        for d in src_dir.iterdir():
            if d.is_dir() and d.name.startswith("module_"):
                if module_type in d.name.lower():
                    debug_print(f"Found module by env name: {d.name}")
                    return d.name
    
    debug_print("No module found for this environment")
    return None

# ============================================================
# РАБОТА СО СЧЁТЧИКАМИ
# ============================================================

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
            debug_print(f"Root counters: major={counters['major']}, core={counters['core']}")
        except Exception as e:
            debug_print(f"Error reading root counter file: {e}")
    else:
        debug_print(f"Root counter file not found: {counter_file}")
    
    return counters

def write_root_counters(project_dir, major, core):
    """Записывает major и core в version_counter.txt"""
    counter_file = project_dir / ROOT_COUNTER_FILE
    try:
        with open(counter_file, 'w') as f:
            f.write(f"major={major}\n")
            f.write(f"core={core}\n")
        debug_print(f"Written root counters: major={major}, core={core}")
    except Exception as e:
        debug_print(f"Error writing root counter file: {e}")

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
            debug_print(f"Module counters: module={counters['module']}, build={counters['build']}")
        except Exception as e:
            debug_print(f"Error reading module counter file: {e}")
    else:
        debug_print(f"Module counter file not found: {counter_file}")
    
    return counters

def write_module_counters(module_dir, module_counter, build_counter):
    """Записывает module и build в module_counter.txt"""
    if not module_dir:
        return
    
    counter_file = module_dir / MODULE_COUNTER_FILE
    try:
        with open(counter_file, 'w') as f:
            f.write(f"module={module_counter}\n")
            f.write(f"build={build_counter}\n")
        debug_print(f"Written module counters: module={module_counter}, build={build_counter}")
    except Exception as e:
        debug_print(f"Error writing module counter file: {e}")

# ============================================================
# GIT ФУНКЦИИ ДЛЯ ОТСЛЕЖИВАНИЯ ИЗМЕНЕНИЙ
# ============================================================

def get_last_commit_hash(project_dir, path):
    """Возвращает хэш последнего коммита для пути"""
    if not ENABLE_GIT_INFO:
        return None
    
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H", path],
            cwd=str(project_dir),
            capture_output=True,
            text=True
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git error for {path}: {e}")
    return None

def get_stored_hash(project_dir, key):
    """Читает сохранённый хэш"""
    hash_file = project_dir / HASH_STORAGE_FILE
    if hash_file.exists():
        try:
            with open(hash_file, 'r') as f:
                for line in f:
                    if line.startswith(f"{key}="):
                        return line.strip().split('=')[1]
        except:
            pass
    return None

def store_hash(project_dir, key, hash_value):
    """Сохраняет хэш"""
    if not ENABLE_GIT_INFO:
        return
    
    hash_file = project_dir / HASH_STORAGE_FILE
    try:
        hashes = {}
        if hash_file.exists():
            with open(hash_file, 'r') as f:
                for line in f:
                    if '=' in line:
                        k, v = line.strip().split('=', 1)
                        hashes[k] = v
        
        hashes[key] = hash_value
        
        with open(hash_file, 'w') as f:
            for k, v in hashes.items():
                f.write(f"{k}={v}\n")
    except Exception as e:
        debug_print(f"Error storing hash: {e}")

def should_increment_core(project_dir, src_dir):
    """Проверяет изменения в core-модулях"""
    if not TRACK_CORE_CHANGES or not ENABLE_GIT_INFO:
        return False
    
    core_dirs = [d.name for d in src_dir.iterdir() 
                 if d.is_dir() and d.name.startswith("core_")]
    
    if not core_dirs:
        return False
    
    any_changed = False
    for core_dir in core_dirs:
        current_hash = get_last_commit_hash(project_dir, f"src/{core_dir}")
        stored_hash = get_stored_hash(project_dir, core_dir)
        
        if current_hash and current_hash != stored_hash:
            debug_print(f"Core {core_dir} changed: {stored_hash} -> {current_hash}")
            any_changed = True
            store_hash(project_dir, core_dir, current_hash)
    
    return any_changed

def should_increment_module(project_dir, module_dir, module_name):
    """Проверяет изменения в модуле"""
    if not TRACK_MODULE_CHANGES or not ENABLE_GIT_INFO:
        return False
    
    current_hash = get_last_commit_hash(project_dir, f"src/{module_name}")
    stored_hash = get_stored_hash(project_dir, module_name)
    
    if current_hash and current_hash != stored_hash:
        debug_print(f"Module {module_name} changed: {stored_hash} -> {current_hash}")
        store_hash(project_dir, module_name, current_hash)
        return True
    
    return False

# ============================================================
# GIT ИНФОРМАЦИЯ ДЛЯ VERSION.H
# ============================================================

def get_git_info(project_dir):
    """
    Получает информацию из Git для version.h.
    """
    info = {
        "branch": "unknown",
        "commit": "unknown",
        "full_commit": "unknown",
        "tag": "no-tag",
        "dirty": False
    }
    
    if not ENABLE_GIT_INFO:
        return info
    
    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
    except:
        debug_print("Git not found")
        return info
    
    try:
        info["branch"] = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_dir),
            text=True
        ).strip()
        debug_print(f"Git branch: {info['branch']}")
    except:
        pass
    
    try:
        info["commit"] = subprocess.check_output(
            ["git", "log", "-1", "--format=%h"],
            cwd=str(project_dir),
            text=True
        ).strip()
        debug_print(f"Git commit: {info['commit']}")
    except:
        pass
    
    try:
        info["full_commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=str(project_dir),
            text=True
        ).strip()
    except:
        pass
    
    try:
        info["tag"] = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            cwd=str(project_dir),
            text=True,
            stderr=subprocess.DEVNULL
        ).strip()
        debug_print(f"Git tag: {info['tag']}")
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
            debug_print(f"Git nearest tag: {info['tag']}")
        except:
            debug_print("No git tags found")
    
    try:
        status = subprocess.check_output(
            ["git", "status", "--porcelain"],
            cwd=str(project_dir),
            text=True
        ).strip()
        info["dirty"] = bool(status)
        debug_print(f"Git dirty: {info['dirty']}")
    except:
        pass
    
    return info

# ============================================================
# ФОРМАТИРОВАНИЕ ВЕРСИИ
# ============================================================

def format_version_component(value, digits, leading_zeros):
    """Форматирует компонент версии с учётом настроек"""
    if leading_zeros:
        return f"{value:0{digits}d}"
    else:
        return str(value)

def build_version_string(major, core, module, build):
    """Собирает полную строку версии"""
    major_str = format_version_component(major, MAJOR_DIGITS, False)
    core_str = format_version_component(core, CORE_DIGITS, CORE_LEADING_ZEROS)
    module_str = format_version_component(module, MODULE_DIGITS, MODULE_LEADING_ZEROS)
    build_str = format_version_component(build, BUILD_DIGITS, BUILD_LEADING_ZEROS)
    
    return f"{major_str}.{core_str}.{module_str}.{build_str}"

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def generate_version_header():
    """
    Генерирует version.h с полной информацией о версии.
    Инкрементирует счётчики только при реальной сборке.
    """
    
    # Проверка двойного запуска
    if check_double_run_protection():
        return
    
    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    src_dir = project_dir / "src"
    version_file = src_dir / "version.h"
    
    info_print(f"\n{'='*60}")
    info_print(f"VERSION BUILDER for: {env_name}")
    info_print(f"{'='*60}")
    
    # Определяем, реальная это сборка или нет
    building = is_real_build()
    if building:
        info_print("  Real build detected - counters will be updated")
    else:
        info_print("  Configuration only - no counters incremented")
    
    # Определяем активный модуль
    module_name = get_module_name(env, src_dir)
    info_print(f"  Active module: {module_name if module_name else 'none'}")
    
    # Читаем корневые счётчики
    root_counters = read_root_counters(project_dir)
    
    # Читаем счётчики модуля
    module_counters = {"module": 0, "build": 0}
    module_dir = None
    if module_name:
        module_dir = src_dir / module_name
        module_counters = read_module_counters(module_dir)
    
    # Инкрементируем счётчики при реальной сборке
    if building:
        inc_core = should_increment_core(project_dir, src_dir)
        if inc_core:
            root_counters["core"] += 1
            info_print(f"  Core changed: incrementing to {root_counters['core']}")
        
        if module_name:
            inc_module = should_increment_module(project_dir, module_dir, module_name)
            if inc_module:
                module_counters["module"] += 1
                info_print(f"  Module changed: incrementing to {module_counters['module']}")
        
        module_counters["build"] += 1
        info_print(f"  Build number: {module_counters['build']}")
        
        write_root_counters(project_dir, root_counters["major"], root_counters["core"])
        if module_dir:
            write_module_counters(module_dir, module_counters["module"], module_counters["build"])
    else:
        info_print("  No counters incremented")
    
    # Получаем Git информацию
    git_info = get_git_info(project_dir)
    
    # Формируем полную версию
    full_version = build_version_string(
        root_counters['major'],
        root_counters['core'],
        module_counters['module'],
        module_counters['build']
    )
    
    # Форматируем отдельные компоненты
    core_str = format_version_component(root_counters['core'], CORE_DIGITS, CORE_LEADING_ZEROS)
    module_str = format_version_component(module_counters['module'], MODULE_DIGITS, MODULE_LEADING_ZEROS)
    build_str = format_version_component(module_counters['build'], BUILD_DIGITS, BUILD_LEADING_ZEROS)
    
    # Генерируем version.h
    content = f'''// Auto-generated version file
// Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// НАСТРОЙКИ ФОРМАТИРОВАНИЯ (из скрипта сборки)
// ============================================================
// Формат: MAJOR.CORE.MODULE.BUILD
// MAJOR_DIGITS = {MAJOR_DIGITS}
// CORE_DIGITS = {CORE_DIGITS}
// MODULE_DIGITS = {MODULE_DIGITS}
// BUILD_DIGITS = {BUILD_DIGITS}
// CORE_LEADING_ZEROS = {CORE_LEADING_ZEROS}
// MODULE_LEADING_ZEROS = {MODULE_LEADING_ZEROS}
// BUILD_LEADING_ZEROS = {BUILD_LEADING_ZEROS}

// ============================================================
// ВЕРСИЯ В ФОРМАТЕ MAJOR.CORE.MODULE.BUILD
// ============================================================

// Глобальная версия проекта (из {ROOT_COUNTER_FILE})
#define PROJECT_VERSION_MAJOR {root_counters['major']}

// Версия ядра (из {ROOT_COUNTER_FILE})
#define CORE_VERSION {root_counters['core']}
#define CORE_VERSION_RAW {root_counters['core']}
#define CORE_VERSION_STR "{core_str}"

// Версия модуля (из {MODULE_COUNTER_FILE} в папке модуля)
#define MODULE_VERSION {module_counters['module']}
#define MODULE_VERSION_RAW {module_counters['module']}
#define MODULE_VERSION_STR "{module_str}"

// Номер сборки (из {MODULE_COUNTER_FILE} в папке модуля)
#define BUILD_NUMBER {module_counters['build']}
#define BUILD_NUMBER_RAW {module_counters['build']}
#define BUILD_NUMBER_STR "{build_str}"

// Полная версия в формате MAJOR.CORE.MODULE.BUILD
#define FIRMWARE_VERSION "{full_version}"
#define FIRMWARE_VERSION_STR "{full_version}"

// ============================================================
// КОМПОНЕНТЫ ВЕРСИИ ДЛЯ МАТЕМАТИЧЕСКИХ ОПЕРАЦИЙ
// ============================================================

#define VERSION_MAJOR {root_counters['major']}
#define VERSION_CORE {root_counters['core']}
#define VERSION_MODULE {module_counters['module']}
#define VERSION_BUILD {module_counters['build']}

// ============================================================
// GIT ИНФОРМАЦИЯ
// ============================================================

#define GIT_BRANCH "{git_info['branch']}"
#define GIT_COMMIT "{git_info['commit']}"
#define GIT_COMMIT_FULL "{git_info['full_commit']}"
#define GIT_TAG "{git_info['tag']}"
#define GIT_DIRTY {str(git_info['dirty']).lower()}

// ============================================================
// ИНФОРМАЦИЯ О СБОРКЕ
// ============================================================

#define BUILD_ENV "{env_name}"
#define BUILD_TIME "{datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}"
#define BUILD_TIMESTAMP "{datetime.datetime.now().strftime('%Y%m%d_%H%M%S')}"
#define BUILD_DATE "{datetime.datetime.now().strftime('%Y%m%d')}"
#define BUILD_YEAR {datetime.datetime.now().strftime('%Y')}
#define BUILD_MONTH {datetime.datetime.now().strftime('%m')}
#define BUILD_DAY {datetime.datetime.now().strftime('%d')}
#define BUILD_HOUR {datetime.datetime.now().strftime('%H')}
#define BUILD_MINUTE {datetime.datetime.now().strftime('%M')}
#define BUILD_SECOND {datetime.datetime.now().strftime('%S')}

#define ACTIVE_MODULE "{module_name if module_name else 'none'}"

// ============================================================
// УДОБНЫЕ МАКРОСЫ ДЛЯ ПРОВЕРОК
// ============================================================

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define IS_GIT_DIRTY GIT_DIRTY

// Версия как число (для сравнений)
#define VERSION_NUM ((VERSION_MAJOR << 24) | (VERSION_CORE << 16) | (VERSION_MODULE << 8) | VERSION_BUILD)

// Полная версия как строка (альтернативный макрос)
#define FW_VERSION TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_CORE) "." TOSTRING(VERSION_MODULE) "." TOSTRING(VERSION_BUILD)

// ============================================================
// ПРОВЕРКА ЦЕЛОСТНОСТИ
// ============================================================

#if VERSION_MAJOR != PROJECT_VERSION_MAJOR
#error "PROJECT_VERSION_MAJOR inconsistency"
#endif

#endif // VERSION_H
'''
    
    # Записываем файл
    version_file.write_text(content, encoding='utf-8')
    info_print(f"\nVersion file created: {version_file}")
    info_print(f"Firmware version: {full_version}")
    info_print(f"{'='*60}\n")
    
    # Устанавливаем флаг защиты от двойного запуска
    set_double_run_flag()

# ============================================================
# ЗАПУСК
# ============================================================
generate_version_header()