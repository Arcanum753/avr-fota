import os
import sys
import shutil
import datetime
import subprocess
import time
import re
import tempfile
import json
from pathlib import Path
from typing import List, Optional, Tuple, Set, Dict, Any
Import("env")

# ============================================================
# КОНФИГУРАЦИОННЫЕ КОНСТАНТЫ (все настройки здесь)
# ============================================================


# ------------------- Директории проекта -------------------
DATA_FOLDER = "data"                     # папка с общими файлами
SRC_FOLDER = "src"                       # папка с исходниками
FW_BINS_ROOT = "proj_fwbins"            # папка для бинарников
WEB_DEBUG_ROOT = "web_debug"            # корневая папка для отладки веба
WEB_PREFIX = "web_"                     # префикс для папок конкретных сборок

# ------------------- Имена модулей и папок -------------------
MODULE_PREFIX = "module_"                # префикс модулей
SUBMODULE_PREFIX = "submodule_"          # префикс субмодулей
CORE_PREFIX = "core_"                    # префикс ядерных модулей
DEVICE_PREFIX = "device_"                # префикс девайс-модулей
WEB_FOLDER_NAME = "web"                  # имя папки с веб-файлами внутри модуля

# ------------------- Имена бинарников файловых систем -------------------
FS_BIN_NAMES = ["spiffs.bin", "littlefs.bin"]

# ------------------- Флаги поведения сборки -------------------
CREATE_CURRENT_SYMLINK = True            # создавать симлинк web_current
SYMLINK_ATOMIC = True                    # использовать атомарную замену симлинка
FS_BUILD_TIMEOUT_SEC = 300               # таймаут сборки ФС (секунды)
FS_BUILD_RETRY_COUNT = 1                 # количество попыток перезапуска FS сборки
FS_BUILD_RETRY_DELAY_SEC = 2             # задержка между попытками (секунды)

# ------------------- Настройки JSON файла версии -------------------
VERSION_FILE_NAME = "_version_fs.json"    # имя JSON файла с версией ФС
GENERATE_FS_VERSION_JSON = True          # генерировать JSON файл с версией ФС
INCLUDE_BUILD_INFO_IN_JSON = True        # включать информацию о сборке в JSON
FS_VERSION_SCHEMA_VERSION = "1.0.0"      # версия схемы JSON

# ------------------- Файлы версий (от version_builder.py) -------------------
VERSION_COUNTER_FILE = "version_counter.txt"   # major и minor
BUILD_COUNTER_FILE = "build_counter.txt"       # build счётчик
VERSION_HEADER_FILE = "version.h"              # заголовочный файл (как источник)

# ------------------- Ограничения и таймауты -------------------
FILE_COPY_RETRY_COUNT = 3                # количество попыток копирования файла
FILE_COPY_RETRY_DELAY_MS = 100           # задержка между попытками (миллисекунды)
MAX_FILENAME_LENGTH = 255                # максимальная длина имени файла
MAX_TOTAL_FILES = 10000                  # максимальное количество файлов для сборки

# ------------------- Временные файлы -------------------
TEMP_SYMLINK_SUFFIX = ".tmp"             # суффикс для временного симлинка

# ------------------- Коды возврата -------------------
EXIT_SUCCESS = 0
EXIT_GENERAL_ERROR = 1
EXIT_FS_BUILD_FAILED = 2
EXIT_COPY_FAILED = 3
EXIT_INVALID_MODULE_NAME = 4

# ============================================================
# ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
# ============================================================

_FS_BUILD_IN_PROGRESS = False

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def log_info(msg: str):
    print(f"[FS Builder] {msg}")

def log_warning(msg: str):
    print(f"[FS Builder] WARNING: {msg}")

def log_error(msg: str):
    print(f"[FS Builder] ERROR: {msg}")

def log_debug(msg: str):
    if os.environ.get("FS_BUILDER_DEBUG") == "1":
        print(f"[FS Builder] DEBUG: {msg}")

