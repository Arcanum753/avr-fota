import os
import sys
import shutil
import datetime
import subprocess
import time
import re
from pathlib import Path
Import("env")

# ============================================================
# КОНФИГУРАЦИЯ (аналог #define в си)
# ============================================================

# Папки для выходных данных
WEB_DEBUG_ROOT = "web_debug"           # корневая папка для отладки веба
WEB_PREFIX = "web_"                     # префикс для папок конкретных сборок
FW_BINS_ROOT = "proj_fwbins"            # папка для бинарников (можно изменить здесь)

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
    """Проверяет, выполняется ли сейчас сборка ФС, с защитой от рекурсии"""
    # Проверка 1: по аргументам командной строки
    for arg in sys.argv:
        if "--target" in arg and "buildfs" in arg:
            return True
    
    # Проверка 2: по переменной окружения (защита от рекурсии)
    if os.environ.get("PLATFORMIO_FS_BUILD") == "1":
        return True
    
    return False

def get_included_modules_from_src_filter(env):
    """
    Получает список модулей, которые включены в src_filter текущего окружения.
    Ищет паттерны +<module_*> в фильтре.
    """
    # Получаем src_filter для текущего окружения
    src_filter = env.subst("${SRC_FILTER}")
    if not src_filter:
        # Если нет специфичного для окружения, берём из platformio
        src_filter = env.subst("${platformio.src_filter}")
    
    print(f"  Parsing src_filter: {src_filter}")
    
    # Ищем все вхождения +<module_имя/>
    pattern = r'\+<module_([^>/]+)/?>'
    matches = re.findall(pattern, src_filter)
    
    # Добавляем префикс module_ обратно
    modules = [f"{MODULE_PREFIX}{match}" for match in matches]
    
    if modules:
        print(f"  Found included modules: {', '.join(modules)}")
    else:
        print("  No module_* patterns found in src_filter")
    
    return modules

def get_core_modules_with_web(src_dir):
    """Возвращает список core_* модулей, у которых есть папка web"""
    core_modules = []
    if src_dir.exists():
        for item in src_dir.iterdir():
            if item.is_dir() and item.name.startswith(CORE_PREFIX):
                web_dir = item / WEB_FOLDER_NAME
                if web_dir.exists():
                    core_modules.append(item.name)
                    print(f"  Found core module with web: {item.name}")
    return core_modules

def create_symlink(target, link_name):
    """Создаёт символическую ссылку"""
    try:
        if link_name.exists() or link_name.is_symlink():
            link_name.unlink()
        os.symlink(target, link_name, target_is_directory=True)
        return True
    except Exception as e:
        print(f"  Note: Could not create symlink: {e}")
        return False

def write_build_info(file_path, env_name, included_modules, file_count):
    """Записывает информацию о сборке"""
    with open(file_path, 'w', encoding='utf-8') as f:
        f.write(f"Build environment: {env_name}\n")
        f.write(f"Build time: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Included modules with web: {', '.join(included_modules)}\n")
        f.write(f"Total files: {file_count}\n")

def copy_fs_image(source, target, env):
    """Копирует образ ФС в папку с бинарниками"""
    env_name = env.subst("$PIOENV")
    project_dir = Path(env.subst("$PROJECT_DIR"))
    target_dir = project_dir / FW_BINS_ROOT
    target_dir.mkdir(exist_ok=True)
    
    build_dir = Path(str(target[0])).parent
    
    for fs_name in FS_BIN_NAMES:
        fs_src = build_dir / fs_name
        if fs_src.exists():
            fs_dst = target_dir / f"{env_name}_fs.bin"
            shutil.copy2(fs_src, fs_dst)
            print(f"FS image copied to: {fs_dst}")
            break

# ============================================================
# ОСНОВНАЯ ФУНКЦИЯ
# ============================================================

