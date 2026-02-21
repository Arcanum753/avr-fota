import shutil
from pathlib import Path
Import("env")

def copy_bin_file(source, target, env):
    # Получаем имя текущей среды сборки
    env_name = env.subst("$PIOENV")
    
    # Путь к исходному файлу
    firmware_path = str(target[0])
    
    # Папка в корне проекта для собранных прошивок
    project_dir = Path(env.subst("$PROJECT_DIR"))
    firmware_dir = project_dir / "proj_fwbins"
    
    # Создаём папку, если её нет
    firmware_dir.mkdir(exist_ok=True)
    
    # Копируем в папку proj_fwbins с именем среды
    new_filename = firmware_dir / f"{env_name}.bin"
    shutil.copy2(firmware_path, new_filename)
    print(f"Copied firmware to: {new_filename}")
    
    # Также копируем в папку сборки (оставляем как было)
    build_dir = Path(firmware_path).parent
    local_filename = build_dir / f"{env_name}.bin"
    shutil.copy2(firmware_path, local_filename)
    print(f"Copied firmware to: {local_filename}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin_file)