import shutil
from pathlib import Path
Import("env")

def copy_fs_image(source, target, env):
    env_name = env.subst("$PIOENV")
    
    # Путь к собранному образу ФС
    fs_path = str(target[0])
    
    project_dir = Path(env.subst("$PROJECT_DIR"))
    firmware_dir = project_dir / "proj_fwbins"
    firmware_dir.mkdir(exist_ok=True)
    
    # Копируем в корневую папку
    fs_dst = firmware_dir / f"{env_name}_fs.bin"
    shutil.copy2(fs_path, fs_dst)
    print(f"Copied FS image to: {fs_dst}")
    
    # Копируем в папку сборки (для локального доступа)
    build_dir = Path(fs_path).parent
    local_fs = build_dir / f"{env_name}_fs.bin"
    shutil.copy2(fs_path, local_fs)
    print(f"Copied FS image to: {local_fs}")

# Регистрируем на события сборки ФС
env.AddPostAction("$BUILD_DIR/spiffs.bin", copy_fs_image)
env.AddPostAction("$BUILD_DIR/littlefs.bin", copy_fs_image)