---
name: live-debug
description: Отладка AVR-FOTA «на живую» на реальном устройстве ESP32/ESP8266 — сборка под env, прошивка по COM-порту, serial-монитор и терминал, проверка HTTP-эндпоинтов, OTA, чтение логов по DEBUG-флагам. Использовать для отладки/проверки на железе, когда нужно собрать, прошить и пронаблюдать устройство, а не гонять host-тесты.
---

# Живая отладка AVR-FOTA на устройстве

Процедура для проверки изменений на реальном ESP32/ESP8266: сборка под нужный env,
прошивка по COM-порту, наблюдение через serial-терминал и HTTP. Дополняет host-тесты
(`tests/`) — их гоняют без железа, а этот skill нужен, когда поведение зависит от драйверов,
таймингов, Wi-Fi, NTP, файловой системы или веб-страниц.

## 0. Предварительные требования

- PlatformIO установлен в `%USERPROFILE%\.platformio\penv\Scripts` (`platformio.exe`).
- **Обязательно добавь этот каталог в `PATH` перед запуском.** Скрипт
  `python/4_fs_builder.py` вызывает субпроцесс `["pio", ...]` для сборки FS; без `pio` в
  `PATH` сборка падает на `[WinError 2]` → `Filesystem build failed`.
- Проверка подключённых портов: `platformio device list`.
- На Windows предупреждение `Could not create symlink ... [WinError 1314]` — **безобидное**
  (нет прав на симлинки), сборку не ломает, если `pio` в `PATH`.

Стандартный префикс для любой команды в этой оболочке:

```powershell
$env:PATH = "$env:USERPROFILE\.platformio\penv\Scripts;$env:PATH"
```

## 1. Выбор env

Env определяется таргетом/компонентом. Найти все:

```powershell
Select-String -Path "platformio.ini","targets/*.ini","src/*/*.ini" -Pattern "^\[env:" |
  Select-Object Path, Line
```

Типовые env: `esp32`, `esp8266`, `esp32cam` (базы); `esp32_macro` (только макросы);
`esp32_electronica7_rgb_macros` / `esp32_electronica7_rgb` (устройство E7);
`esp32_clock-mech`, `esp32_clock-mech_ring`; `TestCore32` / `TestCore8266` (ядро без компонентов).

`src/modules_registry.cpp` и `src/modules_defines.h` **генерируются под один env** —
pre-скрипт `1_registry_pre_build.py` пересоздаёт их при каждой сборке. При смене env просто
собирай заново; вручную — `python python/module_registry_gen.py --env <env>`.

## 2. Сборка

```powershell
$env:PATH = "$env:USERPROFILE\.platformio\penv\Scripts;$env:PATH"
platformio run -e <env>
```

Прошивка и образ FS попадают в `proj_fwbins/<env>-FIRMWARE-<version>.bin` и
`<env>-FILESYS-<version>.bin` (post-скрипты 6/7). Версия автоинкрементится.
Для быстрой итерации удобнее `platformio run -e <env> -t upload` (соберёт и сразу прошьёт).

## 3. Прошивка

```powershell
# только firmware (FS и пользовательские макросы/конфиги НЕ трогаются)
platformio run -e <env> -t upload --upload-port COM3

# полная перепрошивка ФС (перезапишет /macros, config_*.json пользователя!)
platformio run -e <env> -t uploadfs --upload-port COM3
```

Правило: при правках **кода** прошивай только firmware. `uploadfs` — лишь когда менялись
веб-файлы/дефолтные конфиги в `data/` или `web/` модулей, и ты готов потерять правки на FS.
Перед `uploadfs` сохрани пользовательские сценарии (`/macros/*.lua`) и `config_*.json`.
Скорость `upload_speed` задаётся в env (обычно 921600); при сбоях загрузки снизь её.

## 4. Serial-монитор и терминал

```powershell
platformio device monitor -e <env> -p COM3   # 115200, фильтры из env
```

Встроенные отладочные макросы включаются флагом в `build_flags` env
(`-D DEBUG_SYS`, `-D DEBUG_CORE_WIFI`, `-D DEBUG_MACROS`, `-D DEBUG_STATE`, `-D DEBUG_OTA`,
`-D DEBUG_JSON`, `-D DEBUG_UDP`, `-D DEBUG_OTACLIENT`, `-D DEBUG_I2C_MAPPER`, `-D DEBUG_LED` и т.д.).
`-D RELEASE` гасит общие логи; `DEBUG_MACROS` и Wi-Fi-лог не гейтятся `RELEASE`.
Каждая строка модуля префиксуется `[C_]/[M_]/[D_]` через `DBG_MOD` (`src/debug.h`).

Терминальные команды (через serial): `help`, `reset`, `echo`, `?`, `id`, `led`;
при `MODULE_UDP` — `udpp/udpc/udps`; у макросов — `macro list|run|stop|reload|prio|msg|btn`;
`ds-*` (DS3231), `i2c-scan`, `c-*`/`r-*` (механические часы).

## 5. Проверка по HTTP (без перепрошивки)

Устройство доступно по IP (например `http://192.168.88.132`). Эндпоинты требуют
HTTP-auth (`secret.json`). Быстрая проверка PowerShell:

```powershell
(Invoke-WebRequest -Uri "http://<ip>/macros/list" -UseBasicParsing).Content
(Invoke-WebRequest -Uri "http://<ip>/macros/heap" -UseBasicParsing).Content
(Invoke-WebRequest -Uri "http://<ip>/state/info" -UseBasicParsing).Content
```

Полезные адреса: `/macros.html`, `/state/catalog`, `/state/info`, `/system/*`,
`/edit.html?file=/macros/<name>`, `/update.html` (OTA). Если включена авторизация —
`curl.exe -u user:pass http://<ip>/macros/list`.

## 6. Рабочий цикл

1. Внести правку в код компонента (помни: `module_*`/`device_*` — **отдельные git-репозитории**;
   `git status` в корне ядра их не покажет, смотри `git -C src/module_x status`).
2. `platformio run -e <env>` — убедиться, что компилируется.
3. `platformio run -e <env> -t upload --upload-port COM3` (firmware only).
4. Пронаблюдать: serial-монитор (логи/терминал) и/или HTTP-эндпоинт.
5. При необходимости повторить. После проверки **убрать временные отладочные хуки** (например
   `WIFI_GUARD_TEST` из методики в корневом `AGENTS.md`) и пересобрать релиз.

Автоматизированная живая проверка: L4 HTTP-тесты (`tests/core/*/http_api`, pytest) запускаются
только при заданном `DEVICE_HOST`; без него — auto-skip. См. `tests/core/SYSTEM.md`.

## 7. Подводные камни

- Забыл `PATH` → `[WinError 2]` в FS Builder, сборка FAILED (см. §0).
- `pip install`/`pipx` версия `pio` не та, что использует IDE. Проверяй
  `Get-Command platformio` после правки `PATH`.
- `upload` всё равно прогоняет pre-скрипты и соберёт FS-образ, но на устройство firmware-only
  загрузка ФС не переносит.
- Диагностика heap на устройстве: `/macros/heap` (бюджет/свободно/maxalloc) — полезно при OOM.
- Правка запущенных макрос-сценариев запрещена (`/macros/save` → `ERR: stop scenario first`) —
  сначала останови сценарий.
- Не коммить отладочные флаги/хуки и не удаляй их «забывчиво» — только временно, для проверки.
