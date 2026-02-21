import subprocess
import os
import sys
import datetime
from pathlib import Path

# Получаем путь к проекту через переменные окружения PlatformIO
try:
    project_dir = Path(os.getcwd())
    print(f"\n{'='*60}")
    print("GIT VERSION GENERATOR")
    print(f"{'='*60}")
    print(f"Working directory: {project_dir}")
except:
    project_dir = Path(".").absolute()
    print(f"Using current directory: {project_dir}")

# Путь к папке src и файлу version.h
src_dir = project_dir / "src"
version_file = src_dir / "version.h"

print(f"Src directory: {src_dir}")
print(f"Version file: {version_file}")

def ensure_src_dir():
    """Создает папку src, если её нет"""
    if not src_dir.exists():
        print(f"Creating src directory: {src_dir}")
        src_dir.mkdir(parents=True, exist_ok=True)
        return True
    return True

def get_git_info():
    """Получает полную информацию из Git"""
    git_info = {
        "branch": "unknown",
        "commit": "unknown",
        "full_commit": "unknown",
        "tag": "unknown",
        "dirty": "false",
        "tag_distance": "0"
    }
    
    # Проверяем наличие git
    try:
        subprocess.run(["git", "--version"], capture_output=True, check=True)
        print("Git found")
    except:
        print("Git not found, using default values")
        return git_info
    
    # Получаем ветку
    try:
        branch = subprocess.check_output(
            ["git", "rev-parse", "--abbrev-ref", "HEAD"], 
            text=True,
            cwd=str(project_dir),
            encoding='utf-8'  # Явно указываем кодировку
        ).strip()
        git_info["branch"] = branch
        print(f"Git branch: {branch}")
    except Exception as e:
        print(f"Failed to get branch: {e}")
    
    # Получаем короткий хэш
    try:
        commit = subprocess.check_output(
            ["git", "log", "-1", "--format=%h"], 
            text=True,
            cwd=str(project_dir),
            encoding='utf-8'  # Явно указываем кодировку
        ).strip()
        git_info["commit"] = commit
        print(f"Git commit: {commit}")
    except Exception as e:
        print(f"Failed to get commit: {e}")
    
    # Получаем полный хэш
    try:
        full = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], 
            text=True,
            cwd=str(project_dir),
            encoding='utf-8'  # Явно указываем кодировку
        ).strip()
        git_info["full_commit"] = full
    except:
        pass
    
    # Получаем информацию о тегах
    try:
        # Пытаемся получить точный тег на текущем коммите
        exact_tag = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            text=True,
            cwd=str(project_dir),
            encoding='utf-8',  # Явно указываем кодировку
            stderr=subprocess.DEVNULL
        ).strip()
        git_info["tag"] = exact_tag
        git_info["tag_distance"] = "0"
        print(f"Exact tag: {exact_tag}")
    except:
        try:
            # Если нет точного тега, получаем ближайший тег с расстоянием
            describe = subprocess.check_output(
                ["git", "describe", "--tags", "--long"],
                text=True,
                cwd=str(project_dir),
                encoding='utf-8',  # Явно указываем кодировку
                stderr=subprocess.DEVNULL
            ).strip()
            
            # Парсим формат: tag-<distance>-g<commit>
            parts = describe.rsplit('-', 2)
            if len(parts) == 3:
                git_info["tag"] = parts[0]
                git_info["tag_distance"] = parts[1]
                print(f"Nearest tag: {git_info['tag']} (distance: {git_info['tag_distance']})")
            else:
                git_info["tag"] = describe
        except:
            git_info["tag"] = "no-tag"
            print("No tags found")
    
    # Проверяем dirty статус (несохраненные изменения)
    try:
        status = subprocess.check_output(
            ["git", "status", "--porcelain"], 
            text=True,
            cwd=str(project_dir),
            encoding='utf-8'  # Явно указываем кодировку
        ).strip()
        
        if status:
            git_info["dirty"] = "true"
            git_info["commit"] += "-dirty"
            git_info["full_commit"] += "-dirty"
            print("Working directory has uncommitted changes (dirty)")
        else:
            git_info["dirty"] = "false"
            print("Working directory is clean")
    except:
        pass
    
    return git_info

