import os
import sys
import subprocess
import datetime
import re
from pathlib import Path
Import("env")

# ============================================================
# ОТЛАДКА (как #define DEBUG в Си)
# ============================================================
DEBUG = False  # установите True для отладочного вывода

def debug_print(*args, **kwargs):
    """Выводит сообщение только если DEBUG = True"""
    if DEBUG:
        print(*args, **kwargs)

# ============================================================
# ЗАЩИТА ОТ ДВОЙНОГО ЗАПУСКА (только для одного процесса)
# ============================================================
# Проверяем, не сборка ли это ФС
is_fs_build = False
for arg in sys.argv:
    if 'buildfs' in arg or ('--target' in arg and 'fs' in ' '.join(sys.argv).lower()):
        is_fs_build = True
        break

# Если это не сборка ФС и скрипт уже запускался - выходим
if not is_fs_build and os.environ.get("PLATFORMIO_VERSION_BUILDER_RAN") == "1":
    debug_print("Version builder already ran for firmware build, skipping...")
    sys.exit(0)

# ============================================================
# ОПРЕДЕЛЕНИЕ РЕАЛЬНОЙ СБОРКИ
# ============================================================
def is_real_build():
    """
    Определяет, выполняется ли реальная сборка прошивки (не ФС).
    """
    cmd_line = ' '.join(sys.argv).lower()
    
    if 'buildfs' in cmd_line:
        debug_print("  DEBUG: FS build detected - not incrementing counters")
        return False
    
    build_indicators = ['build', 'upload', 'program']
    
    for indicator in build_indicators:
        if indicator in cmd_line:
            debug_print(f"  DEBUG: Found firmware build indicator: {indicator}")
            return True
    
    debug_print("  DEBUG: No firmware build indicators found")
    return False

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
        # Проверяем, что такая папка действительно существует
        if (src_dir / module_name).exists():
            debug_print(f"  Found module in src_filter: {module_name}")
            return module_name
        else:
            debug_print(f"  Warning: module {module_name} found in src_filter but directory missing")
    
    # Способ 2: по имени окружения
    env_lower = env.subst("$PIOENV").lower()
    parts = env_lower.split('-')
    if len(parts) >= 2:
        module_type = parts[-1]  # swd, isp, gpio
        for d in src_dir.iterdir():
            if d.is_dir() and d.name.startswith("module_"):
                # Ищем, содержится ли module_type в имени модуля
                if module_type in d.name.lower():
                    debug_print(f"  Found module by env name: {d.name}")
                    return d.name
    
    debug_print(f"  No module found for this environment")
    return None

def read_root_counters(project_dir):
    """
    Читает значения из version_counter.txt (major и core)
    """
    counter_file = project_dir / "version_counter.txt"
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
    
    return counters

def write_root_counters(project_dir, major, core):
    """Записывает major и core в version_counter.txt"""
    counter_file = project_dir / "version_counter.txt"
    try:
        with open(counter_file, 'w') as f:
            f.write(f"major={major}\n")
            f.write(f"core={core}\n")
        debug_print(f"  Written root counters: major={major}, core={core}")
    except Exception as e:
        debug_print(f"  Error writing root counter file: {e}")

def read_module_counters(module_dir):
    """
    Читает значения из module_counter.txt (module и build)
    """
    counter_file = module_dir / "module_counter.txt"
    counters = {"module": 0, "build": 0}
    
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
    
    return counters

def write_module_counters(module_dir, module_counter, build_counter):
    """Записывает module и build в module_counter.txt"""
    counter_file = module_dir / "module_counter.txt"
    try:
        with open(counter_file, 'w') as f:
            f.write(f"module={module_counter}\n")
            f.write(f"build={build_counter}\n")
        debug_print(f"  Written module counters: module={module_counter}, build={build_counter}")
    except Exception as e:
        debug_print(f"  Error writing module counter file: {e}")

def get_last_commit_hash(project_dir, path):
    """Возвращает хэш последнего коммита для пути"""
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
        debug_print(f"  Git error for {path}: {e}")
    return None

def get_stored_hash(project_dir, key):
    """Читает сохранённый хэш"""
    hash_file = project_dir / ".version_hashes"
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
    hash_file = project_dir / ".version_hashes"
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
        debug_print(f"  Error storing hash: {e}")

def should_increment_core(project_dir, src_dir):
    """Проверяет изменения в core-модулях"""
    core_dirs = [d.name for d in src_dir.iterdir() 
                 if d.is_dir() and d.name.startswith("core_")]
    
    if not core_dirs:
        return False
    
    any_changed = False
    for core_dir in core_dirs:
        current_hash = get_last_commit_hash(project_dir, f"src/{core_dir}")
        stored_hash = get_stored_hash(project_dir, core_dir)
        
        if current_hash and current_hash != stored_hash:
            debug_print(f"  Core {core_dir} changed: {stored_hash} -> {current_hash}")
            any_changed = True
            store_hash(project_dir, core_dir, current_hash)
    
    return any_changed

