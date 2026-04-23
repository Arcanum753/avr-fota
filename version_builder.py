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
# Формат: MAJOR.MINOR.DATE.BUILD
MAJOR_DIGITS = 1     # количество цифр для major (0)
MINOR_DIGITS = 3     # количество цифр для minor (015)
BUILD_DIGITS = 4     # количество цифр для build (1243)

# Ведущие нули для отдельных компонентов
MINOR_LEADING_ZEROS = True      # добавлять ведущие нули к minor
BUILD_LEADING_ZEROS = True       # добавлять ведущие нули к build

# ========== ФАЙЛЫ СЧЁТЧИКОВ ==========
ROOT_COUNTER_FILE = "version_counter.txt"   # major и minor в корне проекта
BUILD_COUNTER_FILE = "build_counter.txt"    # build счётчик (можно в корне или отдельно)
HASH_STORAGE_FILE = ".version_hashes"       # файл для хранения git хэшей

# ========== РЕЖИМЫ РАБОТЫ ==========
# auto: определяется автоматически
# always: всегда инкрементировать build при сборке
# never: никогда не инкрементировать
BUILD_INCREMENT_MODE = "auto"  # auto, always, never

# ========== GIT ИНТЕГРАЦИЯ ==========
ENABLE_GIT_INFO = True          # добавлять Git информацию в version.h
TRACK_MINOR_CHANGES = True      # отслеживать изменения в проекте для инкремента minor

# ========== ЗАЩИТА ОТ ДВОЙНОГО ЗАПУСКА ==========
ENABLE_DOUBLE_RUN_PROTECTION = True  # предотвращает множественный запуск при одной сборке

# ========== ФОРМАТ ДАТЫ ==========
DATE_FORMAT = "%Y%m%d_%H%M"  # формат: yyyyMMddHHmm, например: 202503011430

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
# РАБОТА СО СЧЁТЧИКАМИ
# ============================================================

