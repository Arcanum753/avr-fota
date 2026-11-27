# Общие флаги размера и единая таблица разделов для всех ESP32-сборок.
#
# Скрипт подключается первым pre-скриптом в [env] platformio.ini и работает
# только для платформы espressif32 (ESP8266 не затрагивается).
#
# Зачем build_unflags: platformio-build-esp32.py добавляет -fexceptions в CXXFLAGS
# после пользовательских build_flags. ProcessUnFlags выполняется уже после
# framework-скриптов, поэтому -fexceptions удаляется детерминированно.

Import("env")

platform = str(env.get("PIOPLATFORM", ""))
if platform.startswith("espressif32"):
    env.Append(
        CFLAGS=["-fno-unwind-tables", "-fno-asynchronous-unwind-tables"],
        CXXFLAGS=[
            "-fno-exceptions",
            "-fno-unwind-tables",
            "-fno-asynchronous-unwind-tables",
        ],
    )

    unflags = env.get("BUILD_UNFLAGS", "")
    if isinstance(unflags, str):
        unflags = unflags.split()
    else:
        unflags = list(unflags or [])
    if "-fexceptions" not in unflags:
        unflags.append("-fexceptions")
    env.Replace(BUILD_UNFLAGS=" ".join(unflags))

    # Единая таблица для 4-МБ ESP32: перекрываем только дефолтные/legacy-значения,
    # чтобы не ломать явные нестандартные разметки (8-МБ платы, custom CSV).
    if "BOARD" in env:
        board = env.BoardConfig()
        flash_size = str(board.get(
            "upload.flash_size", board.get("build.flash_size", ""))).replace(" ", "").upper()
        current = str(board.get("build.partitions", ""))
        common = "partitions_esp32.csv"
        legacy = ("", "default.csv", common, "partitions_esp32_macro.csv")
        if flash_size == "4MB" and current in legacy:
            board.update("build.partitions", common)
