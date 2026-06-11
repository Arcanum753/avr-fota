# copy_fs.py
import shutil
import re
from pathlib import Path
Import("env")

# ============================================================
# КОНФИГУРАЦИЯ (как #define в Си)
# ============================================================

# Отладка
# DEBUG = True  # Установите False для отключения вывода
DEBUG = False  # Установите True для отладочного вывода

# Папка для бинарников
FW_BINS_ROOT = "proj_fwbins"

# Имя файла с версией
VERSION_HEADER = "version.h"
VERSION_HEADER_PATH = f"src/{VERSION_HEADER}"

# Имена бинарников ФС
FS_BIN_NAMES = ["spiffs.bin", "littlefs.bin"]

# Суффикс для файла ФС (перед .bin)
FS_FILE_SUFFIX = "_fs"

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def debug_print(*args, **kwargs):
    """Выводит сообщение только если DEBUG = True"""
    if DEBUG:
        print(*args, **kwargs)

def get_firmware_version(project_dir):
    """
    Читает FIRMWARE_VERSION из version.h
    Возвращает строку вида "0.022.10.5451" или None, если файл не найден
    """
    version_file = project_dir / VERSION_HEADER_PATH
    if not version_file.exists():
        debug_print(f"Warning: {VERSION_HEADER} not found")
        return None
    
    content = version_file.read_text(encoding='utf-8')
    
    # Ищем макрос FIRMWARE_VERSION
    match = re.search(r'#define FIRMWARE_VERSION "([^"]+)"', content)
    if match:
        version = match.group(1)
        debug_print(f"Found firmware version: {version}")
        return version
    
    debug_print(f"Warning: FIRMWARE_VERSION not found in {VERSION_HEADER}")
    return None

def format_version_for_filename(version):
    """
    Преобразует версию вида "0.15.20260403_1610.595" в "0.15.20260403_1610.595"
    (оставляет точки между компонентами, подчёркивание только внутри даты)
    """
    parts = version.split('.')
    if len(parts) == 4:
        # parts[0] - major
        # parts[1] - minor
        # parts[2] - date (с подчёркиванием)
        # parts[3] - build
        return f"{parts[0]}.{parts[1]}.{parts[2]}.{parts[3]}"
    return version

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def copy_fs_image(source, target, env):
    """
    Копирует образ файловой системы в папку с бинарниками.
    """
    # Получаем имя текущей среды сборки
    env_name = env.subst("$PIOENV")
    
    # Путь к исходному файлу (образу ФС)
    fs_path = str(target[0])
    
    # Папка в корне проекта для собранных бинарников
    project_dir = Path(env.subst("$PROJECT_DIR"))
    firmware_dir = project_dir / FW_BINS_ROOT
    
    # Создаём папку, если её нет
    firmware_dir.mkdir(exist_ok=True)
    
    # Получаем версию прошивки
    version = get_firmware_version(project_dir)
    
    if version:
        # Формируем имя с версией
        version_for_filename = format_version_for_filename(version)
        base_name = f"{env_name}-FILESYS-{version_for_filename}.bin"
    else:
        # Если версии нет, используем только имя среды
        base_name = f"{env_name}-FILESYS.bin"
        debug_print("Using environment name only (no version)")
    
    # Копируем образ ФС в корневую папку
    fs_dst = firmware_dir / base_name
    shutil.copy2(fs_path, fs_dst)
    print(f"Copied FS image to: {fs_dst}")
    
    # Копируем образ ФС в папку сборки (с именем среды и версией)
    build_dir = Path(fs_path).parent
    local_fs = build_dir / base_name
    shutil.copy2(fs_path, local_fs)
    print(f"Copied FS image to: {local_fs}")

# ============================================================
# РЕГИСТРАЦИЯ
# ============================================================

# Регистрируем на события сборки ФС
for fs_name in FS_BIN_NAMES:
    env.AddPostAction(f"$BUILD_DIR/{fs_name}", copy_fs_image)