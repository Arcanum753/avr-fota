# Слоистая структура компонентов avr-fota

Документ фиксирует единую слоистую структуру компонентов (`core_*`, `module_*`,
`device_*`, `submodule_*`) и план приведения их к единому виду. Код при этом не меняется.

> Общая конвенция для всех компонентов. Модульные детали (что делает конкретный
> компонент, что в нём уже выделено и что предстоит выделить) — в `AGENTS.md` этого
> компонента. Ссылки — в сводной таблице ниже.

Эталон структуры — `module_macros`. Ожидаемый набор слоёв:

```
<module>_types.h   — define'ы конфигов/логики, struct/enum/typedef
<module>.h         — класс CLASS_*, debug-макрос, extern, HTML-шаблоны Page_*
<module>.cpp       — ШАБЛОН: begin, web_Init, веб-обработчики, конфиг, версия,
                     register_resources, терминальные команды
<module>_engine.cpp — ИСПОЛНИТЕЛЬНАЯ логика: state machine, алгоритмы, ISR,
                     колбэки, специфичные хелперы, LED-реализация
<module>_led.h     — объявления внешних LED-макросов (если есть)
<module>_engine.h  — интерфейс/структуры engine (только если нужны наружу)
```

Механический план работ накапливался без пушей; коммиты — по репозиториям компонентов
(см. `BUILD.md` про мульти-репозиторную компоновку).

## Краткие описания компонентов

### Ядра (`core_*`)

| Компонент | Что делает | Что в `.h`/`.cpp` сейчас | Что выделить |
|---|---|---|---|
| `core_wifi` | Wi-Fi STA/AP, 4 профиля, captive DNS, скан, автомат переподключения | `.h`: класс, `strWifiConfig`/`strApConfig`, `enWifiStatus`/`enWifiScan`, LED-макросы. `.cpp`: `begin`, веб-обработчики, конфиг, версия, автомат `secondTick`/`apTick`/`staTick`/`enterApWait`/`leaveApToScan`, WiFi-колбэки, LED-реализация, `buildNetworksJson` | types, engine, `_led.h` |
| `core_sys` | Идентичность (NVRAM), `config_sys.json`, HTTP-auth/recovery, инфо о системе, чтение `_version_fs.json` | `.h`: класс, `strSysConfig`/`strHTTPAuth`. `.cpp`: web, конфиг/identity, «Информация о системе», «FS-версия» | types, engine (инфо, FS-версия) |
| `core_ntp` | NTP-клиент (3 сервера) поверх форка `NtpClientLib` | `.h`: `strNtpConfig`, define'ы. `.cpp`: sync-логика, web, конфиг, версия | types, engine (sync) |
| `core_ota` | Самообновление FW/FS, сравнение версий | `.h`: `fileCompareResult`, `enum UpdateTypeFile`, `OTA_STR_*`, LED-макросы. `.cpp`: web-загрузка, сравнение версий, исполнение, LED | types, engine, `_led.h` |
| `core_json` | Обёртка над ArduinoJson (`jsonFile*`, `jsonParse*`, `jsonBuild*`) | Класс-утилита, шаблонной части почти нет | engine (вся логика, низкая польза) |
| `core_led` | Слоты приоритетов и паттерны LED | `enum LedPriority`, define'ы, почти весь `.cpp` — исполнительная логика | types, engine |
| `core_terminal` | Обёртка над `ErriezSerialTerminal` (форк) + команды | Тип регистрации модулей + реализации команд | types, engine (команды) |
| `core_state` | Ресурсная шина: реестр, события, async, режимы | `BusValue`/`BusResInfo`/`BusCb`, `enum CoreMode`, лимиты; крупная логика | types (публичные), engine |
| `core_task` | Именованные задачи поверх EERTOS | Лимиты, слоты, trampoline-функции | types, engine (слоты) |
| `core_web` | Web-инфраструктура (`AsyncFSWebServer`) + форвардеры к `core_sys` | Инфраструктурный класс | инфраструктура; деление даёт малый выигрыш |

### Модули, субмодули, устройства

Подробные таблицы «Что делает / Слои сейчас / Что выделить» перенесены в `AGENTS.md`
соответствующих компонентов (раздел «Слоистая структура»). Здесь — только ссылки.

| Компонент | Тип | AGENTS.md |
|---|---|---|
| `module_macros` (эталон) | модуль | `src/module_macros/AGENTS.md` |
| `module_editor` | модуль | `src/module_editor/AGENTS.md` |
| `module_gpio` | модуль | `src/module_gpio/AGENTS.md` |
| `module_ds3231` | модуль | `src/module_ds3231/AGENTS.md` |
| `module_rgb` | модуль | `src/module_rgb/AGENTS.md` |
| `module_udp` | модуль | `src/module_udp/AGENTS.md` |
| `module_otaclient` | модуль | `src/module_otaclient/AGENTS.md` |
| `module_lcd-i2c` | модуль | `src/module_lcd-i2c/AGENTS.md` |
| `module_i2c-mapper` | модуль | `src/module_i2c-mapper/AGENTS.md` |
| `module_template` | модуль (в ядре) | `src/module_template/AGENTS.md` |
| `module_program` | контейнер программатора | `src/module_program/AGENTS.md` |
| `device_clock-mech` | устройство | `src/device_clock-mech/AGENTS.md` |
| `device_mech-ring` | устройство | `src/device_mech-ring/AGENTS.md` |
| `device_electronica7_rgb` | устройство | `src/device_electronica7_rgb/AGENTS.md` |

