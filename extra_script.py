import shutil
from pathlib import Path
Import("env")

def copy_bin_file(source, target, env):
    # Получаем имя текущей среды сборки
    env_name = env.subst("$PIOENV")
    
    # Путь к исходному файлу
    firmware_path = str(target[0])
    
    # Новое имя файла
    build_dir = Path(firmware_path).parent
    new_filename = build_dir / f"{env_name}.bin"
    
    # Копируем с новым именем
    shutil.copy2(firmware_path, new_filename)
    print(f"Copied firmware to: {new_filename}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin_file)