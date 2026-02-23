# import shutil
# from pathlib import Path
# Import("env")

# def copy_bin_file(source, target, env):
#     # Получаем имя текущей среды сборки
#     env_name = env.subst("$PIOENV")
    
#     # Путь к исходному файлу
#     firmware_path = str(target[0])
    
#     # Папка в корне проекта для собранных прошивок
#     project_dir = Path(env.subst("$PROJECT_DIR"))
#     firmware_dir = project_dir / "proj_fwbins"
    
#     # Создаём папку, если её нет
#     firmware_dir.mkdir(exist_ok=True)
    
#     # Копируем в папку proj_fwbins с именем среды
#     new_filename = firmware_dir / f"{env_name}.bin"
#     shutil.copy2(firmware_path, new_filename)
#     print(f"Copied firmware to: {new_filename}")
    
#     # Также копируем в папку сборки (оставляем как было)
#     build_dir = Path(firmware_path).parent
#     local_filename = build_dir / f"{env_name}.bin"
#     shutil.copy2(firmware_path, local_filename)
#     print(f"Copied firmware to: {local_filename}")

# env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin_file)

import shutil
import re
from pathlib import Path
Import("env")

def get_firmware_version(project_dir):
    """
    Читает FIRMWARE_VERSION из version_full.h
    Возвращает строку вида "0.022.10.5451" или None, если файл не найден
    """
    version_file = project_dir / "src" / "version_full.h"
    if not version_file.exists():
        print("Warning: version_full.h not found")
        return None
    
    content = version_file.read_text(encoding='utf-8')
    
    # Ищем макрос FIRMWARE_VERSION
    match = re.search(r'#define FIRMWARE_VERSION "([^"]+)"', content)
    if match:
        return match.group(1)
    
    print("Warning: FIRMWARE_VERSION not found in version_full.h")
    return None

def copy_bin_file(source, target, env):
    # Получаем имя текущей среды сборки
    env_name = env.subst("$PIOENV")
    
    # Путь к исходному файлу прошивки
    firmware_path = str(target[0])
    
    # Папка в корне проекта для собранных прошивок
    project_dir = Path(env.subst("$PROJECT_DIR"))
    firmware_dir = project_dir / "proj_fwbins"
    
    # Создаём папку, если её нет
    firmware_dir.mkdir(exist_ok=True)
    
    # Получаем версию прошивки
    version = get_firmware_version(project_dir)
    
    if version:
        # Формируем имя с версией: esp32-swd-0.022.10.5451.bin
        base_name = f"{env_name}-{version}.bin"
    else:
        # Если версии нет, используем только имя среды
        base_name = f"{env_name}.bin"
        print("Using environment name only (no version)")
    
    # Копируем прошивку в корневую папку
    firmware_dst = firmware_dir / base_name
    shutil.copy2(firmware_path, firmware_dst)
    print(f"Copied firmware to: {firmware_dst}")
    
    # Копируем прошивку в папку сборки (с именем среды и версией)
    build_dir = Path(firmware_path).parent
    local_firmware = build_dir / base_name
    shutil.copy2(firmware_path, local_firmware)
    print(f"Copied firmware to: {local_firmware}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin_file)