def should_increment_module(project_dir, module_dir, module_name):
    """Проверяет изменения в модуле"""
    current_hash = get_last_commit_hash(project_dir, f"src/{module_name}")
    stored_hash = get_stored_hash(project_dir, module_name)
    
    if current_hash and current_hash != stored_hash:
        debug_print(f"  Module {module_name} changed: {stored_hash} -> {current_hash}")
        store_hash(project_dir, module_name, current_hash)
        return True
    
    return False

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def generate_version_header():
    """Генерирует version_full.h с версией в формате 0.C.M.B"""
    
    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    src_dir = project_dir / "src"
    version_file = src_dir / "version_full.h"
    
    print(f"\n{'='*60}")
    print(f"VERSION BUILDER for: {env_name}")
    print(f"{'='*60}")
    
    # Определяем, реальная это сборка или просто переключение окружения
    building = is_real_build()
    if building:
        print("  Real build detected - counters will be updated")
    else:
        print("  Configuration only - no counters incremented")
    
    # Определяем активный модуль
    module_name = get_module_name(env, src_dir)
    print(f"  Active module: {module_name if module_name else 'none'}")
    
    # Читаем корневые счётчики (major, core)
    root_counters = read_root_counters(project_dir)
    
    # Читаем счётчики модуля, если он есть
    module_counters = {"module": 0, "build": 0}
    module_dir = None
    if module_name:
        module_dir = src_dir / module_name
        module_counters = read_module_counters(module_dir)
    
    # Определяем, нужно ли увеличивать счётчики (только при реальной сборке)
    if building:
        # Проверяем изменения в core
        inc_core = should_increment_core(project_dir, src_dir)
        if inc_core:
            root_counters["core"] += 1
            print(f"  Core changed: incrementing to {root_counters['core']}")
        
        # Проверяем изменения в модуле (если он есть)
        if module_name:
            inc_module = should_increment_module(project_dir, module_dir, module_name)
            if inc_module:
                module_counters["module"] += 1
                print(f"  Module changed: incrementing to {module_counters['module']}")
        
        # build всегда увеличивается при реальной сборке (в счётчиках модуля)
        module_counters["build"] += 1
        print(f"  Build number: {module_counters['build']}")
        
        # Записываем обновлённые счётчики
        write_root_counters(project_dir, root_counters["major"], root_counters["core"])
        if module_dir:
            write_module_counters(module_dir, module_counters["module"], module_counters["build"])
    else:
        print("  No counters incremented (not a real build)")
    
    # Формируем полную версию
    full_version = f"{root_counters['major']}.{root_counters['core']:03d}.{module_counters['module']:03d}.{module_counters['build']:04d}"
    
    # Получаем Git информацию
    try:
        git_branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_dir),
            text=True
        ).strip()
    except:
        git_branch = "unknown"
    
    try:
        git_commit = subprocess.check_output(
            ["git", "log", "-1", "--format=%h"],
            cwd=str(project_dir),
            text=True
        ).strip()
    except:
        git_commit = "unknown"
    
    # Генерируем version_full.h
    content = f'''// Auto-generated version file
// Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

#ifndef VERSION_FULL_H
#define VERSION_FULL_H

// Глобальная версия проекта (из version_counter.txt)
#define PROJECT_VERSION_MAJOR {root_counters['major']}

// Версия ядра (из version_counter.txt)
#define CORE_VERSION {root_counters['core']:03d}

// Версия модуля (из module_counter.txt в папке модуля)
#define MODULE_VERSION {module_counters['module']:03d}

// Номер сборки (из module_counter.txt в папке модуля)
#define BUILD_NUMBER {module_counters['build']:04d}

// Полная версия в формате 0.C.M.B
#define FIRMWARE_VERSION "{full_version}"

// Дополнительная информация
#define BUILD_ENV "{env_name}"
#define GIT_BRANCH "{git_branch}"
#define GIT_COMMIT "{git_commit}"
#define ACTIVE_MODULE "{module_name if module_name else 'none'}"

#endif // VERSION_FULL_H
'''
    
    # Записываем файл
    version_file.write_text(content, encoding='utf-8')
    print(f"\nVersion file created: {version_file}")
    print(f"Firmware version: {full_version}")
    print(f"{'='*60}\n")
    
    return root_counters, module_counters, full_version

# ============================================================
# ЗАПУСК
# ============================================================
# Запускаем основную функцию
counters, module_counters, full_version = generate_version_header()

# Помечаем, что скрипт выполнен (только если это не сборка ФС)
if not is_fs_build:
    os.environ["PLATFORMIO_VERSION_BUILDER_RAN"] = "1"