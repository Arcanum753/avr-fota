# AVR-FOTA: архитектура и система сборки

## Обзор

Универсальная прошивка для **ESP8266** и **ESP32** с веб-интерфейсом для программирования AVR (AtMega/AtTiny) и STM32 микроконтроллеров по локальной сети, а также для удалённого управления устройствами. Форк [FSBrowserNG](https://github.com/gmag11/FSBrowserNG) с логикой ISP-программирования из [Standalone-Arduino-AVR-ISP-programmer](https://github.com/adafruit/Standalone-Arduino-AVR-ISP-programmer/) и логикой SWD из [ESP32_nRF52_SWD](https://github.com/atc1441/ESP32_nRF52_SWD) / [blackmagic](https://codeberg.org/blackmagic-debug/blackmagic).

## Соглашения о стиле кода

- **Сохранять существующий стиль кода.** Не менять форматирование, отступы, расположение скобок или пробелы без необходимости.
- **Запрещён `#elif`.** Все условные блоки оформляются только через `#if` / `#endif`. Никаких `#elif`/ `#else`. 
- **Новые комментарии — на русском языке.** Существующие комментарии (на любом языке) сохранять как есть, если они остаются актуальными. Устаревшие комментарии можно удалять или обновлять, заменяя на русский.

## Архитектура

### Модульная структура

Проект разделён на **две категории**:

1. **Ядра (`core_*`)** — присутствуют всегда, обеспечивают базовую функциональность
2. **Модули (`module_*`)** — опциональны, добавляются через `src_filter` и `build_flags` в конфиге таргета
3. **Устройства (`device_*`)** — опциональны, добавляются через `src_filter` и `build_flags` в конфиге таргета. Используются для устройств со стабильным железом.

Субмодули (`submodule_*`) наследуются от `Class_ProgBase` и реализуют конкретных программаторов. Базовая логика программатора находится в `module_prog/`.

### Мульти-репозиторная компоновка

Ядро — единый git-репозиторий (этот файл). Компоненты (`module_*`, `device_*`, контейнер
программатора) — **отдельные git-репозитории**, которые клонируются пользователем в `src/<имя>`
(устройства и модули — по репозиторию на компонент). Исключения:

- `src/module_template/` остаётся в ядре — эталон для создания новых модулей.
- Программатор — один репозиторий-контейнер `module_program`, клонируется в `src/module_program/`
  и содержит вложенные компоненты `module_prog/`, `submodule_isp/`, `submodule_swd/` (выбор ISP/SWD —
  через env, как раньше). Тулинг и python-скрипты умеют работать с вложенными компонентами
  (`src/<dir...>/<name>`), генератор registry ищет `<name>.ini` по полному пути.

Сборка набора: клонировать нужные репозитории в `src/` → env из их `.ini` подхватывается
glob-масками `src/*/*.ini` и `src/*/*/*.ini` в `[platformio] extra_configs` → выбрать env и собрать.
Репозиторий ядра игнорирует внешние папки через `.gitignore` (`/src/module_*/`, `/src/device_*/`,
`!/src/module_template/`).

**Репозитории (под аккаунтом `Arcanum753`, ветка `main`):**

| Репозиторий | Папка в `src/` | Содержимое |
|-------------|----------------|------------|
| `avr-fota` | — (ядро) | этот репозиторий: core_*, common, core_sys, core_web, python/, targets/, `module_template` |
| `module_program` | `src/module_program/` | контейнер программатора: `module_prog/` + `submodule_isp/` + `submodule_swd/` |
| `module_udp` | `src/module_udp/` | UDP broadcast |
| `module_editor` | `src/module_editor/` | файловый редактор FS (форк Ace) |
| `module_ds3231` | `src/module_ds3231/` | часы реального времени DS3231 |
| `module_gpio` | `src/module_gpio/` | GPIO через web |
| `module_lcd-i2c` | `src/module_lcd-i2c/` | LCD I2C |
| `module_macros` | `src/module_macros/` | макросы/сценарии |
| `module_otaclient` | `src/module_otaclient/` | OTA-клиент |
| `module_rgb` | `src/module_rgb/` | RGB-матрица (WS2812) |
| `module_i2c-mapper` | `src/module_i2c-mapper/` | I2C-сканер |
| `device_clock-mech` | `src/device_clock-mech/` | часы механические |
| `device_mech-ring` | `src/device_mech-ring/` | часы механические с боем |
| `device_electronica7_rgb` | `src/device_electronica7_rgb/` | часы Электроника-7 RGB |

**Локальная разработка:** в рабочей копии ядра все 13 компонентов клонированы в `src/` (у каждого
своя `.git`, ветка `main`). Ядро их игнорирует. Коммиты/пуши выполняются отдельно в каждой папке;
правки «всё сразу» — как обычные изменения файлов в одном окне VS Code. `module_template` — часть
ядра (не клон).

### Ядровые модули (компилируются всегда)

| Модуль | Каталог | Назначение |
|--------|-----------|---------|
| `core_sys` | `src/core_sys/` | Ядро системы: идентичность (имя/серийник устройства, хранилище в NVRAM), `config_sys.json`, HTTP-аутентификация/восстановление (`secret.json`), информация о системе (причина сброса, chipinfo, about), централизованное чтение версии FS (`_version_fs.json`), EERTOS |
| `core_wifi` | `src/core_wifi/` | Wi-Fi клиент/AP, управление 4 профилями, сканирование |
| `core_ntp` | `src/core_ntp/` | NTP-клиент с 3 серверами (основной + 2 запасных) |
| `core_ota` | `src/core_ota/` | Самообновление (FOTA) через web, проверка версии FS |
| `core_json` | `src/core_json/` | Утилиты JSON (сохранение/загрузка/разбор) |
| `core_led` | `src/core_led/` | Макросы индикации светодиодом (WiFi, ошибки, успех, ожидание) |
| `core_terminal` | `src/core_terminal/` | Последовательный терминал с отладочными/управляющими командами |
| `core_state` | `src/core_state/` | Ресурсная шина: реестр ресурсов, события, async-вызовы, режимы ядра (`system.mode`) |
| `core_task` | `src/core_task/` | Именованные задачи поверх EERTOS (`every`/`after`/`cancel`) + диагностика переполнения |

Редактор FS ранее входил в ядро (`core_editor`); теперь это **опциональный** `module_editor`
(отдельный репозиторий, включается флагом `-D MODULE_EDITOR`, инициализируется в
`modules_begin()`/`modules_web_Init()` через registry). В `core_begin()` его больше нет.

### Опциональные модули

| Модуль | Флаг | Назначение |
|--------|------|---------|
| `module_program` (репо) | `-D PROGTYPE_ISP` | AVR-ISP программатор (AtMega/AtTiny): `src/module_program/module_prog` + `.../submodule_isp` |
| `module_program` (репо) | `-D PROGTYPE_SWD` | SWD программатор (STM32 F1/F4): `src/module_program/module_prog` + `.../submodule_swd` |
| `module_gpio` | `-D MODULE_GPIO` | Управление GPIO через web |
| `module_lcd-i2c` | `-D MODULE_LCD_I2C` | Управление LCD I2C дисплеем (LiquidCrystal_I2C, маски date/time, подсветка) |
| `module_ds3231` | `-D MODULE_DS3231` | Часы реального времени DS3231 |
| `module_macros` | `-D MODULE_MACROS` | Макросы/сценарии |
| `module_rgb` | `-D MODULE_RGB` | RGB-матрица (NeoPixelBus/WS2812) |
| `module_udp` | `-D MODULE_UDP` | UDP broadcast для обнаружения устройств |
| `module_otaclient` | `-D MODULE_OTACLIENT=1` | OTA-клиент (автообновление с удалённого сервера) |
| `module_template` | `-D MODULE_TEMPLATE` | Шаблон модуля — основа для создания новых модулей (в ядре) |
| `module_i2c-mapper` | `-D MODULE_I2C_MAPPER` | Сканер шины I2C (веб-интерфейс, Wire0) |
| `module_editor` | `-D MODULE_EDITOR` | Браузер FS, редактор файлов (html/txt/json/js), загрузка/удаление — форк Ace (опционально) |

### Устройства (device_*) — отдельные репозитории, клонируются в `src/device_*`

| Устройство | Env-примеры | Назначение |
|--------|-------------|------------|
| `device_clock-mech` | `esp32_clock-mech` | Механические часы |
| `device_mech-ring` | `esp32_clock-mech_ring` | Механические часы с боем |
| `device_electronica7_rgb` | `esp32_electronica7_rgb` | Часы «Электроника-7» RGB (матрица) |

Устройства описывают «рецепт» сборки: их `.ini` в `src_filter` перечисляют нужные модули
(например `device_mech-ring.ini` подтягивает `module_udp`, `module_ds3231`, `module_otaclient`).
Для сборки устройства должны быть склонированы и само устройство, и все указанные модули.

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
- Интеграция в `src/core_web/FSWebServerLib.cpp` под флагом `MODULE_XXX`
- Таргеты в `targets/targets_example.ini`

**Единый порядок функций в .cpp модуля (обязательно для всех core_/module_/device_/submodule_):**

Шаблонный блок — всегда в начале файла, конкретная логика — после него (либо в
`<module>_engine.cpp`, см. «Слоистая структура модуля» выше).

1. **INCLUDES** — `#include "core_web/FSWebServerLib.h"` первым, затем библиотечные заголовки, `core_*/...`, свой `module_xxx.h`, `common/common.h`, `*_version.h`, `core_sys/eertos.h`.
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

**Слоистая структура модуля (обязательно для всех core_/module_/device_/submodule_):**

Эталон — `module_macros` (`module_macros.h` + `module_macros.cpp` + `module_macros_engine.h/.cpp`).
Модуль раскладывается на слои, каждый файл имеет своё назначение:

- `<module>_types.h` — define'ы конфигов/лимитов и `struct`/`enum`/`typedef`, не привязанные к классу.
  Минимум include'ов. Include guard: `_<DIR>_TYPES_h` (дефисы → подчёркивания, имя каталога модуля).
- `<module>.h` — класс `CLASS_*` (не дробится: C++ не поддерживает partial class), debug-макрос,
  `extern` на глобальный объект, HTML-шаблоны `Page_*[]`. Включает `_types.h` (и `_led.h`/`_engine.h`).
- `<module>.cpp` — **шаблонный блок**: INCLUDES, определение глобального объекта, `setFs`,
  `begin`/`begin(ctx)`, `web_Init` + веб-обработчики, `register_resources`, конфиг,
  версионные методы, терминальные команды (регистрация). Порядок — как в разделе
  «Единый порядок функций» ниже.
- `<module>_engine.cpp` — **исполнительная логика**: state machine, алгоритмы, ISR-обработчики,
  колбэки внешних API, специфичные хелперы, реализация LED-макросов, `static`-хелперы,
  используемые только engine. Не переносить сюда методы-обёртки веб/терминала/конфига.
- `<module>_led.h` — объявления внешних LED-макросов (например `core_wifi_led.h`,
  `core_ota_led.h`). НЕ создавать для LED-функций, объявленных как статические методы класса.
- `<module>_engine.h` — только если engine предоставляет интерфейс наружу или содержит
  структуры, не влезающие в `_types.h` (например `module_udp_engine.h`).

Правила переноса:
- Логику не менять — только переносить код. Все `DEBUGXXX`-вызовы сохранять.
- Глобальные объекты модуля (`core_wifi`, `module_rgb`, …) остаются в `<module>.cpp`; прочие
  глобальные переменные, используемые только engine, переносятся в engine-файл (`extern` при
  необходимости).
- Функция, используемая и шаблоном, и engine, объявляется в `.h` (метод класса) или в
  `_engine.h` (свободная). `static`-хелпер, нужный обоим файлам, выносится в `common_module`
  или `_engine.h`, но не дублируется.
- Новые условные блоки — только `#if`/`#endif` (без `#elif`/`#else`).
- Файлы `.ini` и генератор registry менять не нужно: `src_filter` включает каталог целиком.

### EERTOS — кооперативный планировщик

`src/core_sys/eertos.h` + `src/core_sys/eertos.cpp` реализуют кооперативный диспетчер задач/таймеров (не RTOS):
- `SetTask(TPTR)` — поставить функцию в очередь на выполнение в главном цикле
- `SetTimerTask(TPTR, uint32_t)` — запланировать функцию через N миллисекунд
- `DelTimerTask(TPTR)` — удалить запланированный таймер
- `TaskManager()` — вызывается из `loop()`, извлекает и выполняет одну задачу за вызов
- `TimerService()` — вызывается из ISR Ticker на 1 мс, уменьшает счётчики таймеров и помещает истёкшие задачи в очередь

Главный цикл (`loop()` в `main.cpp`):
1. Сбрасывает watchdog
2. Вызывает `TaskManager()` — выполняет одну задачу из очереди
3. Снова сбрасывает watchdog
4. Вызывает `loop_user()` (пользовательский хук, по умолчанию пустой)
5. Вызывает `TerminalLoop()`
6. Вызывает `core_loop()`, `modules_loop()`, `dev_loop()` (задача `core_ota.loop()`/`module_otaclient.loop()` вызывается через `core_loop`)

### Порядок инициализации ядра (`setup()`)

1. `InitRTOS()` — инициализация очередей EERTOS
2. `LittleFS.begin()` — монтирование файловой системы
3. `ESPHTTPServer.begin(&LittleFS)` — запускает веб-сервер (`src/core_web/FSWebServerLib.cpp`):
   - Заполняет глобальный `ModContext` (fs, hostname, password)
   - `core_begin(ModContext)` — инициализация ядра (WiFi, NTP, JSON, OTA)
   - `modules_begin(ModContext)` — инициализация опциональных модулей
   - `dev_begin(ModContext)` — инициализация устройств
   - `serverInit()` — регистрация основных HTTP-маршрутов
   - `core_web_Init()`, `modules_web_Init()`, `dev_web_Init()` — регистрация веб-маршрутов
   - `MDNS.begin()` — mDNS
   - Инициализация конкретных модулей регистрируется через `modules_registry` (см. ниже)
4. `TerminalInit()` — последовательный терминал (вызывается через `core_begin`, см. ниже)
5. `ledInit()` — инициализация GPIO светодиода
6. `ledMacroTimerTask()` — запуск таймера LED-макросов
7. `_secondEERtos.attach_ms(1, TimerService)` — запуск тика 1 мс

### Иерархия ключевых классов

```
Class_ProgBase (module_program/module_prog/module_prog.h)
├── Class_SubIsp (module_program/submodule_isp/) — AVR-ISP
└── Class_SubSwd (module_program/submodule_swd/) — STM32 SWD

CLASS_CORE_OTA (core_ota/core_ota.h)
└── CLASS_MODULE_OTACLIENT (module_otaclient/) — расширенный OTA-клиент
```

Ядро `CLASS_CORE_SYS` (`core_sys/core_sys.h`) не входит в иерархию программатора: владеет
идентичностью/конфигом системы, HTTP-auth и информацией о системе; `AsyncFSWebServer`
оставляет лишь web-инфраструктуру и тонкие форвардеры к `core_sys`.

### Именование классов: паттерн `CLASS_<ПРИНАДЛЕЖНОСТЬ>_<ФУНКЦИЯ>`

Имена классов образуются по паттерну `CLASS_<ПРИНАДЛЕЖНОСТЬ>_<ФУНКЦИЯ>`, где
принадлежность — категория модуля:

- Ядра: `CLASS_CORE_*` (например `CLASS_CORE_WIFI`, `CLASS_CORE_OTA`)
- Модули: `CLASS_MODULE_*` (например `CLASS_MODULE_GPIO`, `CLASS_MODULE_UDPBROADCAST`,
  `CLASS_MODULE_I2C_MAPPER`, `CLASS_MODULE_I2C_LCD`)
- Устройства: `CLASS_DEVICE_*` (например `CLASS_DEVICE_CLOCKMECH`, `CLASS_DEVICE_RINGMECH`)

Составные названия функции пишутся через подчёркивание: `I2C_MAPPER`, `I2C_LCD`.

Исключения (не переименовывать):
- `Class_ProgBase` (module_program/module_prog) — база субмодулей.
- `Class_SubIsp` / `Class_SubSwd` (submodule_*) — особый случай.
- Библиотечные/инфраструктурные классы (`SerialTerminal`, `AsyncWebServer*` и т.п.).

Глобальные объекты модулей НЕ переименовываются — только типы.

### Вспомогательные функции: `common/` и локальные `common_module`

Вспомогательные (чистые, без состояния) функции выносятся из основной логики компонента:

- **Общие для нескольких компонентов** — в `src/common/common.h/.cpp` (глобальное пространство
  имён). В частности, экранирование строк:
  - `String escapeHtml(const String&)` — вставка в HTML + защита разделителей CVT
    (`& < > "`, `|`→`&#124;`, CR/LF→пробел);
  - `String escapeJson(const String&)` — кавычки, `\`, `\b \f \n \r \t`, `\uXXXX` для байтов `< 0x20`.
  Дублирующие `escHtml`/`escapeJsonStr`/`macroJsonEscape` запрещены — использовать только эти две.
- **Локальные для компонента** — в `common_module.h/.cpp` внутри папки компонента, обёрнутые
  в namespace **`ns_<dirname>`** (дефисы → подчёркивания): `ns_core_sys`, `ns_core_web`,
  `ns_core_led`, `ns_module_ds3231`, `ns_module_macros`, `ns_module_rgb`,
  `ns_device_electronica7_rgb`. Префикс `ns_` обязателен: namespace с «голым» именем компонента
  конфликтует с глобальным объектом (`CLASS_MODULE_DS3231 module_ds3231;`). Include из файлов
  компонента — локальный: `#include "common_module.h"`.

Правила переноса (что считается вспомогательной функцией):

- переносятся только **чистые stateless** свободные/статические функции: преобразования
  (BCD/hex), форматирование даты/времени/цвета, парсинг/сборка, математика/цветовые утилиты;
- **НЕ переносятся**: методы классов, обработчики прерываний (`IRAM_ATTR`), обёртки таймеров/задач
  EERTOS, функции регистрации терминальных команд, C-callback-мосты (Lua/`cron`), работа с
  аппаратурой и любые функции, читающие/пишущие глобальные переменные модуля;
- исключения: сторонние форкнутые библиотеки (`NTPClientLib.*`, `ErriezSerialTerminal.*`,
  `ESPAsyncWebServer.h`) и уже выделенные helper-файлы (`module_prog/format_bin.*`,
  `format_hex.*`) не трогаются; `common_module` не создаётся, если подходящих функций нет;
- include guard каждого `common_module.h` уникален: `_<DIR>_COMMON_MODULE_h`.
- хелпер, используемый только engine-файлом компонента, остаётся `static` в
  `<module>_engine.cpp` и в `common_module` не выносится.

Текущие локальные `common_module`:

| Компонент | Namespace | Содержимое |
|-----------|-----------|------------|
| `core_sys` | `ns_core_sys` | `isAdminPassValid`, `identCrcSkip` |
| `core_web` | `ns_core_web` | `getContentType` (перенесён из `FSWebServerLib.h`) |
| `core_led` | `ns_core_led` | `ledPatLen`, `ledPatAt` |
| `module_ds3231` | `ns_module_ds3231` | BCD/alarm-хелперы, `_formatAlarmTime/_formatAlarmStamp` (ESP32) |
| `module_macros` | `ns_module_macros` | `macroFileBaseName` |
| `module_rgb` | `ns_module_rgb` | `hexStringToUint32` |
| `device_electronica7_rgb` | `ns_device_electronica7_rgb` | `e7*` (HSV/Lerp/ГПСЧ/эффекты/сэмплер) |

### Механизм подключения: единый контракт модулей + автогенерация registry

Все ядра/модули/устройства приводятся к единому контракту:
- `begin(ModContext& ctx)` — инициализация периферии + загрузка конфигов. `ModContext`
  (вын `src/mod_context.h`) содержит: `fs` (тип по `#if ESP32`/`#if ESP8266`),
  `hostname`, `password`. Метод устанавливает `_fs = ctx.fs` и вызывает существующий `begin()`.
- `web_Init()` — регистрация веб-путей (единое имя; историческое `webInit` удалено).
- `loop()` — периодическая задача (опционально, включается флагом `loop = 1`).

`src/core_web/FSWebServerLib.cpp` и `src/main.cpp` НЕ содержат ручного вызова `setFs`/`begin`/`webInit`
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
- `module_otaclient` (`module_otaclient`) при активном `-D MODULE_OTACLIENT` включается в **core**-группах
  (begin/web/loop) вместе с базовым OTA, а не в modules-группах.
- `module_udp` (`module_udp`) — `begin()` вызывается из `core_wifi` при подключении, поэтому
  `begin` в registry не дублируется; регистрируется только `web_Init()`.
- `core_sys` — всегда компилируется; в `core_begin` идёт сразу после `core_json`
  (загружает identity/auth и заполняет `ctx.hostname`/`ctx.password` до `core_wifi` и mDNS);
  `web_Init()` регистрирует `/system/*` и `/recover*`.
- `core_terminal` — без класса; `TerminalInit()` вызывается в `core_begin`,
  `TerminalLoop()` — в `core_loop` (базовые команды регистрируются в begin, слоты
  модулей применяются лениво при первом вызове `TerminalLoop()`).
- `core_led` вне контракта — инициализируется вручную в `main.cpp` (`ledInit()`).

**ВАЖНОЕ ОГРАНИЧЕНИЕ:** `src/modules_registry.cpp` сгенерирован под ОДИН env и не содержит
`#if defined(...)`. Файл НЕ хранится в git (добавлен в `.gitignore`): pre-скрипт
`1_registry_pre_build.py` перегенерирует его при каждой сборке под выбранный env, поэтому
при смене env/набора модулей достаточно просто собрать заново. Ручной запуск
`python/module_registry_gen.py --env <env>` нужен только вне сборки.

Исходные файлы фильтруются через `src_filter` в `platformio.ini`:
```ini
src_filter = +<*> -<.git/> -<.vscode/> -<module_*/> -<submodule_*/> -<device_*/>
```
Модули добавляются по таргетам:
```ini
[env:esp32-swd]
extends = env:esp32
src_filter = ${platformio.src_filter} +<module_program/module_prog/> +<module_program/submodule_swd/> +<module_udp/>
build_flags = ${env.build_flags} -D MODULE_UDP=1 -D PROGTYPE_SWD=1 -D SWDPIN_CLK=21 -D SWDPIN_DATA=19
```
После изменения `src_filter`/`build_flags` в env — перезапустить `python/module_registry_gen.py --env <env>`.
`extra_configs` в `platformio.ini` использует glob-маски (`src/*/*.ini`, `src/*/*/*.ini`) — env
подхватываются автоматически из склонированных в `src/` компонентов без ручной регистрации.

### Ресурсная шина и реестр (`core_state`)

**Правило:** любое межмодульное взаимодействие — только через `core_state`. Прямые
`#include` соседних `module_*` запрещены (кроме включения своего `common_module` и ядровых
заголовков).

- `core_state` (`CLASS_CORE_STATE`, объект `core_state`) — реестр ресурсов:
  - **pull (состояния):** модуль читает значения по имени в своём `loop()`/задаче;
  - **push (события):** `emit` при изменении, подписка через `on`;
  - системный тик — 1 с через EERTOS; раздача очереди — в `core_state.loop()`.
- `core_task` (`CLASS_CORE_TASK`, объект `core_task`) — именованные периодические/отложенные
  задачи поверх EERTOS: `every(name, fn, period, fire_now)`, `after(name, fn, delay)`,
  `cancel(name)`. Второго планировщика нет; каждый слот использует свою статическую
  trampoline-функцию (EERTOS `SetTimerTask` идемпотентен по указателю).

**Имена ресурсов:** `namespace.field` (один сегмент namespace, lowercase + `_`).
Namespace задаётся в `[registry]` ini компонента (`namespace = otaclient`) — генератор
выставляет его через `core_state.setNamespace()` перед вызовом `register_resources()`.
`regState(name, ...)` авто-префиксует namespace; `regStateAs(full, ...)` — полное имя вручную.

**Контракт модуля:** третий фронтенд наряду с `web_Init` и `TerminalInit` — метод
`register_resources()`. Поля `[registry]`: `object`, `define`, `web`, `loop`, `namespace`,
`res = 1` (есть `register_resources()`), `prio = 0..100` (больше = важнее, дефолт `50`),
`priv = 1` (привилегированный namespace — ядро/macros). Генератор формирует
`core_register_resources()`, `modules_register_resources()`, `dev_register_resources()`;
регистрация выполняется до `begin()` соответствующей группы, порядок — по `prio` (при равенстве
FCFS).

**Типы:** `BOOL / I32 / F32 / STR / TIME / ENUM`. F32 в UI/JSON — 3 знака после запятой.
ENUM хранит индекс, значения задаются `regEnum`.

**Коды возврата:** `0` — успех, `>0` — коды модуля (`regFuncCode`), `<0` — ошибки ядра
(`-1` NOT_REGISTERED, `-2` NOT_FOUND, `-3` BAD_TYPE, `-4` BAD_ARGC, `-5` BAD_VALUE,
`-6` READONLY, `-7` DISABLED, `-8` BUSY, `-9` NOT_READY, `-10` TIMEOUT, `-11` INTERNAL,
`-12` NOT_SUPPORTED, `-13` DENIED).

**Async:** модуль регистрирует `regFuncAsync` (`async = 1`, таймаут дефолт 10 с); модули
завершают вызов через `core_state.asyncComplete(handle, rc, value)`. `call()` для async и
`call_async()` для sync — `ERR_BAD_TYPE`. До 3 параллельных на владельца
(`setAsyncOwner`/`asyncCancelFor`). Callback получает rc первым аргументом.

**Права:** `regState` фиксирует owner-namespace. Запись в чужой ресурс не запрещается, но при
активном caller-контексте и `DEBUG_STATE` логируется warning. Ядро и `macros` —
привилегированные.

**Режимы модуля:** `off / auto / macro`. `idle` — не режим, а флаг ядра (`system.idle`).
Режим хранится в `config_xxx.json` (`"mode"`), модуль сам читает/пишет. Модуль публикует
`regState("mode", ENUM, ...)` и sync-функцию `mode`; `core_state.mode(ns, v)` читает/пишет
(`v < 0` — чтение). Переключение режима не сбрасывает внутреннюю логику модуля. Без
`module_macros` режим `macro` невозможен — модуль молча работает как `auto`.

**Режимы ядра (`system.mode`):** `init / normal / ota / fs_update / prog / test`.
`system.safe` — булев флаг, параллелен любому режиму. Блокировка — через режим, не mutex.
Ручной `test` имеет таймаут (дефолт 30 мин, `/config_state.json` → `test_timeout_s`),
длительные операции — абсолютный таймаут (`op_timeout_s`). Пока идёт длительная операция,
вход в `test` — `ERR_BUSY`; пока пользователь в `test` — автономные `ota`/`fs_update` ждут.
При конфликте длительных операций разрешает `prio` (при равенстве FCFS). Модуль узнаёт о смене
режима через `getMode()`/`requestMode()` или событие `system.mode_changed`.

**Веб:** `core_state.web_Init()` регистрирует `/state/catalog`, `/state/info`, `/state/set`,
`/state/call`, `/state/ver` (+ страница `state.html`). Каталог отдаётся `catalogToJson()` и
используется деревом ресурсов в `module_macros` (`/macros/resources`).

### Применение vs сохранение конфига (правило для модулей и устройств)

**Правило (в дальнейшем — обязательно для всех `core_*`/`module_*`/`device_*`/`submodule_*`):**

- Публичные сеттеры/функции компонента **применяют** изменения в памяти и на экране, но
  **не сохраняют** конфиг на FS. Макрос/шина не должны перезаписывать пользовательский конфиг.
- Сохранение выполняет только пользовательский путь: веб-обработчик (`handleSave`), терминал,
  а также **явная bus-функция `save`**.
- Компонент с resource-bus (`res = 1`) обязан регистрировать функцию сохранения с полным именем
  `<namespace>.save` (`core_state.regFunc("save", "->", "...", <bridge>, nullptr)`), которая
  ставит отложенное сохранение (`_pendingSave = true; SetTask(deferredApplyTask);`).
- Сигналы `core_state.signal(...)` в сеттерах сохранять — другие модули/страницы должны видеть
  текущее in-memory значение.

**Статус:** правило внедрено в `device_electronica7_rgb` (public-сеттеры без persist +
`saveNow()` + bus-функция `e7.save`). Остальные компоненты (`module_rgb`, `module_lcd-i2c`,
`module_ds3231`, `device_clock-mech`, `device_mech-ring`) пока не имеют resource-bus и
персистить конфиг из макроса не могут. **Масштабирование разделения `apply`/`save` на все
модули — отдельная будущая задача.**

### module_macros: один «барабан» и декларативные правила

`module_macros` держит **один** Lua-интерпретатор (`_lua`) на весь модуль. При старте файл
читается один раз: `registerFile()` разбирает `rules` в массив `MacroFile.rules`
(heap, по факту числа правил) и закрывает файл. При срабатывании правило либо делает
декларативные bus-вызовы (`call`/`calls` + `args`), либо перечитывает файл и вызывает
named-handler (`run="..."`) со свежим `_ENV`. Отдельных `lua_State` на файл больше нет —
иначе heap исчерпывался и `AsyncWebServer` падал (`operator new` → `terminate` → `abort`).

Формат файла:
```lua
return {
  desc = "...",
  handlers = { foo = function(ev) ... end },
  rules = {
    { cron = "...", call = "ns.func", args = { 1 } },
    { cron = "...", calls = { {"ns.f", {1}}, {"ns.g", {2}} } },
    { cond = { res = "ns.x", op = "==", val = true }, run = "foo" },
    { term = "spec", run = "foo" }, { button = "name", run = "foo" },
    { on = "ns.event", run = "foo" }, { run = "foo" },
  },
}
```
- `cond` — декларативно (`res`+`op`+`val`, массив = AND), фронт `false→true`.
- Мета-cron (`meta_cron` в таблице сценария, в `config_macros.json` не хранится) — **гейт окна** для всех правил файла.
  Если в файле единственное cron-выражение — его выносят в мета-cron, правило становится телом.
- `on` — декларативная подписка (движок сам `core_state.on`); `call_async` из Lua запрещён.
- Handler получает таблицу `event` (`type`, `spec`, `args`, `value`); глобалы между вызовами
  не живут (stateless).
- `handleList`/`handleResources` защищены от OOM: heap-guard и кэш каталога ресурсов.

### Структура веб-страниц

`src/core_web/web/page_head.html` содержит статическое меню и маркер `<!-- MODULES_RIGHT_COLUMN -->`. Скрипт сборки `gen_page_head.py` заменяет этот маркер ссылками, сгенерированными из каталога `web/` каждого модуля:
- Если у модуля есть `web/_menu.html`, его содержимое используется напрямую
- Иначе файлы `.html` сканируются на наличие `<title>` или первого заголовка

Веб-файлы модулей (например `module_program/module_prog/web/prog.html`, `module_program/submodule_isp/web/avrcfg.html`) копируются в каталог сборки FS скриптом `4_fs_builder.py`.

### Файлы конфигурации файловой системы (в `core_sys/web/`, `core_web/web/` и каталогах `web/` модулей)

| Файл | Расположение | Назначение |
|------|----------|---------|
| `config_sys.json` | `core_sys/web/` | Имя устройства, серийный номер |
| `config_ntp.json` | `core_ntp/web/` | Адреса NTP-серверов, часовой пояс, переход на летнее время |
| `config_wifi0-3.json` | `core_wifi/web/` | 4 профиля Wi-Fi (SSID, пароль, DHCP/статический IP) |
| `secret.json` | `core_sys/web/` | Логин/пароль HTTP-аутентификации (скрыт из браузера FS) |
| `page_head.html` | `core_web/web/` | HTML-шаблон главной страницы устройства (с маркером меню) |
| `config_prog.json` | `module_program/module_prog/web/` | Конфиг проекта программатора (чип, имя проекта) |
| `config_udp.json` | `module_udp/web/` | Конфиг UDP-модуля |
| `config_otaclient.json` | `module_otaclient/web/` | Конфиг OTA-клиента |
| `avrisp_cfg.json` | `module_program/submodule_isp/web/` | База чипов AVR (signature, размер flash, размер страницы) |
| `swd_cfg.json` | `module_program/submodule_swd/web/` | База чипов SWD (IDCODE, параметры flash) |

### Последовательный терминал

`core_terminal` оборачивает библиотеку [ErriezSerialTerminal](https://github.com/Erriez/ErriezSerialTerminal). Зарегистрированные команды (через `TerminalInit()`):
- `help` — список всех команд
- `reset` — перезагрузка ESP
- `echo` — вкл/выкл эхо терминала
- `?` — информация о системе и текущем состоянии Wi-Fi
- `id` — показать/установить имя и серийник устройства
- `1` — тест терминала
- `udpp` / `udpc` / `udps` — отладка UDP-модуля (только при `MODULE_UDP`)
- `led` — тест светодиода

Команды отладки программатора (`flash`, `flash2`, `stm32`, `swdf`, `avr`) в `TerminalInit()` закомментированы и не регистрируются. Команды `ds-*` (DS3231), `i2c-scan` (I2C-сканер), `macro` (макросы) и `c-*`/`r-*` (механические часы) регистрируются соответствующими модулями и устройствами.

### Обработчики форматов (`module_program/module_prog/`)

- `format_bin.h` — API чтения BIN-файлов (open/read/close/isFormat)
- `format_hex.h` — парсер Intel HEX с потоковой валидацией, запись во flash через callback (`hexFileParseStreamWrite`), оценка размера бинарника (`hexFileGetBinarySize`)

## Система сборки

### Платформа — PlatformIO

- Конфиг: `platformio.ini`
- Базовые платформы: `espressif8266`, `espressif32`
- Фреймворк: `arduino`
- Файловая система: LittleFS
- Таблица разделов ESP32: `partitions_esp32.csv` (2 слота OTA + spiffs)

### Предустановленные окружения

| env | Платформа | Плата | Примечания |
|-----|----------|-------|-------|
| `esp8266` | espressif8266 | d1_mini | База для таргетов ESP8266 |
| `esp32` | espressif32 | upesy_wroom | База для таргетов ESP32 |
| `esp32cam` | espressif32 | esp32cam | Расширяет esp32 |

Конфиги конкретных таргетов: `targets/targets_example.ini` (примеры) и `targets/targets_user.ini` (пользовательские).

### Скрипты сборки (`python/`)

Порядок запуска и назначение:

**Pre-скрипты** (перед компиляцией, порядок = номер файла):
1. `1_registry_pre_build.py` — запускает `module_registry_gen.py` под текущий env (см. «Механизм подключения: единый контракт модулей + автогенерация registry»)
2. `2_version_builder.py` — генерирует `src/version.h` с `MAJOR.MINOR.DATE.BUILD`, git-информацией, автоинкрементом MINOR при смене коммита, автоинкрементом BUILD при каждой сборке. Защищён от двойного запуска через переменную окружения.
3. `3_module_version_gen.py` — генерирует `*_version.h` для каждого ядра/модуля (определение изменений по git-хешу)
4. `4_fs_builder.py` — готовит каталог сборки FS в `web_debug/web_<env>/`, копирует файлы из `data/` и каталогов `web/` модулей, генерирует `_version_fs.json`, вызывает `gen_page_head.py`, перенаправляет `PLATFORMIO_FS_DATA_DIR`
5. `5_set_fs_data_dir.py` — устанавливает `PROJECT_DATA_DIR` PlatformIO на подготовленный каталог

**Ручной запуск (перед компиляцией, при смене env/набора модулей):**
- `module_registry_gen.py` — генерирует `src/modules_registry.cpp/.h` под выбранный env (обязательно, см. «Механизм подключения: единый контракт модулей + автогенерация registry»; также автоматически запускается `1_registry_pre_build.py` при каждой сборке)

**Пост-скрипты** (после компиляции):
6. `6_copy_fw.py` — копирует `firmware.bin` → `proj_fwbins/{ENV}-FIRMWARE-{VERSION}.bin`
7. `7_copy_fs.py` — копирует `littlefs.bin` → `proj_fwbins/{ENV}-FILESYS-{VERSION}.bin`

### Формат версии

`MAJOR.MINOR.DATE.BUILD` (например, `0.034.20260614_2351.0801`)

- `MAJOR` (1 цифра) — задаётся вручную в `version_counter.txt`
- `MINOR` (3 цифры) — задаётся вручную, автоинкремент при смене git-коммита
- `DATE` — `%Y%m%d_%H%M`
- `BUILD` (4 цифры) — автоинкремент при каждой компиляции

Версии модулей: независимая числовая версия на модуль, инкрементируется при изменении хеша содержимого файлов модуля. Номер и хеш хранятся в `<module>_version.h`; хеш коммита проекта — в `.version_hashes`.

### CI/CD

Планируется: GitHub Actions workflow (например `.github/workflows/platformio_ci.yml`), который
для заданного набора компонентов (ядро + модули/устройства по списку) клонирует их, собирает env
и публикует артефакты. В ядре `.github/` пока отсутствует; у компонентов CI может быть свой.

### Файлы версий

Всё ниже — генерируемые файлы, **не хранятся в git** (правила `.gitignore` ядра):

- `version_counter.txt` — MAJOR (строка 1), MINOR (строка 2)
- `build_counter.txt` — номер BUILD
- `.version_hashes` — git-хеши (в т.ч. хеш коммита проекта), отслеживается `2_version_builder.py`
- `src/version.h` — автогенерируемый C-заголовок со всеми макросами версий + git-информацией

Заголовки версий модулей `src/*_version.h` (например, `core_wifi_version.h`) в git **отслеживаются**: в них `3_module_version_gen.py` хранит номер версии и хеш последнего обработанного коммита.

### Файл версии файловой системы

`_version_fs.json` генерируется `4_fs_builder.py` и помещается в корень FS. Содержит:
- Полная версия прошивки + компоненты
- Git-информация (ветка, коммит, флаг dirty, тег)
- Версии и даты по модулям
- Статистика FS (размер, занято, свободно, число файлов, дата сборки)

## Структура каталогов

```
avr-fota/
├── data/                    # Пустая (заглушка для сборщика FS; веб-файлы в src/core_* /web)
├── python/                  # Скрипты сборки
│   ├── 1_registry_pre_build.py # Хук регенерации registry (запускает module_registry_gen.py)
│   ├── 2_version_builder.py  # Генерация заголовка версии
│   ├── 3_module_version_gen.py # Генерация версий по модулям
│   ├── 4_fs_builder.py       # Подготовка образа FS
│   ├── 5_set_fs_data_dir.py  # Перенаправление каталога данных FS
│   ├── 6_copy_fw.py          # Копирование бинарника прошивки
│   ├── 7_copy_fs.py          # Копирование бинарника FS
│   ├── module_registry_gen.py # Автогенерация registry под выбранный env (помощник для 1/build_all)
│   ├── gen_page_head.py      # Динамическая генерация заголовка страницы (помощник для 4)
│   └── build_all.py          # Сборка всех env (ручной оркестратор)
├── src/                     # Исходный код
│   ├── common/              # Низкоуровневые утилиты (время + строки + общее)
│   │   ├── common.h/cpp     # hex2bin, urldecode, formatBytes, checkRange, escapeHtml, escapeJson
│   │   ├── TimeLib.h/cpp    # Форк библиотеки времени
│   │   └── StringArray.h    # Утилита связного списка (форк)
│   ├── main.h/cpp            # Точка входа (setup/loop), общие для проекта define
│   ├── debug.h / debug_prefix.cpp # Отладочные макросы логирования + DBG_MOD
│   ├── mod_context.h         # Контекст инициализации модулей (fs, hostname, password)
│   ├── core_sys/             # Ядро системы: идентичность/аутентификация/конфиг + информация о системе (CLASS_CORE_SYS)
│   │   ├── core_sys.h/cpp    # CLASS_CORE_SYS: config_sys.json, secret.json, hostname, FS-version
│   │   ├── common_module.h/cpp # ns_core_sys: isAdminPassValid, identCrcSkip
│   │   ├── ident_store.h/cpp # Идентичность в NVRAM (имя/серийник)
│   │   ├── eertos.h/cpp      # Кооперативный планировщик задач
│   │   └── web/              # Системные страницы: system.html, recover.html, 404.html,
│   │                         #   config_sys.json, secret.json
│   ├── core_web/            # Ядро веб-сервера
│   │   ├── FSWebServerLib.h/cpp # Асинхронный веб-сервер + маршрутизация (AsyncFSWebServer)
│   │   ├── common_module.h/cpp # ns_core_web: getContentType
│   │   └── web/             # Главная страница устройства: index.html, GetJson.js, GetMarkup.js,
│   │                        #   style.css, page_head.html, page_bottom.html, esp.gif, logo.gif, favicon.ico
│   ├── ESPAsyncWebServer.h  # Форк библиотеки (в корне src, чтобы -Isrc перекрывал libdeps)
│   ├── modules_registry.h/cpp # Генерируется под env (не в git, пересоздаётся pre-скриптом сборки)
│   ├── version.h            # Автогенерируемый заголовок версии (не в git)
│   ├── core_wifi/           # Ядро Wi-Fi; слоистая структура:
│   │                        #   core_wifi.h + core_wifi_types.h + core_wifi_led.h
│   │                        #   + core_wifi.cpp (шаблон) + core_wifi_engine.cpp
│   │                        #   (+ web/wifi.html, wifi-slot.js, config_wifi0-3.json)
│   ├── core_ntp/            # Ядро NTP (+ web/ntp.html, config_ntp.json)
│   ├── core_ota/            # Ядро OTA (+ web/update.html, spark-md5.js)
│   ├── core_json/           # Ядро утилит JSON
│   ├── core_led/            # Ядро индикации светодиодом (common_module.h/cpp: ledPatLen, ledPatAt)
│   ├── core_terminal/       # Ядро последовательного терминала
│   ├── module_template/     # Шаблон модуля (эталон для создания новых; остаётся в ядре, не клон)
│   └── module_*/device_*/   # Клоны внешних репозиториев (в dev-копии лежат все 13), каждый со
│                           # своей .git; вне учёта ядра (см. .gitignore). Программатор:
│                           # src/module_program/{module_prog,submodule_isp,submodule_swd};
│                           # редактор FS: src/module_editor/.
│   └── *version.h           # Заголовки версий модулей (отслеживаются в git)
├── targets/                 # Конфиги таргетов PlatformIO
│   ├── targets_example.ini  # Примеры определений таргетов
│   └── targets_user.ini     # Пользовательские определения таргетов
├── web_debug/               # Подготовленный каталог сборки FS (генерируется)
├── proj_fwbins/             # Собранные бинарники прошивки + FS (генерируется)
├── .pio/                    # Артефакты сборки PlatformIO
├── partitions_esp32.csv     # Таблица разделов ESP32
├── platformio.ini           # Конфиг проекта PlatformIO
├── library.json             # Метаданные библиотеки
└── AGENTS.md                # Этот файл
```

### Отладочные макросы

У каждого модуля есть отдельный отладочный флаг и макрос:
- `DEBUG_SYS` → `DEBUGSYS(...)` (`[C_SYS]`)
- `DEBUG_OTA` → `DEBUGOTA(...)`
- `DEBUG_NTP` → `DEBUGNTP(...)`
- `DEBUG_JSON` → `DEBUGJSON(...)`
- `DEBUG_EDITOR` → `DEBUGEDITOR(...)` (`[M_EDITOR]`)
- `DEBUG_LED` → `DEBUGLOGLED(...)`
- `DEBUG_WIFI` → `DEBUG_WIFI(...)`
- `DEBUG_PROG` → `DEBUGLOGPROG(...)`
- `DEBUG_ISP` → `DEBUGLOGISP(...)`
- `DEBUG_SWD` → `DEBUGLOGSWD(...)`
- `DEBUG_UDP` → `DEBUGUDP(...)`
- `DEBUG_OTACLIENT` → `DEBUGOTACLIENT(...)`
- `DEBUG_I2C_MAPPER` → `DEBUGI2CMAPPER(...)`
- `RELEASE` определён → все отладочные макросы становятся пустышками

Все модульные макросы печатают префикс модуля `[C_]/[M_]/[D_]` (например `[C_WIFI]`,
`[M_UDP]`, `[D_CLOCKMECH]`) в начале каждой строки вывода через общий помощник
`DBG_MOD` (см. `src/debug.h` и `src/debug_prefix.cpp`). Префикс выводится только
в начале строки, поэтому паттерн `DEBUGXXX(__FUNCTION__); DEBUGXXX("\r\n");` даёт
один префикс на строку. `module_macros` и `core_terminal` префиксы не используют.

### Ключевые define

- `CONNECTION_LED` — GPIO для статусного светодиода (по умолчанию -1 = отключено)
- `AP_ENABLE_BUTTON` — GPIO для кнопки принудительного AP (по умолчанию -1 = отключено)
- `USE_LITTLEFS` — включить файловую систему LittleFS
- `HIDE_SECRET` — скрыть secret.json из браузера FS
- `PROGTYPE_ISP` / `PROGTYPE_SWD` — включить субмодули программатора
- `SWDPIN_CLK`, `SWDPIN_DATA` — назначение выводов SWD
- `PIN_MISO`, `PIN_MOSI`, `PIN_SCK`, `PIN_RST` — назначение выводов ISP

## Известные дефекты (требуют исправления)

Незакрытые места, зафиксированные по итогам сессии правок (проверка на
`esp32_electronica7_rgb_macros`, прошивка 0.082.20260912_2233.1530). Считать их
списком задач, а не описанием текущего устройства.

1. **`core_*.save` — «функция сохранения» не реализована для ядер.** По правилу
   «Применение vs сохранение конфига» компонент с resource-bus обязан иметь bus-функцию
   `save`. Сделано только для `device_electronica7_rgb` (`e7.save`). Остальные
   компоненты без resource-bus (`module_rgb`, `module_lcd-i2c`, `module_ds3231`,
   `device_clock-mech`, `device_mech-ring`) **не имеют** ни `regFunc`, ни функции
   сохранения — их веб-обработчики сохраняют напрямую, из макросов они недоступны.
   Нужно: при добавлении resource-bus любому компоненту сразу реализовывать
   `<namespace>.save` (см. `device_electronica7_rgb::saveNow()` как эталон).
   Масштабирование apply/save на существующие модули — отдельная задача.
2. **`handleSave` сохраняет весь конфиг целиком.** Если макрос изменил значение в
   памяти (без persist), а пользователь затем через веб поменял другое поле и нажал
   Save — макросное значение тоже уйдёт в конфиг. Нужно: сохранять только реально
   изменённые поля (сравнение с загруженным конфигом). Отмечено в коде
   `// TODO: уточнить — сохранять только изменённые поля?`.
3. **`core_wifi`: guard от цикла `esp_wifi_init` не проверен инъекцией отказа.** В
   `staTick()` при `WIFI_SCAN_FAILED` есть бэкофф (`WIFI_INIT_FAIL_PAUSE_SEC=20`) и
   контролируемый `ESP.restart()` после `WIFI_INIT_FAIL_MAX=5`, но успешность
   восстановления при реальном отказе драйвера не подтверждена. Дополнительно: при
   `system.mode` не `normal/init` рестарт откладывается бессрочно — устройство может
   остаться без сети. Нужно: продумать восстановление в этих режимах (безопасно для
   OTA/FS/prog) и проверить на живом сбое. Методика проверки — ниже.
4. **`module_macros`: слоты `ctx.file` после сдвига массива.** `_files` теперь в heap,
   слоты переиспользуются; при удалении/переименовании файлы сдвигаются копированием
   (`_files[j] = _files[j+1]`), а активные Lua-интерпретаторы держат указатель
   upvalue на прежний адрес `ctx`. Нужно пересматривать/пересоздавать интерпретаторы
   затронутых файлов после сдвига (или не сдвигать массив, а использовать стабильные
   id/указатели).
5. **UI `macros.html` не проверялся в браузере.** Интерактив (перетаскивание
   `.splitter`, независимость чекбоксов `SEL`/`CUR_EDIT`, `indeterminate` чекбокса
   «выделить всё», кнопки start/stop) собран, но вживую не тестировался. Нужно
   проверить в браузере и прогнать сценарии из `example/prompt4.txt`.
6. **`desc` у остановленного файла остаётся в таблице до перезагрузки.** `desc`
   разбирается только у `run`-файлов и не сбрасывается при остановке (в конфиг не
   пишется, после ребута пусто). Косметика; при желании сбрасывать `desc` в
   `setFileRun(off)`/`destroyScript`. Учтено в `macros_help.html`.
7. **Исправлено:** `config_macros.json` больше не должен получать `created:1`.
   Причина была в том, что файлы, найденные `reconcileList()` до синхронизации NTP,
   получали `now()≈1`; условие «переставить дату» проверяло `created == 0` и не
   срабатывало. Теперь `tickStep()` считает невалидной любую метку меньше
   `MACRO_CREATED_MIN` (2001-09-09) и после NTP проставляет реальную дату.
   Старые `created:1` в конфиге будут исправлены при первом тике после NTP и
   сохранены.

### Методика проверки `core_wifi` guard (`WIFI_SCAN_FAILED` → бэкофф → рестарт)

Цель: убедиться, что при «мёртвом» Wi-Fi-драйвере нет цикла `esp_wifi_init` раз в
секунду, а есть бэкофф `WIFI_INIT_FAIL_PAUSE_SEC` и контролируемый `ESP.restart()`,
который не срабатывает в небезопасных режимах. Проверка требует временного
тестового хука — «естественно» получить `WIFI_SCAN_FAILED` на живой плате нельзя
(потеря точки доступа даёт «0 сетей», а не отказ драйвера).

1. **Тестовый хук (только для отладки, в релиз не коммитить).** В `staTick()`
   (`core_wifi_engine.cpp`) сразу после `int st = WiFi.scanComplete();` добавить
   под флагом:
   ```cpp
   #if defined(WIFI_GUARD_TEST)
       st = WIFI_SCAN_FAILED;   // имитация отказа драйвера
   #endif
   ```
   Собрать/прошить с `-D WIFI_GUARD_TEST` (отдельный env или добавить флаг в
   `build_flags`), оставить serial-монитор на 115200.
2. **Ожидаемое поведение без режимной отсрочки.** После старта в режиме `normal`:
   - попытки/ошибки идут не чаще одного раза в ~20 с (`WIFI_INIT_FAIL_PAUSE_SEC`),
     а не раз в секунду;
   - на 5-й подряд неудаче — `[C_WIFI] WiFi init failed 5 times. Restarting.` и
     ребут (виден boot-лог, `Reset reason`);
   - после рестарта хук снова валит скан — проверяем, что это повторяется
     предсказуемо, без `Guru Meditation`/watchdog.
3. **Проверка отсрочки в длительных режимах.** До достижения 5 счётчика перевести
   систему в небезопасный режим:
   `GET /state/set?name=system.mode&value=4` (prog; также `3` = fs_update, `2` = ota).
   Убедиться, что в логе `restart deferred (mode 4)` и рестарта **нет**.
   Вернуть `GET /state/set?name=system.mode&value=1` (normal) — рестарт происходит.
4. **Проверка сброса счётчика.** При успешном скане/подключении счётчик
   сбрасывается: в логе есть `resetWifiFailCounters`, после `GotIP` — `_wifiInitFailCount = 0`.
   Проверить, что после временного успеха счётчик начинается с 1, а не продолжается.
5. **После проверки** удалить блок `WIFI_GUARD_TEST` и пересобрать релизную
   прошивку обоих env. Результат и оставшиеся вопросы (что делать в `test`/`prog`,
   нужен ли fallback в AP) дописать в этот раздел.

### Проверено и исправлено (для контекста)

- Lua OOM при массовом включении макросов: dirty-пересборка по файлам, не более
  `MACRO_PARSE_PER_TICK=2` за тик, лимит авто-повторов `MACRO_PARSE_MAX_RETRY=3`
  (`module_macros_engine.cpp`). Подтверждено на устройстве (heap 124880→72104 без
  `No memory for interpreter`).
- `device_electronica7_rgb::loadFont()` кэширует путь+размер файла — нет спама
  `fopen(...digital7.fnt) failed` при частых apply.
- Разделение apply/save в `device_electronica7_rgb` + bus-функция `e7.save`.
  Подтверждено перезагрузками: `set("e7.effect", ...)` не персистится,
  `call("e7.save")` и веб-`handleSave` персистят.
- Лимит файлов `MACRO_MAX_FILES` = 30; массив `_files` и правила перенесены в heap
  (static `.bss` не влезал в DRAM). Бюджет heap модуля `MACRO_HEAP_BUDGET` = 100 КБ,
  критический порог `MACRO_HEAP_CRIT_PCT` = 95% (кнопка «Валидация макросов» →
  `/macros/heap`). При достижении лимита `reconcileList()` пишет предупреждение в лог.
- Исправлен `created:1` у файлов, найденных до NTP: проверка `created <
  MACRO_CREATED_MIN` (вместо `== 0`) и простановка реальной даты после NTP.
- Устранён `abort()` при открытии `macros.html`: `handleResources`/`handleList`
  отдаются через `AsyncResponseStream` (без промежуточной `String` и второй копии
  в `send()`); добавлен guard `doc.overflowed()`. `MACRO_MAX_ENTS` снижен 24 → 8
  (освободило ~45 КБ heap; heap на старте ~183 КБ против ~137 КБ).
- JSON-дамп конфига (`[C_JSON]`) в `core_json_engine.cpp` отключён (флаг
  `JSON_DUMP_LOG`, по умолчанию выключен) — печать всего JSON на 115200 блокировала
  main-loop.
- Убрано автоматическое сохранение `config_macros.json` в `tickStep()` (простановка
  `created` после NTP теперь только в памяти). Сохранение конфига — только по
  действию пользователя (web/терминал) или явной bus-функции `save`.
- `core_web/web/GetMarkup.js` больше не перезаписывает `document.body.innerHTML`:
  фрагмент вставляется на месте тега `<markup>` (`replaceWith`). Раньше полная
  пересборка body после `window.onload` уничтожала все обработчики, навешанные
  скриптами страницы (кнопки таблицы, дерево Resources, сплиттер `macros.html`).
- `module_macros`: размер файла кэшируется в `MacroFile.size` (обновляется при
  старте, создании и записи); `/macros/list` не открывает файлы на каждый запрос
  (0.05 с вместо ~0.9 с).