def get_env_name():
    """Получает имя текущей среды сборки из PlatformIO"""
    try:
        # Пробуем получить имя среды через PlatformIO
        Import("env")
        env_name = env.subst("$PIOENV")
        print(f"Build environment: {env_name}")
        return env_name
    except:
        # Если не в PlatformIO, пробуем через переменные окружения
        env_name = os.environ.get("PLATFORMIO_ENV", "unknown")
        print(f"Build environment (from env): {env_name}")
        return env_name

def generate_version_file(git_info):
    """Генерирует version.h файл с полной информацией"""
    
    # Получаем имя среды сборки
    env_name = get_env_name()
    
    # Получаем точное время сборки
    build_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    build_timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # Формируем версию на основе тега и коммита
    if git_info["tag"] != "unknown" and git_info["tag"] != "no-tag":
        if git_info["tag_distance"] != "0":
            full_version = f"{git_info['tag']}-{git_info['tag_distance']}-{git_info['commit']}"
        else:
            full_version = git_info["tag"]
    else:
        full_version = f"{git_info['branch']}-{git_info['commit']}"
    
    if git_info["dirty"] == "true":
        full_version += "-dirty"
    
    # Создаем содержимое
    content = f'''// Auto-generated version file
// Generated: {build_time}

#ifndef VERSION_H
#define VERSION_H
#define GIT_BRANCH "{git_info['branch']}"
#define GIT_COMMIT "{git_info['commit']}"
#define GIT_COMMIT_FULL "{git_info['full_commit']}"
#define GIT_TAG "{git_info['tag']}"
#define GIT_TAG_DISTANCE {git_info['tag_distance']}
#define GIT_DIRTY {git_info['dirty']}
#define GIT_VERSION "{full_version}"
#define BUILD_ENV "{env_name}"
#define BUILD_TIME "{build_time}"
#define BUILD_TIMESTAMP "{build_timestamp}"
#if GIT_DIRTY
#define IS_GIT_DIRTY true
#else
#define IS_GIT_DIRTY false
#endif

#endif // VERSION_H
'''
    
    # Записываем файл
    try:
        with open(version_file, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"Version file created: {version_file}")
        return True
    except Exception as e:
        print(f"Failed to write version file: {e}")
        return False

# Импортируем env из PlatformIO
try:
    Import("env")
    print("PlatformIO environment detected")
except:
    print("Running outside PlatformIO")
    env = None

def show_file_content():
    """Безопасно показывает содержимое файла"""
    if version_file.exists():
        print("\nFile content:")
        print("-"*40)
        try:
            # Читаем с явным указанием UTF-8
            with open(version_file, 'r', encoding='utf-8') as f:
                print(f.read())
        except Exception as e:
            print(f"Could not read file: {e}")
        print("-"*40)

def main():
    print("\nStarting version generation...")
    
    # Создаем папку src если нужно
    if not ensure_src_dir():
        print("Cannot create src directory")
        return False
    
    # Получаем Git информацию
    git_info = get_git_info()
    
    # Генерируем файл
    success = generate_version_file(git_info)
    
    # Получаем имя среды для вывода
    env_name = "unknown"
    try:
        Import("env")
        env_name = env.subst("$PIOENV")
    except:
        env_name = os.environ.get("PLATFORMIO_ENV", "unknown")
    
    print("="*60)
    if success:
        print("SUCCESS: Version file generated")
        show_file_content()
            
        print("\nVersion summary:")
        print(f"   Environment: {env_name}")
        print(f"   Branch: {git_info['branch']}")
        print(f"   Commit: {git_info['commit']}")
        print(f"   Tag: {git_info['tag']}")
        print(f"   Dirty: {git_info['dirty']}")
    else:
        print("FAILED: Version file not generated")
    print("="*60 + "\n")
    
    return success

# Запускаем main при импорте
if __name__ == "__main__" or "env" in locals():
    main()