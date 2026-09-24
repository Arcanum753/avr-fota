# Система тестирования ядра avr-fota

Обзор того, **что именно сделано** для автоматического тестирования ядра
(`core_*`, `common/`, EERTOS). Модули (`module_*`) и устройства (`device_*`) —
вне скоупа.

Связанные документы:
- [README.md](README.md) — как запускать и добавлять тесты.
- [INVENTORY.md](INVENTORY.md) — файл → зависимости → уровни.
- [REPORT.md](REPORT.md) — отчёт о работах и текущий статус.

## 1. Назначение и границы

- Покрываются только ядровые компоненты и общие утилиты.
- Никаких зависимостей от реального железа на уровнях L1–L3.
- Стиль/архитектура проекта сохраняются; правки `src/**` не требуются
  (тесты компилируют реальные `.cpp` ядра с host-моками).

## 2. Уровни тестирования

| Уровень | Что проверяет | Где | Условие запуска |
|---|---|---|---|
| **L1** | чистая логика, `env:native`, Unity | `tests/core/<module>/test/test_l1_*` | всегда |
| **L1-JS** | `ParseCVT`/`ApplyCVT` (`core_web/web/GetJson.js`) | `tests/core/core_web/js/`, `node --test` | есть `node` |
| **L2** | логика с моками: LittleFS, `core_json`, `core_state`, EERTOS, WiFi | `tests/core/<module>/test/test_l2_*` | всегда |
| **L3** | compile matrix ядра + AC-19 + AC-22 | `tests/compile_matrix/run_matrix.py` | из оркестратора |
| **L4** | HTTP API на реальной плате, pytest | `tests/core/<module>/http_api/` | задан `DEVICE_HOST` |

## 3. Структура

```
tests/
├── core/
│   ├── <module>/            # 11 модулей
│   │   ├── run.sh           # контракт: MODULE/ROOT/OUT
│   │   ├── platformio.ini   # native-env модуля
│   │   ├── test/            # L1/L2 тесты
│   │   └── http_api/        # L4 (если есть)
│   ├── native/mocks/        # централизованные host-моки Arduino/ESP
│   ├── conftest.py          # общие фикстуры pytest (L4)
│   ├── README.md
│   ├── INVENTORY.md
│   ├── REPORT.md
│   └── SYSTEM.md            # этот файл
├── compile_matrix/
│   ├── run_matrix.py        # L3
│   ├── envs.txt             # обязательные env
│   ├── size_limits.json     # пороги RAM/Flash
│   └── negative_nocore/     # AC-22: модуль без ядра должен падать
└── scripts/
    ├── run_core.sh          # оркестратор
    ├── run_module.sh        # общая логика модуля
    ├── gen_report.py        # сводный JUnit + HTML
    └── gen_coverage.py      # покрытие (gcovr) + пороги
```

## 4. Модули (11)

`common`, `core_web`, `core_sys`, `core_wifi`, `core_ntp`, `core_ota`,
`core_json`, `core_led`, `core_terminal`, `core_state`, `core_task`.

## 5. Запуск

```bash
# всё ядро: модули (L1/L2/L4) + L3, затем сводка
bash tests/scripts/run_core.sh

# только один модуль
bash tests/core/core_state/run.sh
bash tests/scripts/run_core.sh --only common

# без L3
bash tests/scripts/run_core.sh --no-l3

# L4 требует плату
DEVICE_HOST=http://<ip> bash tests/scripts/run_core.sh --with-device
```

## 6. Контракт модульного скрипта

`tests/core/<module>/run.sh` определяет `MODULE`, `ROOT`, `OUT` и вызывает
`tests/scripts/run_module.sh`, который:

- запускает все native-env из `platformio.ini` модуля;
- для `core_web` дополнительно гоняет Node.js-CVT;
- считает покрытие (`gcovr`) и проверяет порог для threshold-модулей;
- запускает L4-pytest, если есть `http_api/` и задан `DEVICE_HOST`.

