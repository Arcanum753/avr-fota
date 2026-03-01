import os
from pathlib import Path
Import("env")

# Этот скрипт запускается перед сборкой ФС
# Он читает переменную окружения PLATFORMIO_FS_DATA_DIR и перенаправляет data_dir

fs_data_dir = os.environ.get("PLATFORMIO_FS_DATA_DIR")

if fs_data_dir:
    print(f"\n{'='*60}")
    print(f"FS DATA DIR REDIRECT")
    print(f"{'='*60}")
    print(f"Using prepared FS data directory: {fs_data_dir}")
    
    # Проверяем, что папка существует
    if Path(fs_data_dir).exists():
        # Перенаправляем PlatformIO на использование подготовленной папки
        env.Replace(PROJECT_DATA_DIR=fs_data_dir)
        print(f"PROJECT_DATA_DIR set to: {fs_data_dir}")
    else:
        print(f"WARNING: Prepared FS data directory does not exist: {fs_data_dir}")
        print("Falling back to default data/ folder")
else:
    print("No PLATFORMIO_FS_DATA_DIR environment variable found, using default data/ folder")
    
print(f"{'='*60}\n")