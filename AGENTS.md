# AVR-FOTA Architecture & Build System

## Overview

Universal firmware for **ESP8266** and **ESP32** with a web interface for programming AVR (AtMega/AtTiny) and STM32 microcontrollers over LAN, plus remote device management. Forked from [FSBrowserNG](https://github.com/gmag11/FSBrowserNG) with ISP programming logic from [Standalone-Arduino-AVR-ISP-programmer](https://github.com/adafruit/Standalone-Arduino-AVR-ISP-programmer/) and SWD logic from [ESP32_nRF52_SWD](https://github.com/atc1441/ESP32_nRF52_SWD) / [blackmagic](https://codeberg.org/blackmagic-debug/blackmagic).

## Code style conventions

- **Сохранять существующий стиль кода.** Не менять форматирование, отступы, расположение скобок или пробелы без необходимости.
- **Запрещён `#elif`.** Все условные блоки оформляются только через `#if` / `#endif`. Никаких `#elif`/ `#else`. 
- **Новые комментарии — на русском языке.** Существующие комментарии (на любом языке) сохранять как есть, если они остаются актуальными. Устаревшие комментарии можно удалять или обновлять, заменяя на русский.

## Architecture

### Modular design

The project is split into **two categories**:

1. **Cores (core_\*)** — always present, provide base functionality
2. **Modules (module_\*)** — optional, added via `src_filter` and `build_flags` in target config

Submodules (`submodule_*`) inherit from `Class_ProgBase` and implement specific programmers. The base programmer logic lives in `module_prog/`.

### Core modules (always compiled)

| Module | Directory | Purpose |
|--------|-----------|---------|
| `core_wifi` | `src/core_wifi/` | Wi-Fi client/AP, 4 profile management, scanning |
| `core_ntp` | `src/core_ntp/` | NTP client with 3 servers (primary + 2 fallback) |
| `core_ota` | `src/core_ota/` | Self-update (FOTA) via web, FS version checking |
| `core_editor` | `src/core_editor/` | FS browser, file editor (html/txt/json/js), upload/delete |
| `core_json` | `src/core_json/` | JSON utilities (save/load/parse) |
| `core_led` | `src/core_led/` | LED indication macros (WiFi, errors, success, waiting) |
| `core_terminal` | `src/core_terminal/` | Serial terminal with debug/management commands |

### Optional modules

| Module | Flag | Purpose |
|--------|------|---------|
| `module_prog` + `submodule_isp` | `-D PROGTYPE_ISP` | AVR-ISP programmer (AtMega/AtTiny) |
| `module_prog` + `submodule_swd` | `-D PROGTYPE_SWD` | SWD programmer (STM32 F1/F4) |
| `module_gpio` | `-D MODULE_GPIO` | GPIO control via web |
| `module_lcd-i2c` | `-D MODULE_LCD_I2C` | LCD I2C display control (LiquidCrystal_I2C, маски date/time, backlight) |
| `module_udp` | `-D MODULE_UDP` | UDP broadcast for device discovery |
| `module_otaclient` | `-D MODULE_OTACLIENT=1` | OTA client (auto-update from remote server) |
| `module_template` | `-D MODULE_TEMPLATE` | Шаблон модуля — основа для создания новых модулей |
| `module_i2c-mapper` | `-D MODULE_I2C_MAPPER` | I2C bus scanner (web interface, Wire0) |

### module_template — шаблон нового модуля

`src/module_template/` содержит эталонную структуру optional-модуля. При создании нового модуля копировать эту папку и переименовывать.

**Что содержит шаблон (брать за основу):**
- `module_xxx.h` — класс с debug-макросом, `setFs()`, `begin()`, `web_Init()`, структурой конфига (`strXxxConfig`), версионными методами
- `module_xxx.cpp` — глобальный объект, загрузка/сохранение JSON конфига через `core_json`, AJAX-эндпоинты (`/xxx/info`, `/xxx/save`, `/xxx/ver`), ответ `text/plain "OK"`
  - `save_config()` использует `core_json.jsonFileLoadDoc()` + мерж (не перезапись), затем `jsonFileSaveDoc()`.
  - Чтение массивов из JSON — через `is<JsonArray>()` + `as<JsonArray>()` с проверкой границ.
  - Сохранение массивов — через `doc["key"].to<JsonArray>()` + `arr.add()`.
- `web/_menu.html` — ссылки в меню.
- `web/xxx.html` — HTML-страница с формой, JS через `fetch` и `ApplyCVT()` из общих файлов `GetJson.js`/`GetMarkup.js` (не дублировать `applyCvtData` на каждой странице), сохранение без перезагрузки страницы.
- `web/config_xxx.json` — дефолтный конфиг
- Интеграция в `FSWebServerLib.cpp` под флагом `MODULE_XXX`
- Таргеты в `targets/targets_example.ini`

**Единый порядок функций в .cpp модуля (обязательно для всех core_/module_/device_/submodule_):**

Шаблонный блок — всегда в начале файла, конкретная логика — после него.

1. **INCLUDES** — `#include "FSWebServerLib.h"` первым, затем библиотечные заголовки, `core_*/...`, свой `module_xxx.h`, `common.h`, `*_version.h`, `eertos.h`.
2. **ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПЕРЕМЕННЫЕ** — глобальный объект класса, конструктор, глобальные/static переменные. Здесь же forward-declarations свободных функций, используемых шаблонным блоком.
3. **setFs()**
4. **begin()** и **begin(ModContext&)**
5. **web_Init()** — регистрация HTTP-путей
6. **ВЕБ-ОБРАБОТЧИКИ** — `handleInfo`/`handleSave`/`cmdXxxWeb` и вспомогательные методы веб-полей
7. **КОНФИГ** — `defaultConfig` / `loadConfig` / `saveConfig`
8. **ВЕРСИОННЫЕ МЕТОДЫ** — `getVersionStr` / `getGeneratedTime` / `getCommitDateStr` / `html_ver_get`
9. **КОНКРЕТНАЯ ЛОГИКА МОДУЛЯ** — свободные функции (ISR, колбэки, `xxxTerminalRegister`), геттеры/публичное API, инициализация периферии, задачи/state machines, терминальные команды

Разделы разделяются баннером `// ============================================================` + название на русском. Свободная функция, используемая в шаблонном блоке, должна быть объявлена в `.h` модуля или forward-declared в разделе 2.

Порядок объявлений в `.h` соответствует порядку в `.cpp`: `public:` конструктор → `setFs` → `begin` → `begin(ctx)` → `web_Init` → публичное API; `private:` версионные методы → веб-обработчики → конфиг → логика; `protected:` поля (относительный порядок полей сохранять).

**Что НЕ брать из шаблона (заменить под свою логику):**
- Управление GPIO через `pinMode`/`digitalWrite` — это только пример. В новом модуле будет своя аппаратная логика.
- Отображение времени через `/xxx/time` — только как демонстрация периодического AJAX-опроса. В новом модуле заменить на свою периодическую задачу или удалить.
- Массив `demoArray` в конфиге — только как демонстрация паттерна `is<JsonArray>()`. В реальном модуле заменить на свои поля или удалить.

### EERTOS — Cooperative scheduler

`src/eertos.h` + `src/eertos.cpp` implement a cooperative task/timer dispatcher (not an RTOS):
- `SetTask(TPTR)` — queue a function for execution in the main loop
- `SetTimerTask(TPTR, uint32_t)` — schedule a function after N milliseconds
- `DelTimerTask(TPTR)` — remove a scheduled timer
- `TaskManager()` — called from `loop()`, dequeues and runs one task per call
- `TimerService()` — called from a 1ms Ticker ISR, decrements timer counters and pushes expired tasks

The main loop (`loop()` in `main.cpp`):
1. Resets watchdog
2. Calls `TaskManager()` — runs one queued task
3. Resets watchdog again
4. Calls `loop_user()` (user hook, empty by default)
5. Calls `TerminalLoop()`
6. Calls `core_loop()`, `modules_loop()`, `dev_loop()` (задача `core_ota.loop()`/`module_otaclient.loop()` вызывается через `core_loop`)

### Core initialization flow (`setup()`)

1. `InitRTOS()` — init EERTOS queues
2. `LittleFS.begin()` — mount filesystem
3. `ESPHTTPServer.begin(&LittleFS)` — starts the web server (`src/FSWebServerLib.cpp`):
   - Fills global `ModContext` (fs, hostname, password)
   - `core_begin(ModContext)` — core init (WiFi, NTP, JSON, editor, OTA)
   - `modules_begin(ModContext)` — optional modules init
   - `dev_begin(ModContext)` — devices init
   - `serverInit()` — register core HTTP routes
   - `core_web_Init()`, `modules_web_Init()`, `dev_web_Init()` — register web routes
   - `MDNS.begin()` — mDNS
   - Инициализация конкретных модулей регистрируется через `modules_registry` (см. ниже)
4. `TerminalInit()` — serial terminal (вызывается через `core_begin`, см. ниже)
5. `ledInit()` — LED GPIO init
6. `ledMacroTimerTask()` — start LED macro timer
7. `_secondEERtos.attach_ms(1, TimerService)` — start 1ms tick

### Key class hierarchy

```
Class_ProgBase (module_prog/module_prog.h)
├── Class_SubIsp (submodule_isp/) — AVR-ISP
└── Class_SubSwd (submodule_swd/) — STM32 SWD

CLASS_CORE_OTA (core_ota/core_ota.h)
└── CLASS_MODULE_OTACLIENT (module_otaclient/) — extended OTA client
```

### Именование классов: паттерн `CLASS_<ПРИНАДЛЕЖНОСТЬ>_<ФУНКЦИЯ>`

Имена классов образуются по паттерну `CLASS_<ПРИНАДЛЕЖНОСТЬ>_<ФУНКЦИЯ>`, где
принадлежность — категория модуля:

- Ядра: `CLASS_CORE_*` (например `CLASS_CORE_WIFI`, `CLASS_CORE_OTA`)
- Модули: `CLASS_MODULE_*` (например `CLASS_MODULE_GPIO`, `CLASS_MODULE_UDPBROADCAST`,
  `CLASS_MODULE_I2C_MAPPER`, `CLASS_MODULE_I2C_LCD`)
- Устройства: `CLASS_DEVICE_*` (например `CLASS_DEVICE_CLOCKMECH`, `CLASS_DEVICE_RINGMECH`)

Составные названия функции пишутся через подчёркивание: `I2C_MAPPER`, `I2C_LCD`.

Исключения (не переименовывать):
- `Class_ProgBase` (module_prog) — база субмодулей.
- `Class_SubIsp` / `Class_SubSwd` (submodule_*) — особый случай.
- Библиотечные/инфраструктурные классы (`SerialTerminal`, `AsyncWebServer*` и т.п.).

Глобальные объекты модулей НЕ переименовываются — только типы.

### Inclusion mechanism: единый контракт модулей + автогенерация registry

Все ядра/модули/устройства приводятся к единому контракту:
- `begin(ModContext& ctx)` — инициализация периферии + загрузка конфигов. `ModContext`
  (вын `src/mod_context.h`) содержит: `fs` (тип по `#if ESP32`/`#if ESP8266`),
  `hostname`, `password`. Метод устанавливает `_fs = ctx.fs` и вызывает существующий `begin()`.
- `web_Init()` — регистрация веб-путей (единое имя; историческое `webInit` удалено).
- `loop()` — периодическая задача (опционально, включается флагом `loop = 1`).

`src/FSWebServerLib.cpp` и `src/main.cpp` НЕ содержат ручного вызова `setFs`/`begin`/`webInit`
для каждого модуля. Вместо этого вызываются функции из автогенерируемого
`src/modules_registry.cpp`:
- `core_begin(ctx)`, `modules_begin(ctx)`, `dev_begin(ctx)`
- `core_web_Init()`, `modules_web_Init()`, `dev_web_Init()`
- `core_loop()`, `modules_loop()`, `dev_loop()`

Файл `src/modules_registry.cpp` генерируется сюкриптом `python/module_registry_gen.py`
**под выбранный env** (без `#if defined(...)`):
```bash
python python/module_registry_gen.py --env esp32_clock-mech
```

Определение включённых модулей:
- Ядра (`core_*`) — статический список в генераторе (включаются всегда).
- Модули/субмодули/устройства — извлекаются из `+<префикс>имя/>` в `src_filter` выбранного env.

Для каждого модуля генератор читает секцию `[registry]` из `src/<module>/<module>.ini`:
```ini
[registry]
object = module_ds3231
define = MODULE_DS3231
web = 1        # есть web_Init() — вызывается в *_web_Init
loop = 0       # есть loop() — вызывается в *_loop
```
- `object` — имя глобального extern-объекта (например `module_ds3231`, `progIsp`, `module_otaclient`).
- `define` — define-флаг env (справочно; сами `#if` в итоговый файл не пишутся).
- `web` — 1 если у модуля есть `web_Init()`.
- `loop` — 1 если у модуля есть `loop()`. Для `device_*` вызывается в `dev_loop()`, для остальных — в `modules_loop()`.

Особые случаи:
- `module_otaclient` (`module_otaclient`) при активном `-D MODULE_OTACLIENT` включаются/istр в **core**-группах
  (begin/web/loop) вместе с базовым OTA, а не в modules-группах.
- `module_udp` (`module_udp`) — `begin()` вызывается из `core_wifi` при подключении, поэтому
  `begin` в registry не дублируется; регистрируется только `web_Init()`.
- `core_terminal` — без класса; `TerminalInit()` вызывается в `core_begin`,
  `TerminalLoop()` — в `core_loop` (базовые команды регистрируются в begin, слоты
  модулей применяются лениво при первом вызове `TerminalLoop()`).
- `core_led` вне контракта — инициализируется вручную в `main.cpp` (`ledInit()`).

**ВАЖНОЕ ОГРАНИЧЕНИЕ:** `src/modules_registry.cpp` сгенерирован под ОДИН env и не содержит
`#if defined(...)`. При сборке другого env или при смене набора модулей необходимо
**перезапустить генератор** с новым `--env`, иначе линковка упадёт.

Source files are filtered by `src_filter` in `platformio.ini`:
```ini
src_filter = +<*> -<.git/> -<.vscode/> -<module_*/> -<submodule_*/> -<device_*/>
```
Modules are added per-target:
```ini
[env:esp32-swd]
extends = env:esp32
src_filter = ${platformio.src_filter} +<module_prog/> +<submodule_swd/> +<module_udp/>
build_flags = ${env.build_flags} -D MODULE_UDP=1 -D PROGTYPE_SWD=1 -D SWDPIN_CLK=21 -D SWDPIN_DATA=19
```
После изменения `src_filter`/`build_flags` в env — перезапустить `python/module_registry_gen.py --env <env>`.

### Web page structure

`data/page_head.html` contains a static menu and a marker `<!-- MODULES_RIGHT_COLUMN -->`. The build script `gen_page_head.py` replaces this marker with links generated from each module's `web/` directory:
- If a module has `web/_menu.html`, its content is used directly
- Otherwise, `.html` files are scanned for `<title>` or first heading

Module web files (e.g. `module_prog/web/prog.html`, `submodule_isp/web/avrcfg.html`) are copied into the FS build directory by `fs_builder.py`.

### File system config files (in `data/` and module `web/` dirs)

| File | Location | Purpose |
|------|----------|---------|
| `config_sys.json` | `data/` | Device name, serial, WiFi scan time, AP lifetime |
| `config_ntp.json` | `data/` | NTP server addresses, timezone, DST |
| `config_wifi0-3.json` | `data/` | 4 Wi-Fi profiles (SSID, password, DHCP/static IP) |
| `secret.json` | `data/` | HTTP auth login/password (hidden from FS browser) |
| `config_prog.json` | `module_prog/web/` | Programmer project config (chip, project name) |
| `config_udp.json` | `module_udp/web/` | UDP module config |
| `config_otaclient.json` | `module_otaclient/web/` | OTA client config |
| `avrisp_cfg.json` | `submodule_isp/web/` | AVR chip database (signature, flash size, page size) |
| `swd_cfg.json` | `submodule_swd/web/` | SWD chip database (IDCODE, flash params) |

### Serial terminal

`core_terminal` wraps the [ErriezSerialTerminal](https://github.com/Erriez/ErriezSerialTerminal) library. Commands registered (via `TerminalInit()`):
- `info` — system info
- `reset` — ESP restart
- `dirs` / `check` — FS operations
- `blink` — LED test
- `flash1` / `flash2` — programmer operations
- `stm32` / `swd` / `swdflash1` / `swdflash` — SWD debug
- `udpp` / `udpc` / `udps` — UDP module debug
- `avr` — AVR-ISP module debug

### Format handlers (`module_prog/`)

- `format_bin.h` — BIN file read API (open/read/close/isFormat)
- `format_hex.h` — Intel HEX parser with streaming validation, callback-based flash write (`hexFileParseStreamWrite`), binary size estimation (`hexFileGetBinarySize`)

## Build System

### Platform — PlatformIO

- Config: `platformio.ini`
- Base platforms: `espressif8266`, `espressif32`
- Framework: `arduino`
- File system: LittleFS
- ESP32 partition table: `partitions.csv` (2 OTA slots + spiffs)

### Environment presets

| env | Platform | Board | Notes |
|-----|----------|-------|-------|
| `esp8266` | espressif8266 | d1_mini | Base for ESP8266 targets |
| `esp32` | espressif32 | upesy_wroom | Base for ESP32 targets |
| `esp32cam` | espressif32 | esp32cam | Extends esp32 |

Target-specific configs: `targets/targets_example.ini` (examples) and `targets/targets_user.ini` (user).

### Build scripts (`python/`)

Run order and purpose:

**Pre-scripts** (before compilation):
1. `version_builder.py` — generates `src/version.h` with `MAJOR.MINOR.DATE.BUILD`, git info, auto-increment MINOR on commit change, auto-increment BUILD on every build. Protected against double-run via env var.
2. `module_version_gen.py` — generates `*_version.h` for each core/module (git-hash-based change detection)
3. `fs_builder.py` — prepares FS build directory at `web_debug/<env>/`, copies files from `data/` and module `web/` dirs, generates `_version_fs.json`, calls `gen_page_head.py`, redirects `PLATFORMIO_FS_DATA_DIR`
4. `set_fs_data_dir.py` — sets PlatformIO's `PROJECT_DATA_DIR` to the prepared directory

**Ручной запуск (before compilation, при смене env/набора модулей):**
- `module_registry_gen.py` — generates `src/modules_registry.cpp/.h` under selected env (обязательно, см. `### Inclusion mechanism`)

**Post-scripts** (after compilation):
5. `copy_fw.py` — copies `firmware.bin` → `proj_fwbins/{ENV}-FIRMWARE-{VERSION}.bin`
6. `copy_fs.py` — copies `littlefs.bin` → `proj_fwbins/{ENV}-FILESYS-{VERSION}.bin`

### Version format

`MAJOR.MINOR.DATE.BUILD` (e.g., `0.034.20260614_2351.0801`)

- `MAJOR` (1 digit) — manual in `version_counter.txt`
- `MINOR` (3 digits) — manual, auto-incremented on git commit change
- `DATE` — `%Y%m%d_%H%M`
- `BUILD` (4 digits) — auto-incremented each compilation

Module versions: independent numeric version per module, incremented when module file content hash changes. Stored in `.module_versions` / `.module_hashes`.

### CI/CD

GitHub Actions (`.github/workflows/platformio_ci.yml`):
- Triggers: push/PR to main/master/dev
- Matrix builds across 6 targets
- Caches PlatformIO dependencies
- Uploads build logs on failure

### Version files

- `version_counter.txt` — MAJOR (line 1), MINOR (line 2)
- `build_counter.txt` — BUILD number
- `.last_commit_hash` — tracked by `version_builder.py`
- `.module_versions` — current module version numbers
- `.module_hashes` — content hashes per module (for change detection)
- `src/version.h` — auto-generated C header with all version macros + git info
- `src/*_version.h` — per-module version headers (e.g., `core_wifi_version.h`)

### File system version file

`_version_fs.json` is generated by `fs_builder.py` and placed in the FS root. Contains:
- Full firmware version + components
- Git info (branch, commit, dirty flag, tag)
- Per-module versions and dates
- FS statistics (size, used, free, file count, build date)

## Directory structure

```
avr-fota/
├── data/                    # FS source files (HTML, configs, images)
│   ├── page_head.html       # HTML template with <!-- MODULES_RIGHT_COLUMN -->
│   ├── config_sys.json      # System configuration
│   ├── config_wifi0-3.json  # Wi-Fi profiles
│   ├── config_ntp.json      # NTP configuration
│   ├── secret.json          # HTTP auth credentials
│   └── ...                  # HTML pages, JS, CSS, images
├── python/                  # Build scripts
│   ├── version_builder.py   # Version header generation
│   ├── module_version_gen.py # Per-module version generation
│   ├── module_registry_gen.py # Registry autogeneration under selected env
│   ├── fs_builder.py        # FS image preparation
│   ├── gen_page_head.py     # Dynamic page header generation
│   ├── set_fs_data_dir.py   # FS data directory redirect
│   ├── copy_fw.py           # Firmware binary copy
│   └── copy_fs.py           # FS binary copy
├── src/                     # Source code
│   ├── main.cpp             # Entry point (setup/loop)
│   ├── main.h               # Project-wide defines
│   ├── common.h/cpp         # Utility functions
│   ├── eertos.h/cpp         # Cooperative task scheduler
│   ├── FSWebServerLib.h/cpp # Async web server + routing
│   ├── mod_context.h        # Module init context (fs, hostname, password)
│   ├── modules_registry.h/cpp # Generated module registry (begin/web_Init/loop)
│   ├── version.h            # Auto-generated version header
│   ├── debug.h              # Debug logging macros
│   ├── StringArray.h        # Linked list utility
│   ├── TimeLib.h / Time.cpp # Time library fork
│   ├── ESPAsyncWebServer.h  # Async TCP/HTTP wrapper header
│   ├── core_wifi/           # Wi-Fi core module
│   ├── core_ntp/            # NTP core module
│   ├── core_ota/            # OTA core module
│   ├── core_editor/         # FS editor core module
│   ├── core_json/           # JSON utilities core module
│   ├── core_led/            # LED indication core module
│   ├── core_terminal/       # Serial terminal core module
│   ├── module_prog/         # Base programmer module (base class)
│   ├── module_gpio/         # GPIO control module
│   ├── module_udp/          # UDP broadcast module
│   ├── module_otaclient/    # OTA client module
│   ├── submodule_isp/       # AVR-ISP programmer submodule
│   ├── submodule_swd/       # STM32 SWD programmer submodule
│   └── *version.h           # Auto-generated per-module version headers
├── targets/                 # PlatformIO target configs
│   ├── targets_example.ini  # Example target definitions
│   └── targets_user.ini     # User target definitions
├── web_debug/               # Prepared FS build directory (generated)
├── proj_fwbins/             # Built firmware + FS binaries (generated)
├── .pio/                    # PlatformIO build artifacts
├── partitions.csv           # ESP32 partition table
├── platformio.ini           # PlatformIO project config
├── library.json             # Library metadata
├── .github/workflows/       # CI/CD configs
└── AGENTS.md                # This file
```

### Debug macros

Each module has a dedicated debug flag and macro:
- `DEBUG_OTA` → `DEBUGOTA(...)`
- `DEBUG_NTP` → `DEBUGNTP(...)`
- `DEBUG_JSON` → `DEBUGJSON(...)`
- `DEBUG_EDITOR` → `DEBUGEDIT(...)`
- `DEBUG_LED` → `DEBUGLOGLED(...)`
- `DEBUGLOG_WIFI` → `DEBUGLOGWIFI(...)`
- `DEBUG_PROG` → `DEBUGLOGPROG(...)`
- `DEBUG_ISP` → `DEBUGLOGISP(...)`
- `DEBUG_SWD` → `DEBUGLOGSWD(...)`
- `DEBUG_UDP` → `DEBUGUDP(...)`
- `DEBUG_OTACLIENT` → `DEBUGOTACLIENT(...)`
- `DEBUG_I2C_MAPPER` → `DEBUGI2CMAPPER(...)`
- `RELEASE` defined → all debug macros are no-ops

### Key defines

- `CONNECTION_LED` — GPIO for status LED (default -1 = disabled)
- `AP_ENABLE_BUTTON` — GPIO for force-AP button (default -1 = disabled)
- `USE_LITTLEFS` — enable LittleFS filesystem
- `HIDE_SECRET` — hide secret.json from FS browser
- `PROGTYPE_ISP` / `PROGTYPE_SWD` — enable programmer submodules
- `SWDPIN_CLK`, `SWDPIN_DATA` — SWD pin assignment
- `PIN_MISO`, `PIN_MOSI`, `PIN_SCK`, `PIN_RST` — ISP pin assignment
