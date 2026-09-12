# Инвентаризация: слоистая структура модулей (Шаг 0)

Документ подготовлен без правок кода. Цель — зафиксировать текущее состояние
компонентов (`core_*`, `module_*`, `device_*`, `submodule_*`) и план приведения их
к единому слоистому виду:

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

Эталон — `module_macros`. Пуш не выполняется, коммиты накапливаются.

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

### Модули (`module_*`)

| Компонент | Что делает | Слои сейчас | Что выделить |
|---|---|---|---|
| `module_macros` | Lua-сценарии, cron, подписки шины | Уже разделён (`_engine.h/.cpp`) | сверить границы (trampoline EERTOS) |
| `module_editor` | Браузер/редактор ФС | Почти всё — веб-обработчики | нечего выносить |
| `module_gpio` | GPIO через web | Логики почти нет | нечего выносить |
| `module_ds3231` | RTC DS3231: время, будильники, SQW/GPIO, ISR | Типы + крупная логика в `.cpp` | types, engine |
| `module_rgb` | WS2812 (NeoPixelBus): эффекты, анимация | `strRgbConfig`, `NeoPixelBusType` | types, engine |
| `module_udp` | UDP broadcast/unicast, связь с сервером | define'ы, `strUdpConfig`, свободные функции | types, engine + `_engine.h` |
| `module_otaclient` | OTA-клиент: manifest, мини-HTTP | define'ы, `strOtaClientConfig`, `ManifestEntry` | types, engine |
| `module_lcd-i2c` | LCD I2C: вывод строк, backlight | `strLcdConfig`, пин-define'ы | types, engine |
| `module_i2c-mapper` | Сканер шины I2C | Пин-define'ы, `scanBus` | types, engine |
| `module_template` | Шаблон optional-модуля (эталон структуры) | demo-логика в `.cpp` | types, engine (показать паттерн) |

### Устройства (`device_*`)

| Компонент | Что делает | Слои сейчас | Что выделить |
|---|---|---|---|
| `device_clock-mech` | Механические часы: ШД, хоминг, установка стрелок | define'ы, enum'ы, `strClockMechConfig` + крупный автомат | types, engine |
| `device_mech-ring` | Часы с боем: хоминг, ротация, звонок | define'ы, enum'ы, `strRingMechConfig` + автомат | types, engine |
| `device_electronica7_rgb` | Матрица E7 RGB: рендер, эффекты, переходы, «дождь» | Есть `e7rgb_matrix.*`, `e7rgb_fonts.*`, `common_module.*` | types, engine |

### Программатор (`module_program/*`)

| Компонент | Что делает | Слои сейчас | Что выделить |
|---|---|---|---|
| `module_prog` (база) | Общие cfg/filelist/md5/миграция | Типы в `.h`, не-веб логика в `.cpp` | types, engine (cfg/filelist/md5) |
| `submodule_isp` | AVR-ISP | Аппаратная часть уже в `prog_isp.*` | types; engine частично |
| `submodule_swd` | STM32 SWD | Аппаратная часть в `swd.*`/`stm32f*_flash.*` | types; engine частично |

## Сводная таблица

| Компонент | Types → `_types.h` | Engine → `_engine.cpp` | LED → `_led.h` | `_engine.h` | Приоритет |
|---|---|---|---|---|---|
| `core_wifi` | да | да | да | нет | высокий |
| `core_sys` | да | да | нет | нет | высокий |
| `core_ntp` | да | да | нет | нет | высокий |
| `core_ota` | да | да | да | нет | высокий |
| `core_state` | да | да | нет | нет | высокий |
| `core_task` | да | да | нет | нет | высокий |
| `core_led` | да | да | нет | нет | высокий |
| `core_json` | нет | да (низкая польза) | нет | нет | низкий |
| `core_terminal` | да | да | нет | нет | низкий |
| `core_web` | частично | минимально (риск) | нет | нет | низкий |
| `module_macros` | уже сделано | уже сделано | нет | уже есть | — |
| `module_editor` | нет | нет | нет | нет | низкий |
| `module_gpio` | нет | нет | нет | нет | низкий |
| `module_ds3231` | да | да | нет | нет | средний |
| `module_rgb` | да | да | нет | нет | средний |
| `module_udp` | да | да | нет | да | средний |
| `module_otaclient` | да | да | нет | нет | средний |
| `module_lcd-i2c` | да | да | нет | нет | средний |
| `module_i2c-mapper` | да | да | нет | нет | низкий |
| `module_template` | да | да | нет | нет | средний |
| `device_clock-mech` | да | да | нет | нет | средний |
| `device_mech-ring` | да | да | нет | нет | средний |
| `device_electronica7_rgb` | да | да | нет | нет | средний |
| `module_prog` | да | да | нет | нет | низкий |
| `submodule_isp` | да | частично | нет | нет | низкий |
| `submodule_swd` | да | частично | нет | нет | низкий |

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