# ============================================================
# ИМПОРТ ГЕНЕРАТОРА page_head.html (с проверкой существования)
# ============================================================
try:
    from gen_page_head import generate_page_head
    _HAS_PAGE_HEAD_GEN = True
    log_info("gen_page_head module loaded successfully")
except ImportError:
    _HAS_PAGE_HEAD_GEN = False
    log_warning("gen_page_head module not found — page_head.html will not be dynamically generated")
except Exception as e:
    _HAS_PAGE_HEAD_GEN = False
    log_warning(f"Failed to load gen_page_head module: {e}")

def is_fs_build() -> bool:
    global _FS_BUILD_IN_PROGRESS
    
    for arg in sys.argv:
        if "--target" in arg and "buildfs" in arg:
            return True
    
    if _FS_BUILD_IN_PROGRESS:
        return True
    
    if os.environ.get("PLATFORMIO_FS_BUILD") == "1":
        _FS_BUILD_IN_PROGRESS = True
        return True
    
    return False

def validate_module_name(module_name: str) -> bool:
    if not module_name:
        return False
    
    if ".." in module_name:
        log_error(f"Invalid module name (contains '..'): {module_name}")
        return False
    
    if "/" in module_name or "\\" in module_name:
        log_error(f"Invalid module name (contains path separator): {module_name}")
        return False
    
    if "\0" in module_name:
        log_error(f"Invalid module name (contains null byte): {module_name}")
        return False
    
    if len(module_name) > MAX_FILENAME_LENGTH:
        log_error(f"Module name too long: {len(module_name)} > {MAX_FILENAME_LENGTH}")
        return False
    
    safe_pattern = re.compile(r'^[a-zA-Z0-9_-]+$')
    if not safe_pattern.match(module_name):
        log_warning(f"Module name contains unusual characters: {module_name}")
    
    return True

def safe_copy_file(src: Path, dst: Path, retry_count: int = FILE_COPY_RETRY_COUNT) -> bool:
    for attempt in range(retry_count):
        try:
            temp_dst = dst.with_suffix(dst.suffix + ".tmp")
            shutil.copy2(src, temp_dst)
            
            if temp_dst != dst:
                temp_dst.replace(dst)
            
            return True
        except (OSError, IOError, shutil.Error) as e:
            if attempt < retry_count - 1:
                time.sleep(FILE_COPY_RETRY_DELAY_MS / 1000.0)
                continue
            log_error(f"Failed to copy {src} to {dst} after {retry_count} attempts: {e}")
            return False
    return False

def parse_src_filter(src_filter: str) -> List[str]:
    if not src_filter:
        return []
    
    modules: Set[str] = set()
    
    prefixes = [MODULE_PREFIX, SUBMODULE_PREFIX, DEVICE_PREFIX]
    
    pattern_prefix_pairs = []
    for prefix in prefixes:
        pattern_prefix_pairs.append((r'\+<' + prefix + r'([^>/]+)', prefix))
        pattern_prefix_pairs.append((r'\+' + prefix + r'([^/\s]+)', prefix))
    
    for pattern, prefix in pattern_prefix_pairs:
        matches = re.findall(pattern, src_filter)
        for match in matches:
            module_name = f"{prefix}{match}"
            if validate_module_name(module_name):
                modules.add(module_name)
    
    result = sorted(list(modules))
    
    if result:
        log_info(f"Found included modules from src_filter: {', '.join(result)}")
    else:
        log_debug("No module_* patterns found in src_filter")
    
    return result

def get_core_modules_with_web(src_dir: Path) -> List[str]:
    core_modules = []
    
    if not src_dir.exists():
        log_warning(f"Source directory does not exist: {src_dir}")
        return core_modules
    
    try:
        for item in src_dir.iterdir():
            if not item.is_dir():
                continue
            
            if not item.name.startswith(CORE_PREFIX):
                continue
            
            if not validate_module_name(item.name):
                continue
            
            web_dir = item / WEB_FOLDER_NAME
            if web_dir.exists() and web_dir.is_dir():
                core_modules.append(item.name)
                log_debug(f"Found core module with web: {item.name}")
    except (OSError, IOError) as e:
        log_error(f"Error scanning core modules: {e}")
    
    return core_modules

