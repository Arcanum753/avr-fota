# Инвентарь ядра avr-fota для юнит-тестирования

Таблица «файл → зависимости → применимые уровни». Составлена по коду
(проверено `#include`-графом).

| Компонент | Реальные TU для тестов | Зависимости | Уровни |
|---|---|---|---|
| `common` | `common/common.cpp` | `String`, `snprintf` (mock Arduino) | L1 |
| `core_web` | `core_web/common_module.cpp`; `core_web/web/GetJson.js` | Arduino String / JS DOM | L1 (C++), L1 (Node.js), L4 |
| `core_sys` | `core_sys/common_module.cpp` (L1); `eertos.cpp`, `ident_store.cpp` (L2) | Arduino; EEPROM/ESP-partition; `Arduino.h` (`noInterrupts`), `IRAM_ATTR` | L1 (`isAdminPassValid`, `identCrcSkip`), L2 (EERTOS, `ident_store`), L4 |
| `core_wifi` | `core_wifi_engine.cpp` | `WiFi`/`DNSServer`/`ESP` (mock), `core_sys`/`core_ntp` (stub), `core_led`, `core_state`, `core_web` | L2, L4 |
| `core_ntp` | `core_ntp.cpp`, `core_ntp_engine.cpp` | форк `NtpClientLib`, `WiFi`, `core_state` | L4 |
| `core_ota` | `core_ota_engine.cpp` | LittleFS (mock), `ArduinoOTA`/`Update` (mock), `version.h` (mock), `core_sys` (stub), `core_led` | L1, L2, L4 |
| `core_json` | `core_json.cpp`, `core_json_engine.cpp` | ArduinoJson, LittleFS (mock), `IPAddress` (mock) | L2 |
| `core_led` | `core_led_engine.cpp`, `core_led/common_module.cpp` | Arduino GPIO (mock), EERTOS | L1 |
| `core_terminal` | `core_terminal.cpp`, `ErriezSerialTerminal.cpp` | Arduino Serial (mock), command-функции (заглушки) | L2 |
| `core_state` | `core_state_engine.cpp`, `core_state/common_module.cpp`, `core_state.cpp`, `core_json_engine.cpp` | ArduinoJson, `core_json`, LittleFS (mock), ESPAsyncWebServer-stub | L2, L4 |
| `core_task` | `core_task.cpp`, `core_task_engine.cpp` | EERTOS | L2 |
| EERTOS | `core_sys/eertos.cpp` | `Arduino.h` (`noInterrupts`), `IRAM_ATTR`, `esp_intr_alloc.h` | L2 |

## Файлы вне скоупа тестирования

- Форк-библиотеки: `NtpClientLib.*`, `ErriezSerialTerminal.*` (компилируются в
  тестах core_ntp/core_terminal, но собственных тестов нет), `TimeLib.*`,
  `StringArray.h`.
- `src/ESPAsyncWebServer.h` — заменяется mock-версией в native-сборке.
- Генерируемые файлы: `src/version.h`, `src/modules_registry.*`, `*_version.h`
  (кроме `core_*_version.h`, которые отслеживаются и используются).

## Замечания

- `main.h` определяет `HIDE_SECRET` (включён) и `HIDE_CONFIG` (выключен) — L4-тесты
  учитывают это.
- Наличие env `TestCore32`/`TestCore8266` обеспечивается
  `targets/targets_example.ini` (подключён в `extra_configs`); это сборки
  «ядро без внешних компонентов» для AC-22.
- `ParseCVT`/`ApplyCVT` — JavaScript, поэтому L1 для core_web включает Node.js-тесты
  (`tests/core/core_web/js/`), см. §D8 плана.