def read_root_counters(project_dir):
    """
    Читает значения из version_counter.txt (major и minor)
    """
    counter_file = project_dir / ROOT_COUNTER_FILE
    counters = {"major": 0, "minor": 0}
    
    if counter_file.exists():
        try:
            with open(counter_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if '=' in line:
                        key, value = line.split('=')
                        if key in counters:
                            counters[key] = int(value)
            debug_print(f"Root counters: major={counters['major']}, minor={counters['minor']}")
        except Exception as e:
            debug_print(f"Error reading root counter file: {e}")
    else:
        debug_print(f"Root counter file not found: {counter_file}")
    
    return counters

def write_root_counters(project_dir, major, minor):
    """Записывает major и minor в version_counter.txt"""
    counter_file = project_dir / ROOT_COUNTER_FILE
    try:
        with open(counter_file, 'w') as f:
            f.write(f"major={major}\n")
            f.write(f"minor={minor}\n")
        debug_print(f"Written root counters: major={major}, minor={minor}")
    except Exception as e:
        debug_print(f"Error writing root counter file: {e}")

def read_build_counter(project_dir):
    """
    Читает значение build счётчика
    """
    counter_file = project_dir / BUILD_COUNTER_FILE
    build = 0
    
    if counter_file.exists():
        try:
            with open(counter_file, 'r') as f:
                build = int(f.read().strip())
            debug_print(f"Build counter: {build}")
        except Exception as e:
            debug_print(f"Error reading build counter file: {e}")
    else:
        debug_print(f"Build counter file not found: {counter_file}")
    
    return build

def write_build_counter(project_dir, build):
    """Записывает build счётчик"""
    counter_file = project_dir / BUILD_COUNTER_FILE
    try:
        with open(counter_file, 'w') as f:
            f.write(str(build))
        debug_print(f"Written build counter: {build}")
    except Exception as e:
        debug_print(f"Error writing build counter file: {e}")

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

def should_increment_minor(project_dir, src_dir):
    """
    Проверяет изменения во всём проекте для инкремента minor.
    Сравнивает хэш всего проекта с сохранённым.
    """
    if not TRACK_MINOR_CHANGES or not ENABLE_GIT_INFO:
        return False
    
    # Получаем хэш всего проекта (последний коммит)
    current_hash = get_last_commit_hash(project_dir, ".")
    stored_hash = get_stored_hash(project_dir, "project")
    
    if current_hash and current_hash != stored_hash:
        debug_print(f"Project changed: {stored_hash} -> {current_hash}")
        store_hash(project_dir, "project", current_hash)
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

def get_date_string():
    """Возвращает дату в заданном формате yyyyMMddHHmm"""
    now = datetime.datetime.now()
    return now.strftime(DATE_FORMAT)

def build_version_string(major, minor, date_str, build):
    """Собирает полную строку версии в формате MAJOR.MINOR.DATE.BUILD"""
    major_str = format_version_component(major, MAJOR_DIGITS, False)
    minor_str = format_version_component(minor, MINOR_DIGITS, MINOR_LEADING_ZEROS)
    build_str = format_version_component(build, BUILD_DIGITS, BUILD_LEADING_ZEROS)
    
    return f"{major_str}.{minor_str}.{date_str}.{build_str}"

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def generate_version_header():
    """
    Генерирует version.h с полной информацией о версии в формате MAJOR.MINOR.DATE.BUILD.
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
    info_print(f"VERSION BUILDER (MAJOR.MINOR.DATE.BUILD) for: {env_name}")
    info_print(f"{'='*60}")
    
    # Определяем, реальная это сборка или нет
    building = is_real_build()
    if building:
        info_print("  Real build detected - counters will be updated")
    else:
        info_print("  Configuration only - no counters incremented")
    
    # Читаем корневые счётчики (major и minor)
    root_counters = read_root_counters(project_dir)
    
    # Читаем build счётчик
    build_counter = read_build_counter(project_dir)
    
    # Инкрементируем счётчики при реальной сборке
    if building:
        inc_minor = should_increment_minor(project_dir, src_dir)
        if inc_minor:
            root_counters["minor"] += 1
            info_print(f"  Minor changed: incrementing to {root_counters['minor']}")
        
        build_counter += 1
        info_print(f"  Build number: {build_counter}")
        
        write_root_counters(project_dir, root_counters["major"], root_counters["minor"])
        write_build_counter(project_dir, build_counter)
    else:
        info_print("  No counters incremented")
    
    # Получаем Git информацию
    git_info = get_git_info(project_dir)
    
    # Получаем дату
    date_str = get_date_string()
    
    # Формируем полную версию
    full_version = build_version_string(
        root_counters['major'],
        root_counters['minor'],
        date_str,
        build_counter
    )
    
    # Форматируем отдельные компоненты
    major_str = format_version_component(root_counters['major'], MAJOR_DIGITS, False)
    minor_str = format_version_component(root_counters['minor'], MINOR_DIGITS, MINOR_LEADING_ZEROS)
    build_str = format_version_component(build_counter, BUILD_DIGITS, BUILD_LEADING_ZEROS)
    
    # Генерируем version.h
    content = f'''// Auto-generated version file
// Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// НАСТРОЙКИ ФОРМАТИРОВАНИЯ (из скрипта сборки)
// ============================================================
// Формат: MAJOR.MINOR.DATE.BUILD
// MAJOR_DIGITS = {MAJOR_DIGITS}
// MINOR_DIGITS = {MINOR_DIGITS}
// BUILD_DIGITS = {BUILD_DIGITS}
// MINOR_LEADING_ZEROS = {MINOR_LEADING_ZEROS}
// BUILD_LEADING_ZEROS = {BUILD_LEADING_ZEROS}
// DATE_FORMAT = "{DATE_FORMAT}"

// ============================================================
// ВЕРСИЯ В ФОРМАТЕ MAJOR.MINOR.DATE.BUILD
// ============================================================

// Глобальная версия проекта (из {ROOT_COUNTER_FILE})
#define PROJECT_VERSION_MAJOR {root_counters['major']}

// Минорная версия (инкремент при каждом коммите)
#define PROJECT_VERSION_MINOR {root_counters['minor']}
#define PROJECT_VERSION_MINOR_RAW {root_counters['minor']}
#define PROJECT_VERSION_MINOR_STR "{minor_str}"

// Дата и время сборки (yyyyMMddHHmm)
#define BUILD_DATE_STR "{date_str}"
#define BUILD_DATE_RAW {date_str}
#define BUILD_DATE_YEAR {datetime.datetime.now().strftime('%Y')}
#define BUILD_DATE_MONTH {datetime.datetime.now().strftime('%m')}
#define BUILD_DATE_DAY {datetime.datetime.now().strftime('%d')}
#define BUILD_TIME_HOUR {datetime.datetime.now().strftime('%H')}
#define BUILD_TIME_MINUTE {datetime.datetime.now().strftime('%M')}

// Номер сборки
#define BUILD_NUMBER {build_counter}
#define BUILD_NUMBER_RAW {build_counter}
#define BUILD_NUMBER_STR "{build_str}"

// Полная версия в формате MAJOR.MINOR.DATE.BUILD
#define FIRMWARE_VERSION "{full_version}"
#define FIRMWARE_VERSION_STR "{full_version}"

// ============================================================
// КОМПОНЕНТЫ ВЕРСИИ ДЛЯ МАТЕМАТИЧЕСКИХ ОПЕРАЦИЙ
// ============================================================

#define VERSION_MAJOR {root_counters['major']}
#define VERSION_MINOR {root_counters['minor']}
#define VERSION_DATE {date_str.replace('_', '')}
#define VERSION_DATE_STR "{date_str}"
#define VERSION_BUILD {build_counter}

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

// ============================================================
// УДОБНЫЕ МАКРОСЫ ДЛЯ ПРОВЕРОК
// ============================================================

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define IS_GIT_DIRTY GIT_DIRTY

// Версия как число (для сравнений)
#define VERSION_NUM ((VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | (int(VERSION_DATE) << 8) | VERSION_BUILD)

// Полная версия как строка (альтернативный макрос)
#define FW_VERSION TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_DATE) "." TOSTRING(VERSION_BUILD)

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