## Сводная таблица

| Компонент | Types → `_types.h` | Engine → `_engine.cpp` | LED → `_led.h` | `_engine.h` | Приоритет | AGENTS.md |
|---|---|---|---|---|---|---|
| `core_wifi` | да | да | да | нет | высокий | — (TRS §3.1.3) |
| `core_sys` | да | да | нет | нет | высокий | — (TRS §3.1.2) |
| `core_ntp` | да | да | нет | нет | высокий | — (TRS §3.1.4) |
| `core_ota` | да | да | да | нет | высокий | — (TRS §3.1.5) |
| `core_state` | да | да | нет | нет | высокий | — (TRS §3.1.9) |
| `core_task` | да | да | нет | нет | высокий | — (TRS §3.1.10) |
| `core_led` | да | да | нет | нет | высокий | — (TRS §3.1.7) |
| `core_json` | нет | да (низкая польза) | нет | нет | низкий | — (TRS §3.1.6) |
| `core_terminal` | да | да | нет | нет | низкий | — (TRS §3.1.8) |
| `core_web` | частично | минимально (риск) | нет | нет | низкий | — (TRS §3.1.1) |
| `module_macros` | уже сделано | уже сделано | нет | уже есть | — | `src/module_macros/AGENTS.md` |
| `module_editor` | нет | нет | нет | нет | низкий | `src/module_editor/AGENTS.md` |
| `module_gpio` | нет | нет | нет | нет | низкий | `src/module_gpio/AGENTS.md` |
| `module_ds3231` | да | да | нет | нет | средний | `src/module_ds3231/AGENTS.md` |
| `module_rgb` | да | да | нет | нет | средний | `src/module_rgb/AGENTS.md` |
| `module_udp` | да | да | нет | да | средний | `src/module_udp/AGENTS.md` |
| `module_otaclient` | да | да | нет | нет | средний | `src/module_otaclient/AGENTS.md` |
| `module_lcd-i2c` | да | да | нет | нет | средний | `src/module_lcd-i2c/AGENTS.md` |
| `module_i2c-mapper` | да | да | нет | нет | низкий | `src/module_i2c-mapper/AGENTS.md` |
| `module_template` | да | да | нет | нет | средний | `src/module_template/AGENTS.md` |
| `device_clock-mech` | да | да | нет | нет | средний | `src/device_clock-mech/AGENTS.md` |
| `device_mech-ring` | да | да | нет | нет | средний | `src/device_mech-ring/AGENTS.md` |
| `device_electronica7_rgb` | да | да | нет | нет | средний | `src/device_electronica7_rgb/AGENTS.md` |
| `module_prog` | да | да | нет | нет | низкий | `src/module_program/AGENTS.md` |
| `submodule_isp` | да | частично | нет | нет | низкий | `src/module_program/AGENTS.md` |
| `submodule_swd` | да | частично | нет | нет | низкий | `src/module_program/AGENTS.md` |

Приоритет: `core_*` (высокий) → `module_*`/`device_*` (средний) → `submodule_*` (низкий).

## Примечания

- LED-функции, объявленные как статические методы класса (`device_*::ledMacrosXxxError`,
  `module_template::ledMacrosTemplateDemo`), отдельный `_led.h` не требуют.
- `_engine.h` создаётся только при необходимости (интерфейс наружу или структуры вне
  `_types.h`). Сейчас реально нужен только `module_udp`.
- Файлы `.ini` и генератор registry не меняются: `src_filter` включает каталог целиком.
- `module_macros` — эталон; при сверке границ перенести только EERTOS-обёртку тика в engine.
- Форк-библиотеки (`NtpClientLib.*`, `ErriezSerialTerminal.*`, `ESPAsyncWebServer.h`,
  `prog_isp.*`, `prog_swd.*`, `swd.*`, `stm32f*_flash.*`, `format_*.*`) не трогаются.

## Порядок дальнейших работ

1. По одному модулю в порядке приоритета: `_types.h` → `_led.h` → `_engine.cpp`
   (`_engine.h`) → правка `.h` → чистка `.cpp` → сборка → коммит в репозитории компонента.
2. Отдельный коммит: обновление `AGENTS.md` (подраздел «Слоистая структура модуля»,
   `Directory structure`, `Code style conventions`, порядок функций в `.cpp`, оговорка
   про `common_module`).
3. Финальные сборки `esp32_electronica7_rgb_macros` и `esp32_electronica7_rgb` + отчёт.

Пуши не выполнялись.

## Ссылки

- Технические требования ядра: `TRS.md`
- Сборка: `BUILD.md`
- Тестирование: `TESTING.md`
- Реестр компонентов: `INVENTORY.md`