def prepare_fs_image():
    """Собирает ФС из общих файлов и web-папок модулей, указанных в src_filter"""
    
    env_name = env.subst("$PIOENV")
    print(f"\n{'='*60}")
    print(f"FS BUILD for: {env_name}")
    print(f"{'='*60}")
    
    project_dir = Path(env.subst("$PROJECT_DIR"))
    
    # 1. Папка с общими файлами
    data_dir = project_dir / DATA_FOLDER
    if not data_dir.exists():
        data_dir.mkdir(exist_ok=True)
    
    # 2. Корневая папка для отладки
    web_debug_dir = project_dir / WEB_DEBUG_ROOT
    web_debug_dir.mkdir(exist_ok=True)
    
    # 3. Папка для этой сборки
    target_web_dir = web_debug_dir / f"{WEB_PREFIX}{env_name}"
    
    # Очистка старой папки
    if target_web_dir.exists():
        shutil.rmtree(target_web_dir, ignore_errors=True)

    target_web_dir.mkdir(parents=True)
    print(f"Target web directory: {target_web_dir}")
    
    # 4. Копируем общие файлы из data/
    common_files = 0
    for item in data_dir.iterdir():
        if item.is_file():
            shutil.copy2(item, target_web_dir / item.name)
            common_files += 1
    print(f"Copied {common_files} common files from {DATA_FOLDER}/")
    
    # 5. Получаем список модулей, включённых в src_filter
    src_dir = project_dir / SRC_FOLDER
    included_modules = get_included_modules_from_src_filter(env)
    
    # 6. Добавляем core_* модули с web (они всегда включены)
    core_modules = get_core_modules_with_web(src_dir)
    all_web_modules = included_modules + core_modules
    
    # 7. Копируем web-файлы из найденных модулей
    web_files = 0
    overwritten_files = 0
    
    for module_name in all_web_modules:
        module_web = src_dir / module_name / WEB_FOLDER_NAME
        if module_web.exists():
            for item in module_web.iterdir():
                if item.is_file():
                    dest_path = target_web_dir / item.name
                    was_common = dest_path.exists()
                    shutil.copy2(item, dest_path)
                    web_files += 1
                    if was_common:
                        overwritten_files += 1
                        print(f"  ✓ {module_name}/{WEB_FOLDER_NAME}/{item.name} (overwrites common)")
                    else:
                        print(f"  + {module_name}/{WEB_FOLDER_NAME}/{item.name}")
    
    print(f"Copied {web_files} web files from modules ({overwritten_files} overwrites)")
    
    # 8. Информационный файл
    info_file = target_web_dir / "_build_info.txt"
    write_build_info(info_file, env_name, all_web_modules, common_files + web_files - overwritten_files)
    print(f"Created build info: {info_file}")
    
    # 9. Перенаправляем PlatformIO
    env.Replace(PROJECT_DATA_DIR=str(target_web_dir))
    
    return target_web_dir
# ============================================================
# ГЛАВНЫЙ ПРОЦЕСС
# ============================================================

if is_fs_build():
    print("\nFS build detected - skipping preparation to avoid recursion")
else:
    target_web_dir = prepare_fs_image()
    
    if CREATE_CURRENT_SYMLINK and target_web_dir:
        web_debug_dir = Path(env.subst("$PROJECT_DIR")) / WEB_DEBUG_ROOT
        current_link = web_debug_dir / "web_current"
        if create_symlink(target_web_dir, current_link):
            print(f"Created symlink: {current_link} -> {target_web_dir}")
    
    # Регистрируем копирование образа ФС после его сборки
    for fs_name in FS_BIN_NAMES:
        env.AddPostAction(f"$BUILD_DIR/{fs_name}", copy_fs_image)
    
    # Устанавливаем переменную окружения перед запуском buildfs
    os.environ["PLATFORMIO_FS_BUILD"] = "1"
    
    print("\nTriggering filesystem build...")
    env_name = env.subst("$PIOENV")
    project_dir = env.subst("$PROJECT_DIR")
    
    def run_fs_build():
        time.sleep(1)
        # Передаём переменную окружения дочернему процессу
        subprocess.run(
            ["pio", "run", "--target", "buildfs", "--environment", env_name],
            cwd=project_dir,
            env={**os.environ, "PLATFORMIO_FS_BUILD": "1"}
        )
    
    import threading
    threading.Thread(target=run_fs_build, daemon=True).start()