def create_symlink_atomic(target: Path, link_name: Path) -> bool:
    if not SYMLINK_ATOMIC:
        return create_symlink_direct(target, link_name)
    
    temp_link = link_name.with_suffix(link_name.suffix + TEMP_SYMLINK_SUFFIX)
    
    try:
        if not create_symlink_direct(target, temp_link):
            return False
        
        if link_name.exists() or link_name.is_symlink():
            link_name.unlink()
        temp_link.rename(link_name)
        return True
    except Exception as e:
        log_warning(f"Atomic symlink creation failed: {e}")
        return create_symlink_direct(target, link_name)

def create_symlink_direct(target: Path, link_name: Path) -> bool:
    try:
        if link_name.exists() or link_name.is_symlink():
            link_name.unlink()
        
        if sys.platform == "win32":
            os.symlink(target, link_name, target_is_directory=True)
        else:
            os.symlink(target, link_name)
        
        return True
    except (OSError, IOError, NotImplementedError) as e:
        log_warning(f"Could not create symlink {link_name} -> {target}: {e}")
        return False

# ============================================================
# ФУНКЦИИ ДЛЯ ЧТЕНИЯ ВЕРСИИ ИЗ ФАЙЛОВ version_builder.py
# ============================================================

def read_version_from_counter_file(project_dir: Path) -> Dict[str, Any]:
    """
    Читает major и minor из version_counter.txt (создаётся version_builder.py)
    Формат файла:
        major=0
        minor=17
    """
    version_info = {
        "major": 0,
        "minor": 0,
        "date": 0,
        "date_str": "",
        "build": 0,
        "full_string": "0.0.0.0",
        "is_debug": False
    }
    
    counter_file = project_dir / VERSION_COUNTER_FILE
    
    if not counter_file.exists():
        log_warning(f"Version counter file not found: {counter_file}")
        log_warning(f"Run version_builder.py first or create {VERSION_COUNTER_FILE}")
        return version_info
    
    try:
        with open(counter_file, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if '=' in line:
                    key, value = line.split('=')
                    key = key.strip()
                    value = value.strip()
                    if key == "major":
                        version_info["major"] = int(value)
                        log_debug(f"Read major: {version_info['major']}")
                    elif key == "minor":
                        version_info["minor"] = int(value)
                        log_debug(f"Read minor: {version_info['minor']}")
    except Exception as e:
        log_error(f"Error reading {VERSION_COUNTER_FILE}: {e}")
    
    return version_info

def read_build_from_counter_file(project_dir: Path) -> int:
    """
    Читает build номер из build_counter.txt (создаётся version_builder.py)
    Формат файла: просто число
    """
    build_file = project_dir / BUILD_COUNTER_FILE
    build = 0
    
    if not build_file.exists():
        log_warning(f"Build counter file not found: {build_file}")
        log_warning(f"Run version_builder.py first to initialize build counter")
        return build
    
    try:
        with open(build_file, 'r', encoding='utf-8') as f:
            content = f.read().strip()
            if content:
                build = int(content)
                log_debug(f"Read build: {build}")
    except Exception as e:
        log_error(f"Error reading {BUILD_COUNTER_FILE}: {e}")
    
    return build

def read_version_from_header(project_dir: Path) -> Dict[str, Any]:
    """
    Альтернативный метод: читает версию напрямую из version.h
    Используется если counter файлы недоступны
    """
    version_info = {
        "major": 0,
        "minor": 0,
        "date": 0,
        "date_str": "",
        "build": 0,
        "full_string": "0.0.0.0",
        "is_debug": False
    }
    
    header_file = project_dir / SRC_FOLDER / VERSION_HEADER_FILE
    
    if not header_file.exists():
        header_file = project_dir / VERSION_HEADER_FILE
    
    if not header_file.exists():
        return version_info
    
    try:
        with open(header_file, 'r', encoding='utf-8') as f:
            content = f.read()
        
        # Ищем VERSION_MAJOR
        major_match = re.search(r'#define\s+VERSION_MAJOR\s+(\d+)', content)
        if major_match:
            version_info["major"] = int(major_match.group(1))
        
        # Ищем VERSION_MINOR
        minor_match = re.search(r'#define\s+VERSION_MINOR\s+(\d+)', content)
        if minor_match:
            version_info["minor"] = int(minor_match.group(1))
        
        # Ищем VERSION_DATE_STR (с _) или VERSION_DATE
        date_str_match = re.search(r'#define\s+VERSION_DATE_STR\s+"(\d+_\d+)"', content)
        if date_str_match:
            version_info["date_str"] = date_str_match.group(1)
            version_info["date"] = int(date_str_match.group(1).replace('_', ''))
        else:
            date_match = re.search(r'#define\s+VERSION_DATE\s+(\d+)', content)
            if date_match:
                d = date_match.group(1)
                version_info["date"] = int(d)
                version_info["date_str"] = d[:8] + '_' + d[8:] if len(d) > 8 else d
        
        # Ищем VERSION_BUILD
        build_match = re.search(r'#define\s+VERSION_BUILD\s+(\d+)', content)
        if build_match:
            version_info["build"] = int(build_match.group(1))
        
        # Пробуем взять полную строку из FIRMWARE_VERSION
        fw_match = re.search(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"', content)
        if fw_match:
            version_info["full_string"] = fw_match.group(1)
        else:
            # Формируем сами с ведущими нулями
            version_info["full_string"] = f"{version_info['major']}.{version_info['minor']:03d}.{version_info.get('date_str', str(version_info['date']))}.{version_info['build']:04d}"
        version_info["is_debug"] = version_info["build"] > 0
        
        if version_info["major"] > 0 or version_info["minor"] > 0:
            log_debug(f"Version from header: {version_info['full_string']}")
        
    except Exception as e:
        log_debug(f"Failed to read version header: {e}")
    
    return version_info

def get_current_version(project_dir: Path) -> Dict[str, Any]:
    """
    Получает текущую версию, читая из файлов version_builder.py.
    Приоритет:
    1. version_counter.txt + build_counter.txt
    2. version.h (если нет counter файлов)
    """
    # Сначала пробуем читать из counter файлов
    version_info = read_version_from_counter_file(project_dir)
    build_num = read_build_from_counter_file(project_dir)
    
    # Если major и minor не прочитаны из counter, пробуем из header
    if version_info["major"] == 0 and version_info["minor"] == 0:
        log_debug("No counter files found, trying version.h")
        version_info = read_version_from_header(project_dir)
    else:
        version_info["build"] = build_num
        date_str = datetime.datetime.now().strftime("%Y%m%d_%H%M")
        version_info["date_str"] = date_str
        version_info["date"] = int(date_str.replace('_', ''))
        version_info["full_string"] = f"{version_info['major']}.{version_info['minor']:03d}.{date_str}.{build_num:04d}"
        version_info["is_debug"] = build_num > 0
    
    return version_info

def format_file_size(size_bytes: int) -> str:
    for unit in ['B', 'KB', 'MB', 'GB']:
        if size_bytes < 1024.0:
            return f"{size_bytes:.1f} {unit}"
        size_bytes /= 1024.0
    return f"{size_bytes:.1f} TB"

def calculate_total_size(target_web_dir: Path) -> Tuple[int, int]:
    total_size = 0
    total_files = 0
    
    try:
        for item in target_web_dir.iterdir():
            if item.is_file() and item.name != VERSION_FILE_NAME:
                total_size += item.stat().st_size
                total_files += 1
    except Exception as e:
        log_warning(f"Failed to calculate total size: {e}")
    
    return total_files, total_size

def get_git_info(project_dir: Path) -> Dict[str, str]:
    """
    Получает Git информацию для JSON (опционально)
    """
    info = {
        "branch": "",
        "commit": "",
        "tag": ""
    }
    
    try:
        # Проверяем наличие git
        subprocess.run(["git", "--version"], capture_output=True, check=True)
    except:
        return info
    
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"],
            cwd=str(project_dir),
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            info["branch"] = result.stdout.strip()
    except:
        pass
    
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%h"],
            cwd=str(project_dir),
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            info["commit"] = result.stdout.strip()
    except:
        pass
    
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--exact-match"],
            cwd=str(project_dir),
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            info["tag"] = result.stdout.strip()
    except:
        pass
    
    return info

def generate_fs_version_json(target_web_dir: Path, env_name: str, 
                             included_modules: List[str],
                             project_dir: Path) -> Optional[Path]:
    """
    Генерирует JSON файл с информацией о версии файловой системы.
    Версия читается из файлов version_builder.py
    """
    if not GENERATE_FS_VERSION_JSON:
        return None
    
    log_info("Generating FS version JSON file...")
    
    # Получаем версию из файлов version_builder.py
    version_info = get_current_version(project_dir)
    
    log_info(f"  Version: {version_info['full_string']}")
    log_info(f"  Major: {version_info['major']}, Minor: {version_info['minor']}")
    log_info(f"  Build: {version_info['build']}, Date: {version_info['date']}")
    
    # Рассчитываем статистику по файлам
    total_files, total_size = calculate_total_size(target_web_dir)
    
    # Получаем Git информацию (опционально)
    git_info = get_git_info(project_dir)
    
    # Формируем структуру JSON
    fs_version_data = {
        "schema_version": FS_VERSION_SCHEMA_VERSION,
        "filesystem": {
            "name": f"FS_{env_name}",
            "environment": env_name,
            "build_time": datetime.datetime.now().isoformat(),
            "build_timestamp": int(time.time()),
            "version": {
                "major": version_info["major"],
                "minor": version_info["minor"],
                "date": version_info["date"],
                "build": version_info["build"],
                "full_string": version_info["full_string"],
                "is_debug": version_info["is_debug"]
            }
        },
        "modules": {
            "included": included_modules,
            "total_count": len(included_modules)
        },
        "statistics": {
            "total_files": total_files,
            "total_size_bytes": total_size,
            "total_size_human": format_file_size(total_size)
        }
    }
    
    # Добавляем Git информацию если доступна
    if git_info["branch"] or git_info["commit"]:
        fs_version_data["git"] = {k: v for k, v in git_info.items() if v}
    
    # Добавляем информацию о сборке если нужно
    if INCLUDE_BUILD_INFO_IN_JSON:
        fs_version_data["build_info"] = {
            "builder": "PlatformIO FS Builder",
            "python_version": sys.version.split()[0],
            "platform": sys.platform,
            "source_data_folder": DATA_FOLDER,
            "web_folder_name": WEB_FOLDER_NAME,
            "version_source_files": {
                "counter_file": VERSION_COUNTER_FILE,
                "build_file": BUILD_COUNTER_FILE
            }
        }
    
    # Сохраняем JSON файл
    json_file_path = target_web_dir / VERSION_FILE_NAME
    
    try:
        with open(json_file_path, 'w', encoding='utf-8') as f:
            json.dump(fs_version_data, f, indent=2, ensure_ascii=False)
        
        file_size = json_file_path.stat().st_size
        log_info(f"FS version JSON generated: {json_file_path} ({format_file_size(file_size)})")
        return json_file_path
        
    except Exception as e:
        log_error(f"Failed to write FS version JSON: {e}")
        return None

def write_build_info(file_path: Path, env_name: str, included_modules: List[str], 
                     file_count: int, overwritten_count: int) -> None:
    try:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(f"Build environment: {env_name}\n")
            f.write(f"Build time: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
            f.write(f"Included modules with web: {', '.join(included_modules) if included_modules else 'None'}\n")
            f.write(f"Total unique files: {file_count}\n")
            f.write(f"Overwritten files: {overwritten_count}\n")
            f.write(f"Source data folder: {DATA_FOLDER}\n")
            f.write(f"Web folder name: {WEB_FOLDER_NAME}\n")
            f.write(f"Version JSON: {VERSION_FILE_NAME}\n")
    except (OSError, IOError) as e:
        log_error(f"Failed to write build info: {e}")

def copy_fs_image(source, target, env) -> None:
    try:
        env_name = env.subst("$PIOENV")
        project_dir = Path(env.subst("$PROJECT_DIR"))
        target_dir = project_dir / FW_BINS_ROOT
        
        target_dir.mkdir(exist_ok=True)
        
        build_dir = Path(str(target[0])).parent
        
        copied = False
        for fs_name in FS_BIN_NAMES:
            fs_src = build_dir / fs_name
            if fs_src.exists():
                fs_dst = target_dir / f"{env_name}_fs.bin"
                
                if fs_dst.exists():
                    backup = fs_dst.with_suffix(fs_dst.suffix + ".bak")
                    try:
                        shutil.copy2(fs_dst, backup)
                        log_debug(f"Created backup: {backup}")
                    except Exception as e:
                        log_warning(f"Failed to create backup: {e}")
                
                if safe_copy_file(fs_src, fs_dst):
                    log_info(f"FS image copied to: {fs_dst}")
                    copied = True
                    break
        
        if not copied:
            log_error(f"No FS image found among: {', '.join(FS_BIN_NAMES)}")
            
    except Exception as e:
        log_error(f"Unexpected error in copy_fs_image: {e}")

def check_disk_space(path: Path, required_mb: int = 50) -> bool:
    try:
        usage = shutil.disk_usage(path)
        free_mb = usage.free / (1024 * 1024)
        if free_mb < required_mb:
            log_error(f"Insufficient disk space: {free_mb:.1f} MB free, need {required_mb} MB")
            return False
        return True
    except Exception as e:
        log_warning(f"Cannot check disk space: {e}")
        return True

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def prepare_fs_image() -> Optional[Path]:
    global _FS_BUILD_IN_PROGRESS
    
    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    
    log_info(f"=" * 60)
    log_info(f"FS BUILD for: {env_name}")
    log_info(f"=" * 60)
    
    # Проверяем наличие файлов версии (предупреждаем если нет)
    version_file = project_dir / VERSION_COUNTER_FILE
    build_file = project_dir / BUILD_COUNTER_FILE
    
    if not version_file.exists():
        log_warning(f"{VERSION_COUNTER_FILE} not found!")
        log_warning(f"Run version_builder.py first to initialize version counters")
    
    if not build_file.exists():
        log_warning(f"{BUILD_COUNTER_FILE} not found!")
        log_warning(f"Run version_builder.py first to initialize build counter")
    
    # Проверка свободного места
    if not check_disk_space(project_dir, 50):
        return None
    
    # 1. Папка с общими файлами
    data_dir = project_dir / DATA_FOLDER
    if not data_dir.exists():
        log_warning(f"Data directory does not exist, creating: {data_dir}")
        try:
            data_dir.mkdir(parents=True, exist_ok=True)
        except Exception as e:
            log_error(f"Failed to create data directory: {e}")
            return None
    
    # 2. Корневая папка для отладки
    web_debug_dir = project_dir / WEB_DEBUG_ROOT
    try:
        web_debug_dir.mkdir(exist_ok=True)
    except Exception as e:
        log_error(f"Failed to create web debug directory: {e}")
        return None
    
    # 3. Папка для этой сборки
    target_web_dir = web_debug_dir / f"{WEB_PREFIX}{env_name}"
    
    if target_web_dir.exists():
        log_debug(f"Removing existing directory: {target_web_dir}")
        try:
            shutil.rmtree(target_web_dir, ignore_errors=False)
        except Exception as e:
            log_error(f"Failed to remove existing directory: {e}")
            return None
    
    try:
        target_web_dir.mkdir(parents=True)
        log_info(f"Target web directory: {target_web_dir}")
    except Exception as e:
        log_error(f"Failed to create target directory: {e}")
        return None
    
    # 4. Копируем общие файлы из data/
    common_files = 0
    
    try:
        for item in data_dir.iterdir():
            if item.is_file():
                if not validate_module_name(item.name):
                    log_warning(f"Skipping file with invalid name: {item.name}")
                    continue
                
                dst_path = target_web_dir / item.name
                if safe_copy_file(item, dst_path):
                    common_files += 1
                    
                    if common_files > MAX_TOTAL_FILES:
                        log_error(f"Too many files ({common_files} > {MAX_TOTAL_FILES})")
                        return None
    except Exception as e:
        log_error(f"Error copying common files: {e}")
        return None
    
    log_info(f"Copied {common_files} common files from {DATA_FOLDER}/")
    
    # 5. Получаем список модулей, включённых в src_filter
    src_dir = project_dir / SRC_FOLDER
    included_modules = parse_src_filter(env.subst("${SRC_FILTER}"))
    
    # 6. Добавляем core_* модули с web
    core_modules = get_core_modules_with_web(src_dir)
    all_web_modules = []
    
    for module in included_modules:
        if module not in all_web_modules:
            all_web_modules.append(module)
    
    for module in core_modules:
        if module not in all_web_modules:
            all_web_modules.append(module)
    
    if all_web_modules:
        log_info(f"Total modules with web: {len(all_web_modules)}")
    
    # 7. Копируем web-файлы из найденных модулей
    web_files = 0
    overwritten_files = 0
    
    for module_name in all_web_modules:
        if not validate_module_name(module_name):
            log_error(f"Skipping invalid module name: {module_name}")
            continue
        
        module_web = src_dir / module_name / WEB_FOLDER_NAME
        if not module_web.exists():
            log_debug(f"Module {module_name} has no web folder")
            continue
        
        if not module_web.is_dir():
            log_warning(f"Module {module_name} web path is not a directory")
            continue
        
        try:
            for item in module_web.iterdir():
                if not item.is_file():
                    continue
                
                # Пропускаем служебные файлы (начинающиеся с _)
                if item.name.startswith("_"):
                    log_debug(f"  - {module_name}/{WEB_FOLDER_NAME}/{item.name} (skipped, service file)")
                    continue
                
                if not validate_module_name(item.name):
                    log_warning(f"Skipping file with invalid name in {module_name}: {item.name}")
                    continue
                
                dst_path = target_web_dir / item.name
                was_overwrite = dst_path.exists()
                
                if safe_copy_file(item, dst_path):
                    web_files += 1
                    
                    if was_overwrite:
                        overwritten_files += 1
                        log_debug(f"  ✓ {module_name}/{WEB_FOLDER_NAME}/{item.name} (overwrites common)")
                    else:
                        log_debug(f"  + {module_name}/{WEB_FOLDER_NAME}/{item.name}")
        except Exception as e:
            log_error(f"Error copying files from module {module_name}: {e}")
            return None
    
    unique_files = common_files + web_files - overwritten_files
    log_info(f"Copied {web_files} web files from modules ({overwritten_files} overwrites)")
    log_info(f"Total unique files: {unique_files}")
    
    # 8. Генерируем динамический page_head.html на основе включённых модулей
    if _HAS_PAGE_HEAD_GEN:
        try:
            # Используем module_* и submodule_* имена (не core_*) для правой колонки меню
            module_only_names = [m for m in all_web_modules 
                                 if m.startswith(MODULE_PREFIX) or m.startswith(SUBMODULE_PREFIX) or m.startswith(DEVICE_PREFIX)]
            
            page_head_html = generate_page_head(module_only_names, src_dir=str(src_dir))
            page_head_path = target_web_dir / "page_head.html"
            
            with open(page_head_path, 'w', encoding='utf-8') as f:
                f.write(page_head_html)
            
            log_info(f"Generated dynamic page_head.html for modules: {module_only_names}")
        except Exception as e:
            log_warning(f"Failed to generate page_head.html: {e}")
            log_warning("Using static page_head.html from data/ folder")
    else:
        log_info("Using static page_head.html from data/ folder (gen_page_head module not available)")
    
    # 10. Генерируем JSON файл с версией ФС (читая из version_builder файлов)
    if GENERATE_FS_VERSION_JSON:
        json_file = generate_fs_version_json(
            target_web_dir, 
            env_name, 
            all_web_modules,
            project_dir
        )
    
    # 11. Информационный файл
    info_file = target_web_dir / "_build_info.txt"
    write_build_info(info_file, env_name, all_web_modules, unique_files, overwritten_files)
    
    # 12. Перенаправляем PlatformIO
    try:
        env.Replace(PROJECT_DATA_DIR=str(target_web_dir))
        log_info(f"Redirected PROJECT_DATA_DIR to {target_web_dir}")
    except Exception as e:
        log_error(f"Failed to replace PROJECT_DATA_DIR: {e}")
        return None
    
    log_info(f"FS preparation completed successfully")
    return target_web_dir

def run_fs_build_with_retry(project_dir: Path, env_name: str, target_web_dir: Path) -> bool:
    for attempt in range(FS_BUILD_RETRY_COUNT + 1):
        if attempt > 0:
            log_info(f"Retry FS build attempt {attempt}/{FS_BUILD_RETRY_COUNT}")
            time.sleep(FS_BUILD_RETRY_DELAY_SEC)
        
        try:
            result = subprocess.run(
                ["pio", "run", "--target", "buildfs", "--environment", env_name],
                cwd=project_dir,
                env={
                    **os.environ,
                    "PLATFORMIO_FS_BUILD": "1",
                    "PLATFORMIO_FS_DATA_DIR": str(target_web_dir)
                },
                timeout=FS_BUILD_TIMEOUT_SEC,
                capture_output=False
            )
            
            if result.returncode == EXIT_SUCCESS:
                log_info("FS build completed successfully")
                return True
            else:
                log_error(f"FS build failed with code {result.returncode}")
                
        except subprocess.TimeoutExpired:
            log_error(f"FS build timed out after {FS_BUILD_TIMEOUT_SEC} seconds")
        except Exception as e:
            log_error(f"Unexpected error during FS build: {e}")
    
    return False

# ============================================================
# ГЛАВНЫЙ ПРОЦЕСС
# ============================================================

def main():
    global _FS_BUILD_IN_PROGRESS
    
    if is_fs_build():
        log_info("FS build already in progress - skipping preparation to avoid recursion")
        return
    
    try:
        _FS_BUILD_IN_PROGRESS = True
        os.environ["PLATFORMIO_FS_BUILD"] = "1"
        
        target_web_dir = prepare_fs_image()
        
        if not target_web_dir:
            log_error("Failed to prepare FS image data")
            sys.exit(EXIT_GENERAL_ERROR)
        
        if CREATE_CURRENT_SYMLINK:
            web_debug_dir = Path(env.subst("$PROJECT_DIR")) / WEB_DEBUG_ROOT
            current_link = web_debug_dir / "web_current"
            
            if create_symlink_atomic(target_web_dir, current_link):
                log_info(f"Created symlink: {current_link} -> {target_web_dir}")
            else:
                log_warning(f"Failed to create symlink to {target_web_dir}")
        
        os.environ["PLATFORMIO_FS_DATA_DIR"] = str(target_web_dir)
        
        for fs_name in FS_BIN_NAMES:
            env.AddPostAction(f"$BUILD_DIR/{fs_name}", copy_fs_image)
        
        env_name = env.subst("$PIOENV")
        project_dir = Path(env.subst("$PROJECT_DIR"))
        
        log_info("Starting filesystem build...")
        
        if not run_fs_build_with_retry(project_dir, env_name, target_web_dir):
            log_error("Filesystem build failed after all retries")
            sys.exit(EXIT_FS_BUILD_FAILED)
        
        log_info("Filesystem build process completed")
        
    except KeyboardInterrupt:
        log_info("Build interrupted by user")
        sys.exit(EXIT_GENERAL_ERROR)
    except Exception as e:
        log_error(f"Unexpected error in main: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(EXIT_GENERAL_ERROR)
    finally:
        _FS_BUILD_IN_PROGRESS = False

if __name__ == "__main__":
    main()
else:
    main()