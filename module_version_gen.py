import os
import sys
import subprocess
import datetime
import hashlib
from pathlib import Path
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

# ========== ФАЙЛЫ СЧЁТЧИКОВ ==========
VERSION_STORAGE_FILE = ".module_versions"  # файл для хранения версий модулей

# ========== ФОРМАТ ВЫВОДА ==========
SHOW_INFO = True                          # показывать информацию в консоль

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

def get_folder_hash(project_dir, folder_path):
    """
    Вычисляет хэш содержимого папки на основе Git.
    Возвращает хэш последнего коммита для папки.
    """
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%H", folder_path],
            cwd=str(project_dir),
            capture_output=True,
            text=True,
            check=False
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git error for {folder_path}: {e}")
    
    return None

def get_commit_date(project_dir, folder_path):
    """
    Возвращает дату последнего коммита для указанной папки.
    Формат: YYYY-MM-DD HH:MM
    """
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%Y-%m-%d %H:%M", folder_path],
            cwd=str(project_dir),
            capture_output=True,
            text=True,
            check=False
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception as e:
        debug_print(f"Git date error for {folder_path}: {e}")
    
    return None

def read_module_versions(project_dir):
    """
    Читает сохранённые версии модулей из файла.
    """
    version_file = project_dir / VERSION_STORAGE_FILE
    versions = {}
    
    if version_file.exists():
        try:
            with open(version_file, 'r') as f:
                for line in f:
                    line = line.strip()
                    if '=' in line:
                        module, version = line.split('=', 1)
                        versions[module] = int(version)
            debug_print(f"Read versions for {len(versions)} modules")
        except Exception as e:
            debug_print(f"Error reading version file: {e}")
    
    return versions

def write_module_versions(project_dir, versions):
    """
    Записывает версии модулей в файл.
    """
    version_file = project_dir / VERSION_STORAGE_FILE
    try:
        with open(version_file, 'w') as f:
            for module, version in sorted(versions.items()):
                f.write(f"{module}={version}\n")
        debug_print(f"Written versions for {len(versions)} modules")
    except Exception as e:
        debug_print(f"Error writing version file: {e}")

def generate_module_header(module_name, version, commit_date, env_name):
    """
    Генерирует содержимое заголовочного файла для модуля.
    """
    now = datetime.datetime.now()
    
    # Разбираем дату для отдельных компонентов
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
    
    # Имя файла: module_name_version.h (например, core_ntp_version.h)
    guard_name = f"{module_name.upper().replace('-', '_').replace('.', '_')}_VERSION_H"
    
    content = f'''// Auto-generated version file for module: {module_name}
// Generated: {now.strftime('%Y-%m-%d %H:%M')}
// Environment: {env_name}

#ifndef {guard_name}
#define {guard_name}

// ============================================================
// ВЕРСИЯ МОДУЛЯ {module_name}
// ============================================================

#define {module_name.upper()}_VERSION {version}

// ============================================================
// ДАТА ПОСЛЕДНЕГО ИЗМЕНЕНИЯ
// ============================================================

#define {module_name.upper()}_COMMIT_DATE "{commit_date if commit_date else '1970-01-01 00:00'}"
#define {module_name.upper()}_COMMIT_YEAR {commit_year}
#define {module_name.upper()}_COMMIT_MONTH {commit_month}
#define {module_name.upper()}_COMMIT_DAY {commit_day}
#define {module_name.upper()}_COMMIT_HOUR {commit_hour}
#define {module_name.upper()}_COMMIT_MINUTE {commit_minute}

// ============================================================
// ИНФОРМАЦИЯ О ГЕНЕРАЦИИ
// ============================================================

#define {module_name.upper()}_GENERATED_TIME "{now.strftime('%Y-%m-%d %H:%M')}"

#endif // {guard_name}
'''
    return content

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def generate_module_versions():
    """
    Генерирует файлы версий для всех core_* и module_* папок.
    Отслеживает изменения по хэшам и инкрементирует счётчики.
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
    
    # Собираем все папки с префиксами core_ и module_
    modules = []
    for item in src_dir.iterdir():
        if item.is_dir():
            if item.name.startswith(CORE_PREFIX) or item.name.startswith(MODULE_PREFIX):
                modules.append(item)
    
    if not modules:
        info_print("No core_* or module_* folders found")
        return
    
    info_print(f"Found {len(modules)} modules to process")
    info_print("-" * 60)
    
    # Читаем сохранённые версии
    stored_versions = read_module_versions(project_dir)
    current_versions = {}
    generated_count = 0
    
    for module_path in modules:
        module_name = module_path.name
        version_file = module_path / f"{module_name}_version.h"
        
        info_print(f"Processing: {module_name}")
        
        # Получаем текущий хэш папки
        current_hash = get_folder_hash(project_dir, f"src/{module_name}")
        
        # Получаем сохранённый хэш и версию
        stored_key = f"{module_name}_hash"
        stored_version = stored_versions.get(module_name, 0)
        
        # Проверяем, изменился ли хэш
        hash_file = project_dir / ".module_hashes"
        stored_hash = None
        if hash_file.exists():
            try:
                with open(hash_file, 'r') as f:
                    for line in f:
                        if line.startswith(f"{module_name}="):
                            stored_hash = line.strip().split('=')[1]
                            break
            except:
                pass
        
        # Если хэш изменился или нет сохранённого, инкрементируем версию
        new_version = stored_version
        if current_hash and (stored_hash != current_hash):
            new_version = stored_version + 1
            info_print(f"  Module changed: version {stored_version} -> {new_version}")
            
            # Сохраняем новый хэш
            hashes = {}
            if hash_file.exists():
                try:
                    with open(hash_file, 'r') as f:
                        for line in f:
                            if '=' in line:
                                k, v = line.strip().split('=', 1)
                                hashes[k] = v
                except:
                    pass
            
            hashes[module_name] = current_hash
            try:
                with open(hash_file, 'w') as f:
                    for k, v in hashes.items():
                        f.write(f"{k}={v}\n")
            except Exception as e:
                debug_print(f"Error writing hash file: {e}")
        else:
            new_version = stored_version
            info_print(f"  Module unchanged: version {stored_version}")
        
        current_versions[module_name] = new_version
        
        # Получаем дату последнего коммита
        commit_date = get_commit_date(project_dir, f"src/{module_name}")
        
        # Генерируем содержимое файла
        content = generate_module_header(
            module_name,
            new_version,
            commit_date,
            env_name
        )
        
        # Записываем файл
        try:
            version_file.write_text(content, encoding='utf-8')
            info_print(f"  Generated: {version_file.relative_to(project_dir)}")
            info_print(f"    Version: {new_version}")
            info_print(f"    Last commit: {commit_date if commit_date else 'unknown'}")
            generated_count += 1
        except Exception as e:
            info_print(f"  ERROR writing file: {e}")
        
        info_print("-" * 40)
    
    # Сохраняем обновлённые версии
    write_module_versions(project_dir, current_versions)
    
    info_print(f"\nGenerated {generated_count} module version files")
    info_print(f"{'='*60}\n")

# ============================================================
# ЗАПУСК
# ============================================================
generate_module_versions()