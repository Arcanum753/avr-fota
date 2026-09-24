# Юнит-тестирование ядра avr-fota

Система автоматического тестирования **только ядра** (`core_*`, `common/`, EERTOS).
Модули (`module_*`) и устройства (`device_*`) вне скоупа.

## Требования окружения

| Инструмент | Нужен для | Проверка |
|---|---|---|
| PlatformIO Core (`pio`) | L1/L2 (`env:native`), L3 (`pio run`) | `pio --version` |
| gcc/g++ (host) | L1/L2 native-сборка | `gcc --version` |
| node ≥ 18 | L1-JS CVT (`core_web`) | `node --version` |
| gcovr | coverage-гейт (`common`/`core_ota`/`core_led`) | `gcovr --version` |
| pytest | L4 (только с `DEVICE_HOST`) | `pytest --version` |
| python3 | `gen_coverage.py`/`gen_report.py`/`run_matrix.py` | `python3 --version` |

Замечания по установке:

- В WSL после `pipx install platformio gcovr pytest` исполняемые файлы попадают в
  `~/.local/bin`, которого нет в неинтерактивном PATH. Скрипты сами добавляют
  `~/.local/bin` и `~/.platformio/penv/bin` в начало `PATH` — отдельная настройка
  не нужна.
- `python` на Ubuntu не существует (есть только `python3`); скрипты резолвят
  интерпретатор автоматически.
- node ставится через `apt install nodejs npm`.

## Быстрый запуск

```bash
# всё ядро: модульные скрипты (L1/L2/L4) + общий compile matrix (L3)
bash tests/scripts/run_core.sh

# один модуль
bash tests/core/core_state/run.sh
bash tests/scripts/run_core.sh --only common

# только L1/L2 (без сборок плат)
bash tests/scripts/run_core.sh --no-l3
```

Отчёты: `test_reports/core/<module>/test_<module>_<level>_junit.xml`, `*.log`,
`coverage.md`; сводка — `test_reports/core/_summary/{report.html, junit.xml}`.

## Уровни

| Уровень | Что | Где запускается |
|---|---|---|
| L1 | host-юнит-тесты чистой логики (`env:native`, Unity, Node.js для CVT) | `tests/core/native/mocks` + `tests/core/<module>/test/test_l1_*` |
| L2 | host-тесты с моками (LittleFS, `core_json`, `core_state`, EERTOS) | `tests/core/<module>/test/test_l2_*` |
| L3 | compile matrix ядровых env (общий) | `tests/compile_matrix/run_matrix.py` (только из оркестратора) |
| L4 | HTTP API против реальной платы (pytest) | `tests/core/<module>/http_api/` (нужен `DEVICE_HOST`) |

L4 без `DEVICE_HOST` автоматически пропускается (exit 0). С флагом
`--with-device` оркестратор требует `DEVICE_HOST`, иначе exit 2.

## Добавление теста

1. L1/L2: создайте `tests/core/<module>/test/test_l1_<тема>.cpp` (или `test_l2_*`).
   Тесты используют Unity: `setUp`, `tearDown`, `main()` с `UNITY_BEGIN`/`RUN_TEST`.
2. В начале тест-файла подключите мок-прелюдию и нужные исходники ядра прямо в TU
   (это исключает `test_build_src` и конфликты include-порядка `src`/моков):

   ```cpp
   #include "override_prelude.h"
   #include "../../../../src/core_state/core_state_engine.cpp"
   #include "../../../../src/core_state/common_module.cpp"
   #include <unity.h>
   ```

   Один native-env на модуль; перечень env `run.sh` берёт из `platformio.ini`.
3. Хостовые заглушки Arduino/ESP лежат централизованно в
   `tests/core/native/mocks/`; библиотека подключается через `lib_extra_dirs`.
   `override_prelude.h` задаёт include-guard'ы реальных заголовков ядра
   (`ESPAsyncWebServer.h`, `FSWebServerLib.h`, `core_sys.h`, `core_ntp.h`,
   `version.h`), поэтому вместо них берутся мок-версии. Новые мок-заголовки —
   только туда.
4. L4: `tests/core/<module>/http_api/test_*.py`. Общие фикстуры (`http_get`,
   `http_post`, `wait_online`) — в `tests/core/conftest.py`.

## Мок-окружение

- `tests/core/native/mocks/` — `Arduino.h`/`WString.h` (класс `String`),
  `LittleFS.h`, `WiFi.h`, `DNSServer.h`, `ESP.h`, `ESPAsyncWebServer.h`,
  `core_web/FSWebServerLib.h`, `EEPROM.h`, `version.h`, `avr/pgmspace.h` и др.
- `millis()` управляется из тестов: `mockMillisSet`/`mockMillisAdvance`.
- GPIO наблюдается через `mockLastDigitalValue()`; `ESP.restart()` считает
  `mockEspRestartCount()`; Serial-ввод — `Serial.inject()`.

## Обновление `size_limits.json`

`tests/compile_matrix/size_limits.json` — пороги в процентах от партиции:

```json
{ "esp32": { "ram_percent": 75.0, "flash_percent": 90.0 } }
```

Пустой/отсутствующий env — проверяется только факт сборки. Порог задавайте
с запасом ~5% от наблюдаемого размера.

## L3 (compile matrix)

- `tests/compile_matrix/envs.txt`: обязательные env — `TestCore32`/`TestCore8266`
  (сборки «ядро без внешних компонентов»). Полные `esp32`/`esp8266`/`esp32cam`/
  `esp32-c3-devkitm-1` закомментированы «на будущее».
- AC-19: `src/modules_registry.cpp` (регенерируется под core-env) не содержит
  `#if` и вызовов `module_*`/`device_*`.
- AC-22: `negative_nocore/` (модуль без ядра) должен падать.
- Сборка ESP требует Linux-тулчейнов в `~/.platformio/packages/`. Без них
  (`toolchains_available()` == False) env помечаются `skip` (не `fail`) и
  валидируются в CI; AC-19/AC-22 тулчейнов не требуют и проверяются всегда.

## CI

`.github/workflows/tests-core.yml`:
- `core-tests` — L1+L2+L3 на push/PR (ровно один вызов оркестратора);
- `core-http` — L4 по `workflow_dispatch`/тегу на self-hosted runner с платой.
