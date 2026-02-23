import os
import sys
import hashlib
import subprocess
import datetime
from pathlib import Path
Import("env")

# ============================================================
# КОНФИГУРАЦИЯ
# ============================================================
PROJECT_VERSION_MAJOR = 0  # глобальная версия (ставится руками)

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def get_core_version(src_dir):
    """
    Версия ядра (C) — хэш всех core_* папок.
    Берём последние коммиты для каждой core-папки и хэшируем их сумму.
    """
    core_dirs = [d for d in src_dir.iterdir() 
                 if d.is_dir() and d.name.startswith("core_")]
    
    if not core_dirs:
        return "000"
    
    # Получаем хэши последних коммитов для каждой core-папки
    hashes = []
    for core_dir in core_dirs:
        try:
            # Хэш последнего коммита, затрагивающего эту папку
            h = subprocess.check_output(
                ["git", "log", "-1", "--format=%h", core_dir.name],
                cwd=str(src_dir.parent),
                text=True
            ).strip()
            hashes.append(h)
            print(f"  Core {core_dir.name}: {h}")
        except:
            hashes.append("000")
            print(f"  Core {core_dir.name}: 000 (git error)")
    
    # Объединяем все хэши и берём первые 3 символа от MD5
    combined = "".join(hashes)
    core_version = hashlib.md5(combined.encode()).hexdigest()[:3]
    print(f"  Core version: {core_version} (from {combined})")
    return core_version

def get_module_version(env_name, src_dir):
    """
    Версия модуля (M) — хэш активного module_*.
    Определяем модуль по имени окружения (swd, isp, gpio и т.д.)
    """
    # Ищем модуль, чьё имя (без префикса) содержится в имени окружения
    module_name = None
    for d in src_dir.iterdir():
        if d.is_dir() and d.name.startswith("module_"):
            mod_key = d.name[7:].lower()  # убираем "module_"
            if mod_key in env_name.lower():
                module_name = d.name
                print(f"  Active module detected: {module_name}")
                break
    
    if not module_name:
        print("  No active module found")
        return "000"
    
    try:
        # Хэш последнего коммита в папке модуля
        h = subprocess.check_output(
            ["git", "log", "-1", "--format=%h", module_name],
            cwd=str(src_dir),
            text=True
        ).strip()
        print(f"  Module hash: {h}")
        return h[:3]
    except:
        print(f"  Failed to get module hash")
        return "000"

def get_build_number(project_dir):
    """
    Номер билда (B) — счётчик в файле build_counter.txt
    Автоматически увеличивается при каждой сборке
    """
    counter_file = project_dir / "build_counter.txt"
    
    # Читаем текущий номер
    build = 1
    if counter_file.exists():
        try:
            with open(counter_file, 'r') as f:
                build = int(f.read().strip()) + 1
                print(f"  Previous build: {build-1}")
        except:
            build = 1
    
    # Записываем новый
    with open(counter_file, 'w') as f:
        f.write(str(build))
    
    print(f"  New build number: {build}")
    return build

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
    print(f"VERSION BUILDER (0.C.M.B) for: {env_name}")
    print(f"{'='*60}")
    
    # Получаем все части версии
    core_ver = get_core_version(src_dir)
    module_ver = get_module_version(env_name, src_dir)
    build_num = get_build_number(project_dir)
    
    # Формируем полную версию
    full_version = f"{PROJECT_VERSION_MAJOR}.{core_ver}.{module_ver}.{build_num:04d}"
    
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

// Глобальная версия проекта (ставится руками)
#define PROJECT_VERSION_MAJOR {PROJECT_VERSION_MAJOR}

// Версии компонентов (автоматические)
#define CORE_VERSION "{core_ver}"
#define MODULE_VERSION "{module_ver}"
#define BUILD_NUMBER {build_num}

// Полная версия в формате 0.C.M.B
#define FIRMWARE_VERSION "{full_version}"

// Дополнительная информация для отладки
#define BUILD_ENV "{env_name}"
#define GIT_BRANCH "{git_branch}"
#define GIT_COMMIT "{git_commit}"

// Для обратной совместимости со старым кодом
#define GIT_VERSION FIRMWARE_VERSION

#endif // VERSION_FULL_H
'''
    
    # Записываем файл
    version_file.write_text(content, encoding='utf-8')
    print(f"\nVersion file created: {version_file}")
    print(f"Firmware version: {full_version}")
    print(f"{'='*60}\n")

# ============================================================
# ЗАПУСК
# ============================================================
generate_version_header()