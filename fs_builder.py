import os
import shutil
import datetime
import subprocess
import time
from pathlib import Path
Import("env")

# ============================================================
# КОНФИГУРАЦИЯ (аналог #define в си)
# ============================================================

# Папки для выходных данных
WEB_DEBUG_ROOT = "web_debug"           # корневая папка для отладки веба
WEB_PREFIX = "web_"                     # префикс для папок конкретных сборок
FW_BINS_ROOT = "proj_fwbins"            # папка для бинарников

# Откуда брать файлы
DATA_FOLDER = "data"                     # папка с общими файлами
SRC_FOLDER = "src"                       # папка с исходниками

# Маски для поиска модулей
MODULE_PREFIX = "module_"                 # префикс модулей (все module_*)
CORE_PREFIX = "core_"                     # префикс ядерных модулей (если у них есть web)

# Имя папки с веб-файлами внутри модуля
WEB_FOLDER_NAME = "web"

# Имена бинарников ФС (spiffs.bin / littlefs.bin)
FS_BIN_NAMES = ["spiffs.bin", "littlefs.bin"]

# Создавать ли симлинк web_current на последнюю сборку
CREATE_CURRENT_SYMLINK = True

# ============================================================
# ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
# ============================================================

def is_fs_build():
    """Проверяет, выполняется ли сейчас сборка ФС"""
    import sys
    for arg in sys.argv:
        if "--target" in arg and "buildfs" in arg:
            return True
    return False

def get_active_modules_by_env(env_name, src_dir):
    """
    Определяет активные модули для данного окружения.
    Смотрит на src_filter через PlatformIO или определяет по соглашениям.
    """
    active_modules = []
    
    if src_dir.exists():
        for item in src_dir.iterdir():
            if item.is_dir() and item.name.startswith(MODULE_PREFIX):
                module_name = item.name
                module_key = module_name[len(MODULE_PREFIX):].lower()
                if module_key in env_name.lower():
                    active_modules.append(module_name)
                    print(f"  Detected module by keyword: {module_name} (keyword: {module_key})")
    
    for item in src_dir.iterdir():
        if item.is_dir() and item.name.startswith(CORE_PREFIX):
            web_dir = item / WEB_FOLDER_NAME
            if web_dir.exists():
                active_modules.append(item.name)
                print(f"  Core module with web: {item.name}")
    
    if not active_modules:
        keywords = ["isp", "swd", "gpio", "http", "mqtt", "ble", "wifi"]
        for kw in keywords:
            if kw in env_name.lower():
                for item in src_dir.iterdir():
                    if item.is_dir() and item.name.startswith(MODULE_PREFIX):
                        if kw in item.name.lower():
                            active_modules.append(item.name)
                            print(f"  Fallback detected: {item.name} (contains '{kw}')")
    
    return list(set(active_modules))

def create_symlink(target, link_name):
    """Создаёт символическую ссылку (кросс-платформенно)"""
    try:
        if link_name.exists() or link_name.is_symlink():
            link_name.unlink()
        os.symlink(target, link_name, target_is_directory=True)
        return True
    except Exception as e:
        print(f"  Note: Could not create symlink: {e}")
        return False

def write_build_info(file_path, env_name, active_modules, file_count):
    """Записывает информацию о сборке в текстовый файл"""
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(f"Build environment: {env_name}\n")
        f.write(f"Build time: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Active modules with web: {', '.join(active_modules)}\n")
        f.write(f"Total files: {file_count}\n")

# ============================================================
# ОСНОВНЫЕ ФУНКЦИИ
# ============================================================