Коды возврата: `0` — зелено, `1` — фейлы, `2` — конфигурационная ошибка
(нет `pio`, нет `[env:*]`, нечего запускать, `--with-device` без `DEVICE_HOST`,
для threshold-модулей нет данных покрытия).

Ложнозелёные сценарии исключены: отсутствие env/pio/данных покрытия даёт `2`.

## 7. Мок-окружение

`tests/core/native/mocks/` — библиотека host-моков:

- `Arduino.h`/`WString.h` — класс `String`, `Serial` (с инъекцией ввода),
  управляемые `millis()`/GPIO/`ESP.restart()`;
- `LittleFS.h` — in-memory ФС (`File`/`FS`);
- `WiFi.h`, `DNSServer.h`, `ESP.h`, `EEPROM.h`, `ArduinoOTA.h`, `Update.h`;
- `ESPAsyncWebServer.h` и `core_web/FSWebServerLib.h` — заглушки web-слоя;
- `version.h`, `avr/pgmspace.h`, `esp_*` — платформенные заглушки;
- `override_prelude.h` — задаёт include-guard'ы реальных заголовков ядра,
  поэтому вместо тяжёлых/несовместимых версий берутся мок-версии.

Ключевое отступление: тесты подключают нужные `src/**.cpp` **напрямую в свой TU**
(после `override_prelude.h`), а не через `test_build_src` — в PlatformIO 6.x
`src_dir` вне проекта не компилируется, а `src` в include-порядке идёт раньше
моков. Один native-env на модуль.

## 8. L3 — compile matrix

- `envs.txt`: сейчас обязательные `TestCore32`/`TestCore8266` («ядро без внешних
  компонентов»); полные `esp32`/`esp8266`/`esp32cam`/`esp32-c3-devkitm-1`
  исключены ради времени и оставлены закомментированными «на будущее».
- `run_matrix.py`: сборка env, парсинг RAM/Flash, сверка с `size_limits.json`
  (запас +5%), AC-19 (`modules_registry.cpp` без `#if` и без `module_*`/`device_*`),
  AC-22 (негативная сборка `negative_nocore/` должна падать). Пишет
  `l3_junit.xml`.

## 9. L4 — HTTP API

- `tests/core/conftest.py`: фикстуры `http_get`/`http_post`/`wait_online`,
  Basic-auth из `DEVICE_HOST`/`DEVICE_USER`/`DEVICE_PASS`, autouse-skip без
  `DEVICE_HOST`.
- Наборы: `core_web` (`/all`, `/secret.json`, 404), `core_sys`
  (`/system/*`, `/recover*`), `core_wifi` (`/wifi/*`, `/api/wifi/slot`, captive),
  `core_ntp`, `core_ota` (`/update/*`), `core_state` (`/state/*`).

## 10. Отчёты и покрытие

- `test_reports/core/<module>/<env>_junit.xml`, `*.log`, `coverage.md`.
- Сводка: `test_reports/core/_summary/{junit.xml, report.html}`
  (`tests/scripts/gen_report.py`).
- Покрытие: `tests/scripts/gen_coverage.py` (gcovr). Для `common`, `core_ota`,
  `core_led` порог ≥80% — жёсткий гейт (нет данных/ниже порога → exit 2).

## 11. CI

`.github/workflows/tests-core.yml`:
- `core-tests` — L1+L2+L3 на push/PR (`bash tests/scripts/run_core.sh`);
- `core-http` — L4 по `workflow_dispatch`/тегу на self-hosted runner с платой.

## 12. Текущий статус

- L1/L2 — **зелёные** (все 11 модулей, 119 тестов, WSL Ubuntu 24.04).
- L3 — **зелёный**: AC-19 (чистота `modules_registry.cpp`) и AC-22 (негативная
  сборка без ядра) проходят; сборки ESP `TestCore32`/`TestCore8266` помечаются
  `skip` без Linux-тулчейнов и валидируются в CI.
- L4 — auto-skip без `DEVICE_HOST`.
- `size_limits.json` — пуст (пороги RAM/Flash заполняются после сборки в CI).
- Детали и журнал работ — в [REPORT.md](REPORT.md).