def prepare_fs_image():
    """Собирает ФС из общих файлов и web-папок активных модулей"""
    
    env_name = env.subst("$PIOENV")
    print(f"\n{'='*60}")
    print(f"FS BUILD for: {env_name}")
    print(f"{'='*60}")
    
    project_dir = Path(env.subst("$PROJECT_DIR"))
    
    # 1. Базовая папка с общими файлами
    data_dir = project_dir / DATA_FOLDER
    if not data_dir.exists():
        data_dir.mkdir(exist_ok=True)
    
    # 2. Корневая папка для отладки
    web_debug_dir = project_dir / WEB_DEBUG_ROOT
    web_debug_dir.mkdir(exist_ok=True)
    
    # 3. Папка для этой конкретной сборки
    target_web_dir = web_debug_dir / f"{WEB_PREFIX}{env_name}"
    
    # Создаём новую папку (если есть старая, удалим позже)
    if target_web_dir.exists():
        # Не удаляем сразу, а переименовываем для отложенного удаления
        old_dir = target_web_dir.with_name(f"{WEB_PREFIX}{env_name}_old")
        if old_dir.exists():
            shutil.rmtree(old_dir, ignore_errors=True)
        target_web_dir.rename(old_dir)
        # Запланируем удаление через отдельный процесс
        def cleanup_old():
            time.sleep(2)
            if old_dir.exists():
                shutil.rmtree(old_dir, ignore_errors=True)
        import threading
        threading.Thread(target=cleanup_old, daemon=True).start()
    
    target_web_dir.mkdir(parents=True)
    print(f"Target web directory: {target_web_dir}")
    
    # 4. Копируем все общие файлы из DATA_FOLDER
    common_files = 0
    for item in data_dir.iterdir():
        if item.is_file():
            shutil.copy2(item, target_web_dir / item.name)
            common_files += 1
    print(f"Copied {common_files} common files from {DATA_FOLDER}/")
    
    # 5. Получаем список активных модулей
    src_dir = project_dir / SRC_FOLDER
    active_modules = get_active_modules_by_env(env_name, src_dir)
    
    # 6. Копируем web-файлы из активных модулей
    web_files = 0
    for module_name in active_modules:
        module_web = src_dir / module_name / WEB_FOLDER_NAME
        if module_web.exists():
            for item in module_web.iterdir():
                if item.is_file():
                    dest_path = target_web_dir / item.name
                    shutil.copy2(item, dest_path)
                    web_files += 1
                    print(f"  + {module_name}/{WEB_FOLDER_NAME}/{item.name}")
    
    print(f"Copied {web_files} web files from modules")
    
    # 7. Создаём информационный файл
    info_file = target_web_dir / "_build_info.txt"
    write_build_info(info_file, env_name, active_modules, common_files + web_files)
    print(f"Created build info: {info_file}")
    
    # 8. Перенаправляем PlatformIO на использование этой папки
    env.Replace(PROJECT_DATA_DIR=str(target_web_dir))
    
    return target_web_dir

def copy_all_binaries(source, target, env):
    """Копирует прошивку и образ ФС в папку с бинарниками"""
    
    env_name = env.subst("$PIOENV")
    print(f"\n{'='*60}")
    print(f"COPYING BINARIES for: {env_name}")
    print(f"{'='*60}")
    
    project_dir = Path(env.subst("$PROJECT_DIR"))
    target_dir = project_dir / FW_BINS_ROOT
    target_dir.mkdir(exist_ok=True)
    
    build_dir = Path(str(target[0])).parent
    
    # Копируем прошивку
    firmware_src = build_dir / "firmware.bin"
    if firmware_src.exists():
        firmware_dst = target_dir / f"{env_name}.bin"
        shutil.copy2(firmware_src, firmware_dst)
        print(f"Firmware: {firmware_dst}")
    
    # Копируем образ ФС
    for fs_name in FS_BIN_NAMES:
        fs_src = build_dir / fs_name
        if fs_src.exists():
            fs_dst = target_dir / f"{env_name}_fs.bin"
            shutil.copy2(fs_src, fs_dst)
            print(f"FS image: {fs_dst}")
            break
    
    print(f"{'='*60}\n")

# ============================================================
# ГЛАВНЫЙ ПРОЦЕСС
# ============================================================

# Если это сборка ФС - не делаем ничего, чтобы избежать рекурсии
if is_fs_build():
    print("\nFS build detected - skipping preparation to avoid recursion")
else:
    # Подготавливаем ФС
    target_web_dir = prepare_fs_image()

    # Создаём симлинк
    if CREATE_CURRENT_SYMLINK and target_web_dir:
        web_debug_dir = Path(env.subst("$PROJECT_DIR")) / WEB_DEBUG_ROOT
        current_link = web_debug_dir / "web_current"
        if create_symlink(target_web_dir, current_link):
            print(f"Created symlink: {current_link} -> {target_web_dir}")

    # Регистрируем копирование бинарников
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_all_binaries)

    # Запускаем сборку ФС в отдельном процессе
    print("\nTriggering filesystem build...")
    env_name = env.subst("$PIOENV")
    project_dir = env.subst("$PROJECT_DIR")
    
    # Запускаем с задержкой, чтобы избежать конфликтов
    def run_fs_build():
        time.sleep(1)
        subprocess.run(
            ["pio", "run", "--target", "buildfs", "--environment", env_name],
            cwd=project_dir
        )
    
    import threading
    threading.Thread(target=run_fs_build, daemon=True).start()