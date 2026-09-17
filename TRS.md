# Technical Requirement Specification (TRS)

**Проект:** avr-fota — универсальная веб-прошивка для ESP8266/ESP32 с программатором AVR/STM32 и удалённым управлением устройствами
**Версия документа:** 1.0
**Статус:** черновик (составлен по состоянию репозитория на 2026-09-16)

---

## 1. Введение

### 1.1. Назначение документа

Документ описывает технические требования к программно-аппаратному комплексу **avr-fota**. Он фиксирует состав и границы системы; функциональные требования по каждому ядру (`core_*`), модулю (`module_*`, `submodule_*`) и устройству (`device_*`); нефункциональные требования; ограничения платформы и среды сборки; интерфейсы; требования к данным; критерии приёмки.

Документ предназначен разработчикам, тестировщикам и инженерам, собирающим прошивку под конкретное устройство.

### 1.2. Область применения (Scope)

Система представляет собой прошивку для микроконтроллеров **ESP8266** (базовая плата `d1_mini`) и **ESP32** (`upesy_wroom`, а также `esp32cam`, `esp32-c3-devkitm-1`), которая:

1. поднимает HTTP-сервер с файловой системой LittleFS и веб-интерфейсом;
2. программирует AVR (AtMega/AtTiny) по ISP или STM32 по SWD из браузера;
3. обновляет саму себя по сети (FOTA) — вручную и автоматически с удалённого сервера;
4. реализует набор опциональных модулей (UDP-обнаружение, редактор FS, RTC, LCD, RGB, макросценарии и т.д.);
5. реализует набор «устройств» — законченных рецептов сборки под конкретное железо (часы и т.п.).

**Границы:** система не является ОС и не поддерживает многозадачность реального времени; планировщик кооперативный (EERTOS). Облачные сервисы не обслуживаются, кроме опционального HTTP-клиента OTA.

### 1.3. Термины и сокращения

| Термин | Значение |
|--------|----------|
| **Ядро (`core_*`)** | Компонент, компилируемый в сборку всегда; базовая функциональность |
| **Модуль (`module_*`)** | Опциональный git-репозиторий, подключаемый через `src_filter`/`build_flags` |
| **Субмодуль (`submodule_*`)** | Компонент внутри контейнера `module_program` (ISP/SWD), наследует `Class_ProgBase` |
| **Устройство (`device_*`)** | Опциональный репозиторий-«рецепт сборки»; в `.ini` перечисляет нужные модули |
| **Registry** | Автогенерируемый `src/modules_registry.cpp/.h`, собирающий вызовы `*_begin`, `*_web_Init`, `*_loop`, `*_register_resources` под выбранный env |
| **EERTOS** | Кооперативный диспетчер задач/таймеров (`SetTask`, `SetTimerTask`, `TaskManager`, `TimerService`) |
| **Ресурсная шина** | `core_state` — реестр ресурсов (состояния/события/функции) с именами `namespace.field` |
| **ISP** | In-System Programming — внутрисхемное программирование AVR (STK500-совместимое) |
| **SWD** | Serial Wire Debug — интерфейс ARM (STM32) |
| **FOTA** | Firmware Over-The-Air — обновление прошивки/ФС по сети |
| **FS** | Файловая система устройства (LittleFS) |
| **NVRAM** | Энергонезависимая память идентичности устройства (имя/серийник) |
| **env** | Окружение PlatformIO (платформа, плата, флаги, фильтры исходников) |
| **CVT** | Формат ответа веб-API «ключ\|тип\|значение» (`ApplyCVT()`/`ParseCVT()`) |
| **app0/app1** | Два OTA-слота в таблице разделов ESP32 |

---

## 2. Обзор системы

### 2.1. Общая архитектура

Система построена по модульному принципу и разделена на три категории:

1. **Ядра (`core_*`)** — присутствуют в любой сборке.
2. **Модули (`module_*`, `submodule_*`)** — опциональны.
3. **Устройства (`device_*`)** — опциональны, законченный рецепт сборки.

Субмодули программатора наследуются от `Class_ProgBase`; базовая логика — в `module_prog/`.

**Критическое требование совместимости:** ни один `module_*`, `submodule_*` или `device_*` не является самостоятельным изделием и **не работоспособен без ядра**. Все они используют как минимум `core_web` (веб-сервер, `ESPHTTPServer`, `ModContext`), `core_sys` (идентичность, авторизация, версия ФС) и `core_json` (конфиги), а также — по необходимости — `core_led`, `core_terminal`, `core_state`, `core_task`, `core_ntp`, `core_ota`, `core_wifi`. Сборка модуля без ядра технически невозможна: базовый `src_filter` исключает `module_*/submodule_*/device_*`, а registry генерируется только вместе с ядровыми группами.

### 2.2. Мульти-репозиторная компоновка

Ядро — единый git-репозиторий. Компоненты (`module_*`, `device_*`, контейнер программатора) — отдельные репозитории под аккаунтом `Arcanum753` (ветка `main`), клонируемые в `src/<имя>`:

| Репозиторий | Папка в `src/` | Содержимое |
|-------------|----------------|------------|
| `avr-fota` | — (ядро) | `core_*`, `common`, `python/`, `targets/`, `module_template` |
| `module_program` | `src/module_program/` | контейнер: `module_prog/` + `submodule_isp/` + `submodule_swd/` |
| `module_udp` | `src/module_udp/` | UDP broadcast |
| `module_editor` | `src/module_editor/` | редактор FS (форк Ace) |
| `module_ds3231` | `src/module_ds3231/` | часы реального времени DS3231 |
| `module_gpio` | `src/module_gpio/` | GPIO через web |
| `module_lcd-i2c` | `src/module_lcd-i2c/` | LCD I2C |
| `module_macros` | `src/module_macros/` | макросы/сценарии (Lua) |
| `module_otaclient` | `src/module_otaclient/` | OTA-клиент |
| `module_rgb` | `src/module_rgb/` | RGB-лента/матрица (WS2812) |
| `module_i2c-mapper` | `src/module_i2c-mapper/` | сканер I2C |
| `device_clock-mech` | `src/device_clock-mech/` | часы механические |
| `device_mech-ring` | `src/device_mech-ring/` | часы механические с боем |
| `device_electronica7_rgb` | `src/device_electronica7_rgb/` | часы «Электроника-7» RGB |

Ядро игнорирует внешние папки через `.gitignore` (`/src/module_*/`, `/src/device_*/`, `!/src/module_template/`). `module_template` остаётся в ядре как эталон.

### 2.3. Порядок инициализации (`setup()`)

1. `Serial.begin(115200)`
2. `InitRTOS()` — инициализация очередей EERTOS
3. `LittleFS.begin()` — монтирование ФС
4. `ESPHTTPServer.begin(&LittleFS)` — запуск веб-сервера, заполнение `ModContext`, затем:
   - `core_begin(ctx)` — ядро, терминал, базовый или клиентский OTA;
   - `modules_begin(ctx)` — опциональные модули;
   - `dev_begin(ctx)` — устройства;
   - `serverInit()` — основные HTTP-маршруты;
   - `core_web_Init()` / `modules_web_Init()` / `dev_web_Init()` — маршруты компонентов;
   - `MDNS.begin()`
5. `ledInit()` — инициализация светодиода
6. `_secondEERtos.attach_ms(1, TimerService)` — тик планировщика 1 мс

### 2.4. Главный цикл (`loop()`)

1. Сброс watchdog
2. `TaskManager()` — выполнение одной задачи из очереди EERTOS
3. Сброс watchdog
4. `core_loop()` — `core_state.loop()`, `core_task.loop()`, `TerminalLoop()`, (`module_otaclient.loop()`)
5. `modules_loop()` — `loop()` модулей при `loop = 1`
6. `dev_loop()` — `loop()` устройств при `loop = 1`

### 2.5. Ресурсная шина и режимы (обзор)

- Любое межмодульное взаимодействие — **только через `core_state`**; прямые `#include` соседних `module_*` запрещены.
- Имена ресурсов: `namespace.field`; namespace задаётся в `[registry]`.
- Режимы ядра (`system.mode`): `init / normal / ota / fs_update / prog / test`; флаг `system.safe` параллелен любому режиму; длительные операции имеют абсолютный таймаут (`op_timeout_s`), ручной `test` — таймаут `test_timeout_s`.
- Режимы модулей: `off / auto / macro`; хранятся в `config_xxx.json` (`"mode"`); без `module_macros` режим `macro` невозможен.
- Правило «применение vs сохранение»: публичные сеттеры применяют изменения в памяти, но **не** сохраняют конфиг; сохранение выполняет только пользовательский путь (веб, терминал) либо явная bus-функция `<namespace>.save`.

### 2.6. Карта компонентов и связей

```
                     ┌──────────────────────────────────────┐
                     │        core_web (ESPHTTPServer)      │
                     │  LittleFS · маршрутизация · mDNS     │
                     └───────────────┬──────────────────────┘
                                     │ modules_registry.cpp (генерируется)
        ┌────────────────────────────┼────────────────────────────────┐
        │                            │                                │
 core_sys  core_json  core_state  core_task  core_wifi  core_ntp  core_ota  core_led  core_terminal
        │
        │  ресурсная шина (namespace.field, события, call/async)
        │
 module_udp  module_editor  module_ds3231  module_gpio  module_lcd-i2c
 module_macros  module_otaclient  module_rgb  module_i2c-mapper  module_template
        │
 device_clock-mech  device_mech-ring  device_electronica7_rgb
        │
 module_program ── module_prog (Class_ProgBase)
                  ├── submodule_isp (Class_SubIsp)  → PROGTYPE_ISP
                  └── submodule_swd (Class_SubSwd)  → PROGTYPE_SWD
```

---

## 3. Функциональные требования

Нумерация: `FR-<КОМПОНЕНТ>-<N>`. Для каждого компонента указаны назначение, требования, зависимости и интерфейсы. Для всех компонентов разделов 3.2 и 3.3 действует обязательное требование: **компонент работоспособен только в составе сборки, содержащей ядро** (см. 2.1).

---

### 3.1. Ядровые компоненты (`core_*`)

#### 3.1.1. `core_web` — ядро веб-сервера

**Каталог:** `src/core_web/` · **Класс:** `AsyncFSWebServer : public AsyncWebServer` · **Объект:** `ESPHTTPServer` · **.ini:** отсутствует (инфраструктура, вне registry).

**Назначение:** асинхронный веб-сервер, маршрутизация, раздача статики из LittleFS, делегирование авторизации `core_sys`, mDNS, сборка модулей, перезагрузка.

**Функциональные требования:**
- FR-CORE-WEB-1: Сервер слушает порт 80 и обслуживает HTTP-запросы асинхронно (форк `ESPAsyncWebServer`; заголовок `src/ESPAsyncWebServer.h` имеет приоритет над `libdeps`).
- FR-CORE-WEB-2: `begin(fs)` заполняет глобальный `ModContext` (`fs`, `hostname`, `password`) и выполняет полную инициализацию системы по порядку 2.3.
- FR-CORE-WEB-3: Статические файлы отдаются из LittleFS (`handleFileRead`), для `/` подставляется `index.html`.
- FR-CORE-WEB-4: `onNotFound` отдаёт статику, при отсутствии файла — `/404.html`.
- FR-CORE-WEB-5: `GET /secret.json` возвращает 403 при определённом `HIDE_SECRET`; `GET /config_sys.json` — 403 при `HIDE_CONFIG`.
- FR-CORE-WEB-6: `GET /all` отдаёт JSON с информацией о heap/аналоговых входах.
- FR-CORE-WEB-7: `AsyncEventSource /events` — SSE-канал для серверных событий.
- FR-CORE-WEB-8: `restart_esp()` корректно завершает ФС и перезагружает устройство.
- FR-CORE-WEB-9: `getResetReason()`, `serialShowAbout()`, `getFsVersionStr()` делегируют в `core_sys`.
- FR-CORE-WEB-10: `checkAuth()` применяется ко всем защищённым маршрутам модулей.

**Веб-файлы:** `index.html`, `page_head.html` (статическое меню + маркер `<!-- MODULES_RIGHT_COLUMN -->`), `page_bottom.html`, `GetJson.js` (`GetJson`, `GetText`, `ApplyJson`, `ParseCVT`, `ApplyCVT`), `GetMarkup.js`, `style.css`, `esp.gif`, `logo.gif`, `favicon.ico`.

**Зависимости:** `ESPAsyncWebServer-esphome`, `AsyncTCP-esphome` (ESP32) / `ESPAsyncTCP` (ESP8266), `ESPmDNS`, `Ticker`, LittleFS, `common/TimeLib.h`, `ns_core_web::getContentType`.

**Ограничения:** маркер `MODULES_RIGHT_COLUMN` заполняется скриптом `gen_page_head.py` из `web/_menu.html` модулей; ручная правка `page_head.html` не сохраняется.

---

#### 3.1.2. `core_sys` — идентичность, аутентификация, информация

**Каталог:** `src/core_sys/` · **Класс:** `CLASS_CORE_SYS` · **Объект:** `core_sys`
**`[registry]`:** `object=core_sys`, `define=CORE_SYS`, `namespace=system`, `web=1`, `loop=0`, `res=1`, `prio=85`.

**Назначение:** идентичность устройства (имя/серийник в NVRAM), `config_sys.json`, HTTP-аутентификация и восстановление доступа (`secret.json`), информация о системе (причина сброса, chipinfo, about), централизованное чтение версии ФС (`_version_fs.json`), источник hostname для mDNS.

**Функциональные требования:**
- FR-CORE-SYS-1: Хранить имя и серийный номер в NVRAM (`ident_store`) с CRC-контролем (`identCrcSkip`), лимит поля 63 байта.
- FR-CORE-SYS-2: Загружать/сохранять `config_sys.json` (`deviceName`, `deviceSerial`).
- FR-CORE-SYS-3: Загружать/сохранять `secret.json` (`auth`, `user`, `pass`, `secq`, `seca`), включая HTTP Basic-аутентификацию (`checkAuth`, `httpAuthEnabled`).
- FR-CORE-SYS-4: Поддерживать восстановление доступа без авторизации: `GET /recover`, `GET /recover/status`, `POST /recover/reset` (проверка секретного ответа).
- FR-CORE-SYS-5: Предоставлять информацию о системе: причина сброса, chipinfo, about (`serialShowAbout`), значения (`/system/infovalues`).
- FR-CORE-SYS-6: Централизованно читать и кэшировать `/_version_fs.json` (`getFsVersionStr`, `getFsVersion`, `invalidateFsVersionCache`).
- FR-CORE-SYS-7: Заполнять `ctx.hostname`/`ctx.password` до инициализации `core_wifi` и mDNS.
- FR-CORE-SYS-8: Публиковать ресурсы `system.safe` (BOOL), `system.idle` (BOOL), `system.hostname` (STR) — только чтение.

**Веб-маршруты:** `POST /system/restart`; `/system/wwwauth`; `/system/infovalues`; `/system/version`; `/system.html`; `/system/savewwwauth`; `GET /system/devconf`; публичные без авторизации: `GET /recover`, `GET /recover/status`, `POST /recover/reset`.
**Веб-файлы:** `system.html`, `recover.html`, `404.html`, `config_sys.json`, `secret.json`.
**Конфиги:** `/config_sys.json`, `/secret.json`; чтение `/_version_fs.json`.
**Зависимости:** `core_json`, LittleFS, `esp_task_wdt`/`esp_ota_*` (ESP32), `ns_core_sys`.

---

#### 3.1.3. `core_wifi` — Wi-Fi клиент/AP

**Каталог:** `src/core_wifi/` · **Класс:** `CLASS_CORE_WIFI` · **Объект:** `core_wifi`
**`[registry]`:** `object=core_wifi`, `define=CORE_WIFI`, `namespace=wifi`, `web=1`, `loop=0`, `res=1`, `prio=80`.

**Назначение:** конечный автомат Wi-Fi STA/AP, 4 профиля SSID + системный конфиг, сканирование сетей, captive-portal DNS, счётчики неудач по SSID, индикация Wi-Fi.

**Функциональные требования:**
- FR-CORE-WIFI-1: Поддерживать 4 профиля подключения (`config_wifi0..3.json`) с DHCP или статическим IP.
- FR-CORE-WIFI-2: Системный конфиг `config_wifi.json`: период сканирования (`scantime`, мин) и удержание AP (`aptime`, мин).
- FR-CORE-WIFI-3: Сканировать сети (`scanWifi`), отдавать результаты по `/scan` и `/wifi/scan`.
- FR-CORE-WIFI-4: Поднимать точку доступа, обслуживать captive portal (`/generate_204`, `/hotspot-detect.html`, `/ncsi.txt`) и DNS-сервер (`startDNSCaptive`).
- FR-CORE-WIFI-5: Принудительно включать AP по кнопке `AP_ENABLE_BUTTON` с таймаутом `AP_ENABLE_TIMEOUT=60`.
- FR-CORE-WIFI-6: Вести счётчики неудач по SSID (`MAX_WIFI_FAIL_COUNT=3`, `WIFI_RESCAN_PAUSE_SEC=20`, `WIFI_CONNECT_BUDGET_SEC=20`, `WIFI_SCAN_STUCK_SEC=60`).
- FR-CORE-WIFI-7: Обрабатывать отказ драйвера: бэкофф `WIFI_INIT_FAIL_PAUSE_SEC=20` и контролируемый `ESP.restart()` после `WIFI_INIT_FAIL_MAX=5` (с отсрочкой в небезопасных режимах).
- FR-CORE-WIFI-8: При подключении вызывать `module_udp.begin()` (если модуль включён) и инициировать синхронизацию NTP.
- FR-CORE-WIFI-9: Публиковать ресурсы `wifi.connected` (BOOL), `wifi.rssi` (I32), `wifi.ip` (STR) и события `wifi.just_connected`, `wifi.just_disconnected`.

**Веб-маршруты:** `/wifi.html`; `/wifi/info`; `GET|POST /api/wifi/slot/0..3`; `GET /scan`; `GET /wifi/scan`; `GET /generate_204`; `GET /hotspot-detect.html`; `GET /ncsi.txt`; `/wifi/ver`; `GET|POST /wifi/sysconf`.
**Веб-файлы:** `wifi.html`, `wifi-slot.js`, `config_wifi0.json` … `config_wifi3.json`.
**Конфиги:** `/config_wifi0..3.json` (`ssid`, `pass`, `dhcp`, `ip[4]`, `netmask[4]`, `gateway[4]`, `dns[4]`), `/config_wifi.json`.
**Зависимости:** Arduino `WiFi`, `DNSServer`, LittleFS, `core_json`, `core_sys`, `core_led`, `core_state`, EERTOS.

---

#### 3.1.4. `core_ntp` — клиент NTP

**Каталог:** `src/core_ntp/` · **Класс:** `CLASS_CORE_NTP` · **Объект:** `core_ntp`
**`[registry]`:** `object=core_ntp`, `define=CORE_NTP`, `namespace=time`, `web=1`, `loop=0`, `res=1`, `prio=70`.

**Назначение:** синхронизация времени по NTP с 3 серверами (основной + 2 запасных), периодическая синхронизация, часовой пояс/летнее время, публикация времени на шину.

**Функциональные требования:**
- FR-CORE-NTP-1: Хранить 3 сервера (`ntp0`, `ntp1`, `ntp2`) и переключаться на запасной (`ntpSwitchReserv`).
- FR-CORE-NTP-2: Синхронизироваться с периодичностью `NTPperiod` (мин), учитывать `timeZone` (десятые доли часа) и `daylight`.
- FR-CORE-NTP-3: Запускать синхронизацию по подключению Wi-Fi (`ntpOnConnected`), останавливать при потере соединения.
- FR-CORE-NTP-4: Публиковать ресурсы `time.now` (TIME), `time.valid` (BOOL), `time.source` (STR) и событие `time.synced`.

**Веб-маршруты:** `GET /ntp/info`; `GET /ntp/conf`; `POST /ntp.html` (сохранение); `/ntp/ver`.
**Веб-файлы:** `ntp.html`, `config_ntp.json`.
**Конфиги:** `/config_ntp.json` (`ntp0`, `ntp1`, `ntp2`, `NTPperiod`, `timeZone`, `daylight`).
**Зависимости:** форк `NtpClientLib`, `common/TimeLib.h`, `WiFiClient`, `core_state`, `core_json`.

---

#### 3.1.5. `core_ota` — самообновление (FOTA) через web

**Каталог:** `src/core_ota/` · **Класс:** `CLASS_CORE_OTA` · **Объект:** `core_ota`
**`[registry]`:** `object=core_ota`, `define=CORE_OTA`, `namespace=ota`, `web=1`, `loop=1`, `res=1`, `prio=75`.
**Особенность:** при определённом `MODULE_OTACLIENT` методы `begin`/`web_Init`/`loop` базового OTA не вызываются — их заменяет `module_otaclient`; ресурсы базового OTA регистрируются.

**Назначение:** самообновление по сети (FOTA) прошивки и файловой системы, проверка имени/версии файла, MD5-верификация в браузере, прогресс загрузки; обработка `ArduinoOTA`.

**Функциональные требования:**
- FR-CORE-OTA-1: Принимать файл через `POST /update` с MD5-верификацией (`/update/setmd5`, `spark-md5.js`).
- FR-CORE-OTA-2: Проверять имя файла (`isValidFilename`, `fileNameCheck`) по шаблонам `firmware.bin`/`littlefs.bin` с разделителями `-FIRMWARE-`/`-FILESYS-`.
- FR-CORE-OTA-3: Сравнивать версию загружаемого файла с текущей (`compareWithCurrentFsVersion`, `compareVersionDiffs`), предупреждать о даунгрейде.
- FR-CORE-OTA-4: Корректно завершать/перемонтировать ФС перед записью (`fsEnd`/`fsRemount`, `_ota_fsEndCalled`).
- FR-CORE-OTA-5: Отдавать прогресс (`/update/progress`) и состояние.
- FR-CORE-OTA-6: Обрабатывать `ArduinoOTA.handle()` в `loop()`.
- FR-CORE-OTA-7: Публиковать ресурсы `ota.state` (ENUM `idle/upload/verify/done/error`) и `ota.server_reachable` (BOOL).
- FR-CORE-OTA-8: Предоставлять расширяемые точки `registerCommonRoutes()`/`registerCustomRoutes()` и виртуальные версионные методы для наследника `module_otaclient`.

**Веб-маршруты:** `/update/setmd5`; `/update/firmwarefilecheck`; `/update/progress`; `GET /update`; `POST /update`; `/update/ver`.
**Веб-файлы:** `update.html`, `spark-md5.js`.
**Конфиги:** отсутствуют.
**Зависимости:** `Update.h`, `ArduinoOTA.h`, LittleFS, `core_sys`, `core_state`, `core_led`, `version.h` (`BUILD_ENV`).

---

#### 3.1.6. `core_json` — утилиты JSON

**Каталог:** `src/core_json/` · **Класс:** `CLASS_CORE_JSON` · **Объект:** `core_json` · **.ini:** отсутствует (статически в списке ядер).

**Назначение:** обёртка над ArduinoJson: чтение/запись ключей, вложенных значений и массивов, загрузка/сохранение документов, сериализация Wi-Fi слотов. Инициализируется первым, чтобы `_fs` был доступен остальным.

**Функциональные требования:**
- FR-CORE-JSON-1: Чтение из файла: `jsonFileReadStr/Int/Uint/Bool`, `jsonParseStr/Int/Bool`, `jsonParseNestedStr/Int/Int64/Bool`, `jsonGetArraySize/Str/Int`.
- FR-CORE-JSON-2: Запись: `jsonFileWriteStr/Int/Bool`; сборка: `jsonBuildObj`, `jsonBuildObjInt`.
- FR-CORE-JSON-3: Документы: `jsonFileLoadDoc`, `jsonFileSaveDoc`, `save_jsonDoc`, `load_jsonDoc` (merge-сохранение конфигов модулями).
- FR-CORE-JSON-4: Wi-Fi слоты: `jsonBuildSlotConfig`, `jsonParseSlotConfig`, `jsonFileLoadSlot`, `jsonFileSaveSlot`.
- FR-CORE-JSON-5: Дамп JSON в лог должен быть отключаемым (`JSON_DUMP_LOG`, по умолчанию выключен).

**Веб-маршруты:** `/json/ver`.
**Конфиги:** не владеет.
**Зависимости:** ArduinoJson 7, LittleFS, ESPAsyncWebServer.

---

#### 3.1.7. `core_led` — индикация светодиодом

**Каталог:** `src/core_led/` · **Класс:** отсутствует (свободные функции) · **.ini:** отсутствует; инициализируется вручную из `main.cpp` (`ledInit()`).

**Назначение:** неблокирующий приоритетный блинкер светодиода (на EERTOS) с базовым состоянием; строковые макросы паттернов, хранение паттернов в PROGMEM.

**Функциональные требования:**
- FR-CORE-LED-1: `ledInit()` инициализирует GPIO `CONNECTION_LED` (при `-1` — отключено); для ESP32/ESP8266 уровень может инвертироваться.
- FR-CORE-LED-2: Приоритеты слотов `LED_PRIO_DEV/DEMO/OTA/WIFI/MANUAL` (0..4), минимальное время слота `SLOT_MIN_TIME=100`.
- FR-CORE-LED-3: API: `ledSetState(prio, pattern, times)`, `ledClearState(prio)`, `ledSetSteady(on)`, `espLedOn/Off`, `ledMacroTimerTask`, `ledMacroBlinker`, `ledMacroRst`, `LedMacroSet(str, times)`.
- FR-CORE-LED-4: Ограничение длины паттерна `LEDSTRINGLIMIT=128`.

**Веб-маршруты:** нет. **Терминал:** команда `led` (обработчик `BlinkCmd`).
**Зависимости:** Arduino GPIO, `ns_core_led`, EERTOS.

---

#### 3.1.8. `core_terminal` — последовательный терминал

**Каталог:** `src/core_terminal/` · **Класс:** отсутствует (`SerialTerminal term`) · **.ini:** отсутствует; `TerminalInit()`/`TerminalLoop()` в `core_begin`/`core_loop`.

**Назначение:** последовательный терминал (форк ErriezSerialTerminal) с базовыми командами и ленивыми слотами команд модулей; установка/показ идентичности.

**Функциональные требования:**
- FR-CORE-TERM-1: `TerminalInit()` регистрирует базовые команды, `TerminalLoop()` обслуживает ввод в главном цикле.
- FR-CORE-TERM-2: Зарегистрированные команды: `help`, `reset`, `echo`, `?`, `id`, `1`, `udpp`, `udpc`, `udps`, `led`.
- FR-CORE-TERM-3: `udpp/udpc/udps` доступны только при включённом `MODULE_UDP`.
- FR-CORE-TERM-4: Механизм слотов `TerminalRegisterModule(initFn)` (до `TERMINAL_MODULE_SLOTS=8`), применяемых лениво при первом `TerminalLoop()`.
- FR-CORE-TERM-5: Команды `flash/flash2/stm32/swdf/avr` в текущей версии закомментированы и не активны.

**Зависимости:** форк `ErriezSerialTerminal`, `core_ntp`, `core_led`, `core_sys`, `ident_store`, опционально `module_udp`/`submodule_swd`.

---

#### 3.1.9. `core_state` — ресурсная шина

**Каталог:** `src/core_state/` · **Класс:** `CLASS_CORE_STATE` · **Объект:** `core_state`
**`[registry]`:** `object=core_state`, `define=CORE_STATE`, `namespace=system`, `web=1`, `loop=1`, `res=1`, `prio=100`.

**Назначение:** реестр ресурсов — pull-чтение состояний, push-события, подписки, синхронные/асинхронные вызовы функций, режимы ядра с таймаутами и приоритетами, каталог для UI.

**Функциональные требования:**
- FR-CORE-STATE-1: Регистрация: `regState`/`regStateAs` (BOOL/I32/F32/STR/TIME/ENUM), `regEnum`, `regEvent`, `regFunc`, `regFuncAsync`, `regFuncCode`.
- FR-CORE-STATE-2: Контекст: `setNamespace`/`clearNamespace`, `setPrivileged`, `setModulePrio`; генератор выставляет namespace/priv/prio перед `register_resources()`.
- FR-CORE-STATE-3: Чтение: `getBool/getInt/getF32/getStr/getTime/has/info/mode`; запись: `setBool/setInt/setF32/setStr/setTime`.
- FR-CORE-STATE-4: События: `on`/`off`/`emit`/`signal`, очередь `EV_QUEUE=16`.
- FR-CORE-STATE-5: Вызовы: sync `call`; async `call_async`/`cancel_async`/`asyncComplete`/`setAsyncOwner`/`asyncCancelFor`; таймаут async по умолчанию 10 с; до 3 параллельных вызовов на владельца; `call()` для async и `call_async()` для sync дают `ERR_BAD_TYPE`.
- FR-CORE-STATE-6: Режимы ядра `system.mode` (ENUM `init/normal/ota/fs_update/prog/test`), событие `system.mode_changed`, API `getMode/setMode/requestMode`.
- FR-CORE-STATE-7: Коды возврата: `0` — успех, `>0` — коды модуля, `<0` — ошибки ядра (`NOT_REGISTERED -1` … `DENIED -13`).
- FR-CORE-STATE-8: Права: запись в чужой namespace логируется warning при `DEBUG_STATE`; привилегированные namespace защищены от внешней записи.
- FR-CORE-STATE-9: Каталог ресурсов для UI/макросов (`catalogToJson`, `catalogModulesToJson`).
- FR-CORE-STATE-10: Ограничения: `CORE_STATE_MAX_RES=64`, `MAX_NS=16`, `MAX_SUBS=16`, `MAX_ASYNC=6`, `MAX_CODES=8`, `MAX_ENUM=8`, `MAX_ARGS=4`; длины имён 24/32/56.
- FR-CORE-STATE-11: Системный тик 1 с; раздача очереди событий — в `loop()`.

**Веб-маршруты:** `GET /state/catalog`, `/state/info`, `/state/set`, `/state/call`, `/state/modules`, `/state/module_mode`, `/state/ver`.
**Веб-файлы:** `state.html`, `_menu.html`, `config_state.json`.
**Конфиги:** `/config_state.json` (`test_timeout_s`, `op_timeout_s`, по умолчанию 1800 с).
**Зависимости:** ArduinoJson 7, ESPAsyncWebServer, LittleFS, `ns_core_state`.

---

#### 3.1.10. `core_task` — именованные задачи поверх EERTOS

**Каталог:** `src/core_task/` · **Класс:** `CLASS_CORE_TASK` · **Объект:** `core_task`
**`[registry]`:** `object=core_task`, `define=CORE_TASK`, `namespace=task`, `web=0`, `loop=1`, `res=0`, `prio=90`.

**Назначение:** именованные периодические/отложенные задачи поверх EERTOS. Второго планировщика нет; каждый слот использует свою статическую trampoline-функцию (EERTOS `SetTimerTask` идемпотентен по указателю).

**Функциональные требования:**
- FR-CORE-TASK-1: `every(name, fn, period_ms, fire_now=false)` — периодическая задача.
- FR-CORE-TASK-2: `after(name, fn, delay_ms)` — отложенная задача.
- FR-CORE-TASK-3: `cancel(name)`, `cancelAll()`.
- FR-CORE-TASK-4: Диагностика переполнения слотов в `loop()`; лимиты `CORE_TASK_SLOTS=12`, `CORE_TASK_NAME_LEN=24`.

**Веб-маршруты:** нет. **Конфиги:** нет.
**Зависимости:** EERTOS (`core_sys/eertos.h`).

---

#### 3.1.11. EERTOS — кооперативный планировщик

**Каталог:** `src/core_sys/` (`eertos.h/.cpp`)

- FR-EERTOS-1: `SetTask(TPTR)` / `SetTaskEx(TPTR)` — постановка функции в очередь главного цикла.
- FR-EERTOS-2: `SetTimerTask(TPTR, uint32_t)` / `SetTimerTaskEx` — планирование через N мс; `DelTimerTask(TPTR)` — удаление.
- FR-EERTOS-3: `SetTaskFromISR(TPTR)` — безопасная постановка из прерывания.
- FR-EERTOS-4: `TaskManager()` — выполнение одной задачи за вызов; `TimerService()` — вызов из ISR Ticker 1 мс.
- FR-EERTOS-5: Диагностика `EertosDroppedCount`; `TaskQueueSize=30`, `MainTimerQueueSize=30`.

---

#### 3.1.12. `common/` и общие утилиты

**Каталог:** `src/common/`, `src/debug.h`, `src/debug_prefix.cpp`

- FR-COMMON-1: `escapeHtml(const String&)` — экранирование для HTML + защита разделителей CVT (`& < > "`, `|`→`&#124;`, CR/LF→пробел).
- FR-COMMON-2: `escapeJson(const String&)` — экранирование JSON (кавычки, `\`, управляющие символы, `\uXXXX`).
- FR-COMMON-3: `hex2bin`, `urldecode`, `formatBytes`, `checkRange`, `h2int`; форк `TimeLib`, `StringArray`.
- FR-COMMON-4: Единый `DBG_MOD` печатает префикс модуля (`[C_]/[M_]/[D_]`) только в начале строки; при `RELEASE` все отладочные макросы — пустышки.
- FR-COMMON-5: Дублирующие локальные реализации `escHtml`/`escapeJsonStr`/`macroJsonEscape` запрещены.
- FR-COMMON-6: Локальные stateless-хелперы компонента — в `<компонент>/common_module.*` в namespace `ns_<dirname>`.

---

### 3.2. Опциональные модули (`module_*`, `submodule_*`)

> Общее требование для всех подразделов 3.2: модуль **не работоспособен без ядра**, подключается через `src_filter` + `build_flags`, а его `[registry]`-секция генерирует вызовы в `modules_registry.cpp`.

#### 3.2.1. `module_program` → `module_prog` — база программатора

**Каталог:** `src/module_program/module_prog/` · **Класс:** `Class_ProgBase` (абстрактный) · **Объект:** отсутствует · **`.ini`:** отсутствует (обрабатывается генератором как модуль без ini).

**Назначение:** общая база субмодулей программатора. Владеет проектной конфигурацией, списком файлов прошивок, загрузкой файлов в FS (MD5 + размер), информацией о диске, опросом статуса чипа, версионной точкой. Работа с чипом делегирована виртуальным методам.

**Функциональные требования:**
- FR-MODPROG-1: Абстрактный интерфейс для наследников: `getProgTypePrefix`, `isFlashBusy`, `getFlashPercent`, `chipSpecificInit`, `web_FileUpload2Chip`, `onFlashComplete`, `getChipCfgJsonPath`.
- FR-MODPROG-2: Конфиг `/config_prog.json` (`project`, `chip_name`), API `cfg_FileStructGet/SaveFromWeb/SetDefault/FileLoad/FileSave`.
- FR-MODPROG-3: Список `/prog_filelist.json` (`filename`, `upload_date`, `md5`, `prog_date`, `prog_status`, `prog_time`, `prog_error`, `prog_error_stage`, `prog_error_percent`, `prog_speed`).
- FR-MODPROG-4: Загрузка файлов в FS с MD5 (`web_FileUpload2FS`, `file_ComputeMD5`), удаление (`web_FileDelete`), информация о диске (`web_GetDiskInfo`).
- FR-MODPROG-5: Миграция старых путей (`config_prog_isp.json`, `config_prog_swd.json`, `isp_filelist.json`, `swd_filelist.json`).
- FR-MODPROG-6: Коды ошибок `progerr_t`: `-1` SIGN, `-2` BUSY, `-3` FLASH, `-4` ERASE, `-5` HEX, `-6` CFG, `-8` OPENFILE, `-9` INCORRECTFILE, `-10` NOFILE, `-11` HEXCRC, `-12` HEXADDR, `-13` HEXMEMOVER, `-14` CHIP_OFFLINE, `-15` CHIP_MISMATCH, `-16` CHIP_NOT_IN_CFG.

**Общие веб-маршруты (для обоих субмодулей):** `/prog/diskinfo`, `/prog/fileslist`, `/prog/delete`, `POST /prog/uploadfile`, `/prog/uploadstat`, `/prog/setmd5`, `/prog/uploadsize`, `/prog/progress`, `/prog/flash`, `/prog/ver`, `/prog/chipstatus`, `/project/info`, `/project/save`, `/project/chips`, `/project/chipinfo`, `GET /project`, `GET /prog`. Все защищены `checkAuth`.
**Веб-файлы:** `_menu.html`, `prog.html`, `project.html`, `config_prog.json` (использует shared `spark-md5.js` из `core_ota`).
**Зависимости:** LittleFS, ArduinoJson, `core_sys/eertos.h`, `core_web`, `core_json`, `core_ntp`, `common/common.h`.

---

#### 3.2.2. `submodule_isp` — программатор AVR ISP

**Каталог:** `src/module_program/submodule_isp/` · **Класс:** `Class_SubIsp : public Class_ProgBase` · **Объект:** `progIsp`
**`[registry]`:** `object=progIsp`, `define=PROGTYPE_ISP`, `web=1`, `loop=0` (без `namespace`/`res`/`prio`).

**Назначение:** программирование AVR (STK500-совместимое, bit-bang SPI + reset), чтение сигнатуры и фьюзов, запись фьюзов, поиск чипа в базе, автомат определения чипа. Обёртка над `ESP_AVRISP avrprog`.

**Функциональные требования:**
- FR-ISP-1: Читать подпись чипа (`chipSignRead`) и находить в базе `/avrisp_cfg.json` по `signature` (`chipCfg_FindBySignature`).
- FR-ISP-2: Сверять найденный чип с `chip_name` из `/config_prog.json`; при несовпадении — `HTTP 423`/`ERR_CHIP_MISMATCH`.
- FR-ISP-3: Для неизвестной сигнатуры без заданного имени — по умолчанию flash 32768 Б, page 128 Б.
- FR-ISP-4: Прошивать HEX или BIN постранично, кооперативно (`startFlash` + `beginFlashStep`; `FLASH_IDLE/INIT/WRITE/DONE`), с прогрессом (`getPercent`) и верификацией (`chipFlashVerification`).
- FR-ISP-5: Чтение/запись фьюзов (`chipFusesRead`, `chipFusesWrite`).
- FR-ISP-6: Определять чип с повторами (15 попыток; `CHIP_IDLE/INIT/PROBE/DONE`).
- FR-ISP-7: База `/avrisp_cfg.json` — `chips[]`: `signature`, `name`, `flash_size`, `page_size`; поставка: ATmega328P (`0x1E950F`, 32768/128), ATmega168P (`0x1E940B`, 16384/128).
- FR-ISP-8: Константы: `AVRISP_SPI_FREQLOW=100000`, `AVRISP_SPI_FREQHIGH=500000`, `MEM_PAGE_SIZE=128`.
- FR-ISP-9: Выводы по умолчанию: `PIN_MISO=19`, `PIN_MOSI=23`, `PIN_SCK=18`, `PIN_RST=5`.

**Веб-маршруты:** `GET /avr/fuseread`, `POST /avr/fusewrite`, `/avr/info`, `POST /avr/save`, `GET /avr/readsignature`, `GET /avrcfg`.
**Веб-файлы:** `_menu.html`, `avrcfg.html`, `avrisp_cfg.json`, `avrisp.json` (устаревший).
**Терминал:** активных команд нет.
**Зависимости:** `SPI.h`, `common/common.h` (`hex2bin`), `format_hex`/`format_bin`.

---

#### 3.2.3. `submodule_swd` — программатор STM32 SWD

**Каталог:** `src/module_program/submodule_swd/` · **Класс:** `Class_SubSwd : public Class_ProgBase` · **Объект:** `progSwd`
**`[registry]`:** `object=progSwd`, `define=PROGTYPE_SWD`, `web=1`, `loop=0`.

**Назначение:** программирование STM32 F1/F4 по SWD (bit-bang SWD поверх GPIO), проверка IDCODE, mass-erase + program + reset. Обёртка над `ESP_PROGSWD swdprog`.

**Функциональные требования:**
- FR-SWD-1: Читать IDCODE (`swd_init`), находить чип в `/swd_cfg.json` по `idcode` (`chipCfg_FindById`).
- FR-SWD-2: Сверять чип с `chip_name` из `/config_prog.json`.
- FR-SWD-3: Применять параметры чипа (`flash_start`, `flash_size`, `page_size`, `word_size`, `csw_value`); выбирать алгоритм F1/F4 по полю `family`.
- FR-SWD-4: Прошивать HEX потоково или BIN постранично, кооперативно (`startFlash` + `beginFlashStep`), с прогрессом и скоростью (`getSpeed`).
- FR-SWD-5: Выполнять `mass_erase`/`erase_sector`, запись 16/32-битными словами, halt/unhalt/reset.
- FR-SWD-6: Значения по умолчанию: `flash_start=0x08000000`, `page_size=1024`, `word_size=2`, `csw_value=0xA2000002`.
- FR-SWD-7: База `/swd_cfg.json` — `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `page_size`, `word_size`, `csw_value`; поставка: STM32F103C8 (`0x1BA01477`, `stm32f1`, 65536 Б), STM32F411 (`0x2BA01477`, `stm32f4`, 524288 Б).
- FR-SWD-8: Выводы по умолчанию: `SWDPIN_CLK=21`, `SWDPIN_DATA=19`.

**Веб-маршруты:** только общие маршруты `module_prog`; собственных нет.
**Веб-файлы:** `_menu.html`, `swd_cfg.json`.
**Терминал:** активных команд нет.
**Зависимости:** GPIO, `ESP_PROGSWD`, формат-хелперы.

---

#### 3.2.4. Формат-хелперы программатора

**Каталоги:** `module_prog/format_hex.*`, `module_prog/format_bin.*`

- FR-FMT-1: Intel HEX — `hexFileLineParser`, `hexFileParseStream` (валидация), `hexFileParseStreamWrite` (потоковая запись через callback), `hexFileIsFormat`, `hexFileGetBinarySize`.
- FR-FMT-2: Проверки HEX: CRC, монотонность адресов, переполнение памяти чипа (коды −9/−10/−11/−12/−13), ошибка записи −14.
- FR-FMT-3: BIN — `binFileOpen/GetSize/ReadPage/Close/IsFormat` (`.bin`/`.binary`).
- FR-FMT-4: Формат определяется по расширению в `startFlash()`; неизвестное расширение — BIN.

---

#### 3.2.5. `module_udp` — UDP broadcast

**Каталог:** `src/module_udp/` · **Класс:** `CLASS_MODULE_UDPBROADCAST` · **Объект:** `module_udp`
**`[registry]`:** `object=module_udp`, `define=MODULE_UDP`, `web=1`, `loop=0`.

**Назначение:** UDP-обнаружение (broadcast/unicast) и телеметрия; строка-«пароль» (`Ave_Omnissiah`), режим ответа, отслеживание доступности сервера с фоллбэком на broadcast.

**Функциональные требования:**
- FR-UDP-1: Конфиг `/config_udp.json`: `udpPortTx`, `udpPortRx`, `udpTimeOut`, `udpkeyword`, `udpPowerOn`, `udpResponse`, `udpMissLimit`.
- FR-UDP-2: Отправка broadcast (`broadcastSend`) и unicast (`unicastSend`), тест (`broadcastTest`), останов (`stop`).
- FR-UDP-3: Отслеживание сервера (`serverOnlineGet`, `serverIpGet`, `lastContactMsGet`) с фоллбэком при пропуске лимита.
- FR-UDP-4: Порт по умолчанию `UDP_PORT=40001`.
- FR-UDP-5: `begin()` вызывается **не** из registry, а из `core_wifi` при подключении к сети; `stop()` — при потере соединения.

**Веб-маршруты:** `POST /udp.html`, `GET /udp/info`, `/udp/test`, `GET /udp/status`, `/udp/ver`.
**Веб-файлы:** `udp.html`, `_menu.html`, `config_udp.json`.
**Терминал:** `udpp`/`udpc`/`udps` реализованы в `core_terminal` и вызывают API модуля.
**Зависимости:** `AsyncUDP.h` (ESP32) / `ESPAsyncUDP.h` (ESP8266).

---

#### 3.2.6. `module_editor` — редактор файловой системы

**Каталог:** `src/module_editor/` · **Класс:** `CLASS_MODULE_EDITOR` · **Объект:** `module_editor`
**`[registry]`:** `object=module_editor`, `define=MODULE_EDITOR`, `web=1`, `loop=0`.

**Назначение:** браузер/редактор LittleFS (форк Ace): список, создание, удаление, загрузка, чтение/запись текстовых файлов; информация об использовании ФС. Ранее входил в ядро (`core_editor`), теперь опционален.

**Функциональные требования:**
- FR-EDITOR-1: `GET /editor/list` — список файлов; `GET /editor/fsinfo` — использование ФС.
- FR-EDITOR-2: `GET /editor/edit` — страница; `PUT /editor/edit` — создание; `DELETE /editor/edit` — удаление; `POST /editor/edit` — загрузка.
- FR-EDITOR-3: Ограничение загрузки `MAX_UPLOAD_SIZE=1 МБ`.
- FR-EDITOR-4: Интеграция с `module_macros` («Open in editor»).

**Веб-файлы:** `edit.html`, `ace.js`, `mode-*.js`, `theme-*.js`, `_menu.html`.
**Конфиги:** нет. **Зависимости:** LittleFS (Ace — vendored JS).

---

#### 3.2.7. `module_ds3231` — часы реального времени DS3231

**Каталог:** `src/module_ds3231/` · **Класс:** `CLASS_MODULE_DS3231` · **Объект:** `module_ds3231`
**`[registry]`:** `object=module_ds3231`, `define=MODULE_DS3231`, `web=1`, `loop=0`.

**Назначение:** RTC DS3231 по I2C: чтение/установка времени, Alarm1/Alarm2, температура, регистры управления/статуса, мониторинг SQW/INT# (ESP32).

**Функциональные требования:**
- FR-DS3231-1: Чтение/установка времени (`getTime`, `setTime`), проверка связи (`isConnected`, `getAddr`, `getLastError`).
- FR-DS3231-2: Будильники Alarm1/Alarm2 (`getAlarm1/setAlarm1`, `getAlarm2/setAlarm2`), фиксация срабатывания (`getAlarmFired1/2`, `getLastAlarm1/2Time`).
- FR-DS3231-3: Температура (`getTemperature`), регистр статуса (`getStatusReg`).
- FR-DS3231-4: SQW-вывод (ESP32): `sqwGpioInit/Reinit/Stop`, `sqwEnable/DisableInterrupt`, `getSqwLevel`; пин `DS3231_SQW_PIN=13`.
- FR-DS3231-5: Конфиг `/config_ds3231.json`: `addr`, `autoPoll`, `pollInterval`, `sqwEnabled`, `sqwMode`, `sqwLevelActive`, `ctrlBbsqw`, `ctrlRs`, `ctrlIntcn`, `ctrlA1ie`, `ctrlA2ie`.

**Веб-маршруты:** `GET /ds3231/read`, `/ds3231/poll`, `POST /ds3231/set_time`, `/ds3231/set_alarm1`, `/ds3231/set_alarm2`, `/ds3231/set_reg`, `/ds3231/save`, `GET /ds3231/info`, `/ds3231/ver`.
**Терминал:** `ds-alarm`, `ds-sqw`, `ds-sqr`.
**Зависимости:** `Wire`, `common/TimeLib`, `core_ntp/NtpClientLib`.

---

#### 3.2.8. `module_gpio` — GPIO через web

**Каталог:** `src/module_gpio/` · **Класс:** `CLASS_MODULE_GPIO` · **Объект:** `module_gpio`
**`[registry]`:** `object=module_gpio`, `define=MODULE_GPIO`, `web=1`, `loop=0`.

**Назначение:** минимальный веб-модуль управления GPIO (демонстрационный). Разбирает аргументы формы `uartstr`/`led1..led4`.

**Функциональные требования:**
- FR-GPIO-1: `POST /gpio` — приём параметров формы; запись выходов выполняет обработчик.
- FR-GPIO-2: Конфиг отсутствует.

**Веб-файлы:** `gpio.html`, `_menu.html`.
**Примечание:** `html_ver_get` существует, но маршрут `/gpio/ver` не зарегистрирован; функциональность неполная — модуль-заглушка.

---

#### 3.2.9. `module_lcd-i2c` — LCD I2C

**Каталог:** `src/module_lcd-i2c/` · **Класс:** `CLASS_MODULE_I2C_LCD` · **Объект:** `module_lcd_i2c`
**`[registry]`:** `object=module_lcd_i2c`, `define=MODULE_LCD_I2C`, `web=1`, `loop=0`.

**Назначение:** управление LCD-дисплеем I2C: до 4 строк, подсветка, настройка адреса/столбцов/строк, раскрытие масок `date`/`time`, задача перерисовки 1 с.

**Функциональные требования:**
- FR-LCD-1: Инициализация дисплея по I2C (`LCD_I2C_SDA/SCL`); лимиты `LCD_I2C_MAX_ROWS=4`, `LCD_I2C_MAX_LINE_LEN=40`.
- FR-LCD-2: Конфиг `/config_lcd-i2c.json`: `i2cAddr` (по умолчанию `0x27`), `cols`, `rows`, `backlight`, `display_lines[]`.
- FR-LCD-3: Поддержка масок даты/времени в строках; перерисовка раз в секунду.
- FR-LCD-4: Включение/выключение подсветки без перезагрузки.

**Веб-маршруты:** `GET /lcd-i2c/info`, `POST /lcd-i2c/save_content`, `/lcd-i2c/save_config`, `GET /lcd-i2c/sysinfo`, `POST /lcd-i2c/backlight`, `GET /lcd-i2c/ver`.
**Веб-файлы:** `lcd-i2c.html`, `lcd-i2c2.html`, `_menu.html`.
**Зависимости:** `LiquidCrystal_I2C`, `Wire`.

---

#### 3.2.10. `module_macros` — макросы/сценарии (Lua)

**Каталог:** `src/module_macros/` · **Класс:** `CLASS_MODULE_MACROS` · **Объект:** `module_macros`
**`[registry]`:** `object=module_macros`, `define=MODULE_MACROS`, `namespace=macros`, `web=1`, `loop=0`, `res=1`, `prio=60`, `priv=1`.

**Назначение:** пользовательские сценарии на Lua с триггерами cron/cond/event и декларативными действиями через ресурсную шину; единый Lua-интерпретатор на модуль; веб-менеджер файлов.

**Функциональные требования:**
- FR-MACROS-1: Единый `lua_State` на модуль (не на файл) — иначе heap исчерпывается и `AsyncWebServer` падает.
- FR-MACROS-2: Файл-сценарий — Lua-чанк, возвращающий таблицу: `desc` (опц.), `meta_cron` (опц.), `handlers` (опц.), `rules` (обязательно, 1..`MACRO_MAX_RULES`).
- FR-MACROS-3: Правило содержит не более одного триггера из `cron`/`term`/`button`/`on` (внутри `when`) и ровно одно действие: `set`, `call`, `calls` или `run`.
- FR-MACROS-4: `cond` — `{res, op, val}` или массив (AND), до `MACRO_MAX_CONDS=4`; операторы `== != < <= > >= changed`; фронт `false→true`.
- FR-MACROS-5: Cron — 6 полей (секунды первыми), 5/7 полей нормализуются; требует NTP; `meta_cron` — необязательное поле возвращаемой сценарием таблицы `.lua` (гейт окна для всех правил файла), в `config_macros.json` не сохраняется.
- FR-MACROS-6: Действия `call`/`calls` — декларативные bus-вызовы (до `MACRO_MAX_ACTIONS=4`, до `MACRO_CALL_MAX_ARGS=4`); `run` — named-handler с перечитыванием файла (тяжёлый).
- FR-MACROS-7: Lua bus API: `get`, `set`, `call`, `has`, `mode`, `info`, `puts`/`print`, `clock`, `now`; `call_async` из Lua запрещён.
- FR-MACROS-8: Handler получает таблицу `event` (`type`, `spec`, `args`, `value`); глобалы между вызовами не живут.
- FR-MACROS-9: Конфиг `/config_macros.json`: `enabled` (bool), `files[]` (`name`, `prio` 0..7, `run`, `created`).
- FR-MACROS-10: Лимиты: `MACRO_MAX_FILES=30`, `MACRO_MAX_RULES=8`, `MACRO_EV_QUEUE=8`, `MACRO_MAX_EVENT_SUBS=8`, `MACRO_HEAP_BUDGET=100 КБ`, `MACRO_HEAP_CRIT_PCT=95%`, `MACRO_LUA_MAX_OPS=100000`, `MACRO_PARSE_PER_TICK=2`, `MACRO_PARSE_MAX_RETRY=3`.
- FR-MACROS-11: Ресурсы bus: `macros.enabled` (BOOL), `macros.files` (I32); функция `macros.reload`; namespace `macros` привилегированный.
- FR-MACROS-12: Редактирование/сохранение сценария разрешено только для остановленного файла.
- FR-MACROS-13: Защита от OOM в `/macros/list` (порог 6000 Б) и `/macros/resources` (8000 Б), страховка `doc.overflowed()`.
- FR-MACROS-14: Конфиг сохраняется только по действию пользователя (не автоматически в тике).

**Веб-маршруты:** `GET /macros/list`, `/macros/create`, `/macros/delete`, `/macros/rename`, `/macros/state`, `/macros/prio`, `/macros/reload`, `/macros/fire`, `/macros/resources`, `/macros/validate`, `/macros/heap`, `/macros/save`, `/macros/get`, `/macros/ver`.
**Терминал:** `macro` с подкомандами `list`, `reload [file]`, `run <file>`, `stop <file>`, `prio <file> <delta>`, `msg <words...>`, `btn <words...>`.
**Веб-файлы:** `macros.html`, `cron.html`, `macros_help.html`, `_menu.html`, примеры `web/macros/*.lua`.
**Зависимости:** vendored EspLuaEngine (Lua 5.4.7), `ccronexpr`, `core_state`, `core_json`, NTP. **Только ESP32.**

---

#### 3.2.11. `module_otaclient` — OTA-клиент

**Каталог:** `src/module_otaclient/` · **Класс:** `CLASS_MODULE_OTACLIENT : public CLASS_CORE_OTA` · **Объект:** `module_otaclient`
**`[registry]`:** только `object=module_otaclient`, `define=MODULE_OTACLIENT`; размещается в **core**-группах (begin/web_Init/loop) рядом с базовым OTA.

**Назначение:** автоматическое удалённое обновление — опрос манифеста по HTTP, сравнение версий FW/FS, загрузка и прошивка с повторами и прогрессом.

**Функциональные требования:**
- FR-OTACLIENT-1: Периодический опрос манифеста (`checkForUpdates`) по `serverAddress:serverPort/manifestPath` с таймаутом `timeOut`.
- FR-OTACLIENT-2: Сравнение версий FW/FS, загрузка и прошивка при необходимости; `OTACLIENT_MAX_RETRIES=3`, `OTACLIENT_CHUNK_SIZE=1024/512`, до `OTACLIENT_MAX_MANIFEST_ENTRIES=20` записей.
- FR-OTACLIENT-3: Запуск проверки по подключению Wi-Fi (`onWiFiConnect`) и/или по включению питания (`powerOn`).
- FR-OTACLIENT-4: Конфиг `/config_otaclient.json`: `timeOut`, `powerOn`, `serverAddress`, `serverPort`, `manifestPath`.
- FR-OTACLIENT-5: Тестовое подключение (`/otaclient/test`, `/otaclient/teststatus`).
- FR-OTACLIENT-6: Наследует общие маршруты OTA (`/update*`) и ресурсы `ota.state`, `ota.server_reachable`.

**Веб-маршруты:** `POST /otaclient.html`, `GET /otaclient/info`, `/otaclient/test`, `/otaclient/teststatus`, `/otaclient/update`, `/otaclient/ver`.
**Веб-файлы:** `otaclient.html`, `_menu.html`.
**Зависимости:** `WiFiClient`, `Update.h` (ESP32) / `ArduinoOTA` (ESP8266), `core_ota`, `core_json`, NTP.

---

#### 3.2.12. `module_rgb` — RGB-лента/матрица WS2812

**Каталог:** `src/module_rgb/` · **Класс:** `CLASS_MODULE_RGB` · **Объект:** `module_rgb`
**`[registry]`:** `object=module_rgb`, `define=MODULE_RGB`, `web=1`, `loop=0`.

**Назначение:** управление лентой/матрицей WS2812 (NeoPixelBus): режимы solid/rainbow/gradient/individual/equalizer, яркость, анимационный таймер, отложенное применение/сохранение конфига.

**Функциональные требования:**
- FR-RGB-1: Конфиг `/config_rgb.json`: `dataPin`, `numLeds`, `brightness`, `mode` (0..4), `effectSpeed`, `solidColor`, `gradStartColor`, `gradEndColor`, `individualColors[]`, `eqBands`, `eqLedsPerBand`.
- FR-RGB-2: Лимиты `RGB_MAX_LEDS=100`, `RGB_DEFAULT_LEDS=10`.
- FR-RGB-3: Режимы: сплошной цвет, радуга, статичный градиент, индивидуальные цвета, эквалайзер.
- FR-RGB-4: Установка пикселя (`/rgb/setPixel`) и сохранение конфига (`/rgb/save`); чтение `/rgb/info`, `/rgb/ver`.
- FR-RGB-5: Анимация — таймером EERTOS; применение конфига — отложенной задачей.

**Веб-файлы:** `rgb.html`, `_menu.html`.
**Зависимости:** `makuna/NeoPixelBus` (ESP32 RMT, ESP8266 UART1).

---

#### 3.2.13. `module_i2c-mapper` — сканер I2C

**Каталог:** `src/module_i2c-mapper/` · **Класс:** `CLASS_MODULE_I2C_MAPPER` · **Объект:** `module_i2c_mapper`
**`[registry]`:** `object=module_i2c_mapper`, `define=MODULE_I2C_MAPPER`, `web=1`, `loop=0`.

**Назначение:** сканирование адресов шины I2C 0x01..0x7E (Wire0) с кормлением watchdog; результат через web и последовательную карту.

**Функциональные требования:**
- FR-I2CMAP-1: `POST /i2cmapper/scan` возвращает JSON-массив найденных адресов вида `"0xNN"`.
- FR-I2CMAP-2: Сканирование с кормлением `esp_task_wdt` (ESP32).
- FR-I2CMAP-3: Выводы `I2C_MAPPER_SDA/SCL` (ESP32 21/22, ESP8266 4/5).
- FR-I2CMAP-4: Терминальная команда `i2c-scan` печатает ASCII-карту шины.

**Веб-файлы:** `i2cmapper.html`, `_menu.html`.
**Зависимости:** `Wire`, `esp_task_wdt.h`.

---

#### 3.2.14. `module_template` — шаблон нового модуля

**Каталог:** `src/module_template/` (в ядре, не клон) · **Класс:** `CLASS_MODULE_TEMPLATE` · **Объект:** `module_template`
**`[registry]`:** `object=module_template`, `define=MODULE_TEMPLATE`, `web=1`, `loop=0`.

**Назначение:** эталонный пример опционального модуля — два GPIO-выхода (мигание), конфиг с массивом, AJAX-страница с `GetJson.js`/`GetMarkup.js`.

**Функциональные требования:**
- FR-TEMPLATE-1: Образец структуры: `.h` + `.cpp` (шаблонный блок) + `_types.h` + `_engine.cpp` + `web/`.
- FR-TEMPLATE-2: Образец порядка функций: INCLUDES → объекты → `setFs` → `begin`/`begin(ctx)` → `web_Init` → веб-обработчики → конфиг → версии → логика.
- FR-TEMPLATE-3: Образец паттернов: AJAX-сохранение без перезагрузки, чтение/запись массивов JSON, отложенное сохранение.
- FR-TEMPLATE-4: Не включать в реальные сборки как функциональный модуль (только эталон).

**Веб-маршруты:** `POST /template/save`, `POST /template/save_demo`, `GET /template/info`, `/template/time`, `/template/ver`.
**Веб-файлы:** `template.html`, `template2.html`, `_menu.html`, `config_template.json`.

---

### 3.3. Устройства (`device_*`)

> Общее требование для всех подразделов 3.3: устройство **не работоспособно без ядра**; для сборки необходимо клонировать само устройство и все указанные в его `.ini` модули.

#### 3.3.1. `device_clock-mech` — механические часы (шаговый двигатель)

**Каталог:** `src/device_clock-mech/` · **Класс:** `CLASS_DEVICE_CLOCKMECH` · **Объект:** `device_clock_mech`
**`[registry]`:** `object=device_clock_mech`, `define=DEVICE_CLOCKMECH`, `web=1`, `loop=0`.
**Env:** `esp32_clock-mech` (extends `env:esp32`); `src_filter`: `+<module_udp/> +<module_otaclient/> +<device_clock-mech/> +<module_ds3231/>`; флаги: `MODULE_UDP=1`, `MODULE_OTACLIENT=1`, `MODULE_DS3231=1`, `DS3231_SQW_PIN=13`, `DEVICE_CLOCKMECH=1`, `DEBUG_*`.

**Назначение:** управление механизмом часов через шаговый двигатель (STEP/DIR/EN драйвера A4988), парковка/обнуление часовой и минутной стрелок по двум оптическим датчикам и подсветке датчиков, подсчёт шагов на оборот (калибровка), опрос реального времени и вывод стрелок в целевое положение.

**Функциональные требования:**
- FR-CLOCKMECH-1: Два режима `enable_status`: DEBUG (ручное управление) и WORK (автономная работа).
- FR-CLOCKMECH-2: Калибровка `stepsPerRevolution` и лимит ошибки `errorLimitSteps`.
- FR-CLOCKMECH-3: Источник времени `timeSource`: `ds3231` (I2C RTC + SQW пин 13) или `ntp` (`now()`).
- FR-CLOCKMECH-4: Установка стрелок в целевое время (`cmdSetArrows`, `/clock-mech/set-xx00`, `/clock-mech/set-12xx`).
- FR-CLOCKMECH-5: Управление драйвером: `cmdStep`, `cmdDir`, `cmdEn`, `cmdSled` (подсветка датчиков), `cmdSens` (чтение датчиков).
- FR-CLOCKMECH-6: Периодический опрос `pollInterval`; при превышении `errorLimitSteps` — индикация ошибки (`ledMacrosClockMechError`).
- FR-CLOCKMECH-7: Сохранение конфига — только web `handleSave` и терминал `c-save` (bus-функции `save` нет).
- FR-CLOCKMECH-8: Выводы: DIR=33, STEP=12, EN=14, SENS_LED=27, SENS_MIN=26, SENS_HOUR=25; `CONNECTION_LED=2`.

**Веб-маршруты:** `POST /clock-mech/save`, `/clock-mech/reset`, `/clock-mech/count`; GET: `/clock-mech/info`; GET: `/clock-mech/step|dir|en|sled|sens|n|reset|set-xx00|set-12xx|count|status|ver`.
**Веб-файлы:** `clock-mech.html`, `_menu.html`.
**Конфиг:** `/config_clock-mech.json`: `enable_status`, `timeSource`, `stepsPerRevolution`, `pollInterval`, `errorLimitSteps`, `sensorLedEnabled`.
**Зависимости модулей:** `module_udp`, `module_otaclient`, `module_ds3231`.

---

#### 3.3.2. `device_mech-ring` — механические часы с боем

**Каталог:** `src/device_mech-ring/` · **Класс:** `CLASS_DEVICE_RINGMECH` · **Объект:** `device_mech_ring`
**`[registry]`:** `object=device_mech_ring`, `define=DEVICE_RINGMECH`, `web=1`, `loop=0`.
**Env:** `esp32_clock-mech_ring` (extends `env:esp32`); `src_filter` включает `module_udp`, `module_otaclient`, `device_clock-mech`, `module_ds3231`, `device_mech-ring`.

**Назначение:** бой часов (молоток/звонковое колесо): парковка по метке датчика, калибровка по шагам, поворот N полных оборотов с настраиваемыми паузами, а в начале нового часа (в окне `time_begin..time_end`) — отбивание соответствующего числа ударов.

**Функциональные требования:**
- FR-RINGMECH-1: Два режима `enable_status`; парковка (`cmdHome`), подсчёт оборотов (`cmdCount`), поворот (`cmdTurn`).
- FR-RINGMECH-2: Отбивание часа: число ударов = текущий час, только в окне `time_begin..time_end`.
- FR-RINGMECH-3: Паузы боя `ringPauseOne`, `ringPauseTwo`; начальное положение `firstPosition`.
- FR-RINGMECH-4: Источник времени `timeSource` (`ds3231`/`ntp`), период `pollInterval`, лимит `errorLimitSteps`.
- FR-RINGMECH-5: Датчик `SENS=32` (`INPUT_PULLUP`, срабатывание по LOW), подсветка `SENS_LED=27`, STEP=17, EN=16.
- FR-RINGMECH-6: Требует наличия `device_clock-mech` в сборке.
- FR-RINGMECH-7: Сохранение конфига — web `handleSave` и терминал `r-save`; bus-функции `save` нет.

**Веб-маршруты:** `POST /ring-mech/save`, `GET /ring-mech/info`; GET: `/ring-mech/turn|en|sens|home|count|status|reset|ver`.
**Веб-файлы:** `ring-mech.html`, `_menu.html`.
**Конфиг:** `/config_ring-mech.json`: `enable_status`, `stepsPerRevolution`, `pollInterval`, `errorLimitSteps`, `ringPauseOne`, `ringPauseTwo`, `firstPosition`, `time_begin`, `time_end`, `timeSource`.
**Зависимости модулей:** `module_udp`, `module_otaclient`, `module_ds3231`, `device_clock-mech`.

---

#### 3.3.3. `device_electronica7_rgb` — часы «Электроника-7» RGB

**Каталог:** `src/device_electronica7_rgb/` · **Класс:** `CLASS_DEVICE_E7RGB` · **Объект:** `device_electronica7_rgb`
**`[registry]`:** `object=device_electronica7_rgb`, `define=DEVICE_E7RGB`, `namespace=e7`, `web=1`, `loop=0`, `res=1`, `prio=70`.
**Env:** `esp32_electronica7_rgb` и `esp32_electronica7_rgb_macros` (с `module_macros` и таблицей `partitions_esp32_macro.csv`).

**Назначение:** вывод HH:MM (или ручного 4-символьного текста) на матрице WS2812 7×16 со шрифтами из ФС; цветовые эффекты, 12 типов переходных эффектов при смене времени, эффект «дождя»/«оседания» до синхронизации NTP; режимы шины off/auto/macro.

**Функциональные требования:**
- FR-E7RGB-1: Отображать время HH:MM или `manualText`; шрифт из `fontFile` (по умолчанию `/e7fonts/digital7.fnt`), кэшировать путь+размер.
- FR-E7RGB-2: Эффекты `effect` (моно, радуга, градиент статичный/динамичный, цикл цвета) с `effectDir`, `animSpeed`, `palette[8]`, `colorsCount`, `cycleMode`.
- FR-E7RGB-3: Переходные эффекты `timeFx` (12 типов), `timeFxFreq`, `timeFxDur`, `timeFxBg`.
- FR-E7RGB-4: Вступительный «дождь»: `rainEnabled`, `rainDurMin`, `rainIntensity`, `rainSettleDur`.
- FR-E7RGB-5: Публичные сеттеры (`setEffect`, `setBrightness`, `setSpeed`, `setDigitsColor`, `setManualText`, `setBusMode`) применяют изменения в RAM/на экране, но **не сохраняют** конфиг; сигналят на шину; планируют отложенное применение.
- FR-E7RGB-6: `saveNow()` и web `handleSave` сохраняют конфиг; bus-функция `e7.save` ставит отложенное сохранение.
- FR-E7RGB-7: Режимы шины `off/auto/macro` (`busMode`); bus-запись (кроме `mode`) разрешена только в `macro`, иначе `BUS_ERR_DENIED`.
- FR-E7RGB-8: Конфиг `/config_e7rgb.json`: `busMode`, `mode`, `dataPin` (16), `brightness` (25), `effect`, `effectDir`, `digitsColor`, `digitsColor2`, `animSpeed`, `colorsCount`, `cycleMode`, `palette[8]`, `origin`, `direction`, `layout`, `timeFx`, `timeFxFreq`, `timeFxDur`, `timeFxBg`, `rainEnabled`, `rainDurMin`, `rainIntensity`, `rainSettleDur`, `fontFile`, `manualText`.
- FR-E7RGB-9: Аппаратно 112 светодиодов WS2812 (7×16) через NeoPixelBus; RMT при `-D E7_USE_RMT=1`, иначе I2S0; `dataPin=-1` — выключено.

**Ресурсы шины (namespace `e7`):**
- состояния: `e7.mode` (ENUM off/auto/macro), `e7.effect` (ENUM), `e7.brightness` (I32), `e7.speed` (I32), `e7.color` (I32), `e7.text` (STR);
- функции: `e7.mode`, `e7.effect`, `e7.brightness`, `e7.speed`, `e7.color`, `e7.text`, `e7.save`.

**Веб-маршруты:** `GET /e7rgb/save`, `/e7rgb/info`, `/e7rgb/fonts`, `/e7rgb/ver`.
**Веб-файлы:** `electronica7-rgb.html`, `_menu.html`, `config_e7rgb.json`, `e7fonts/digital7.fnt`, макросы `web/macros/*.lua`.
**Зависимости модулей:** `module_udp`, `module_otaclient`, (`module_macros` — только в macros-env).

---

### 3.4. Реестр компонентов (сводно)

| Компонент | Тип | Класс | Объект | Namespace | web | loop | res | prio |
|-----------|-----|-------|--------|-----------|-----|------|-----|------|
| core_json | ядро | `CLASS_CORE_JSON` | `core_json` | — | 1 | 0 | 0 | — |
| core_sys | ядро | `CLASS_CORE_SYS` | `core_sys` | `system` | 1 | 0 | 1 | 85 |
| core_state | ядро | `CLASS_CORE_STATE` | `core_state` | `system` | 1 | 1 | 1 | 100 |
| core_task | ядро | `CLASS_CORE_TASK` | `core_task` | `task` | 0 | 1 | 0 | 90 |
| core_wifi | ядро | `CLASS_CORE_WIFI` | `core_wifi` | `wifi` | 1 | 0 | 1 | 80 |
| core_ntp | ядро | `CLASS_CORE_NTP` | `core_ntp` | `time` | 1 | 0 | 1 | 70 |
| core_ota | ядро | `CLASS_CORE_OTA` | `core_ota` | `ota` | 1 | 1 | 1 | 75 |
| core_led | ядро | — | — | — | 0 | 0 | 0 | — |
| core_terminal | ядро | — | `term` | — | 0 | 0 | 0 | — |
| core_web | ядро | `AsyncFSWebServer` | `ESPHTTPServer` | — | — | — | — | — |
| module_prog | модуль | `Class_ProgBase` | — | — | 0 | 0 | 0 | — |
| submodule_isp | субмодуль | `Class_SubIsp` | `progIsp` | — | 1 | 0 | 0 | — |
| submodule_swd | субмодуль | `Class_SubSwd` | `progSwd` | — | 1 | 0 | 0 | — |
| module_udp | модуль | `CLASS_MODULE_UDPBROADCAST` | `module_udp` | — | 1 | 0 | 0 | — |
| module_editor | модуль | `CLASS_MODULE_EDITOR` | `module_editor` | — | 1 | 0 | 0 | — |
| module_ds3231 | модуль | `CLASS_MODULE_DS3231` | `module_ds3231` | — | 1 | 0 | 0 | — |
| module_gpio | модуль | `CLASS_MODULE_GPIO` | `module_gpio` | — | 1 | 0 | 0 | — |
| module_lcd-i2c | модуль | `CLASS_MODULE_I2C_LCD` | `module_lcd_i2c` | — | 1 | 0 | 0 | — |
| module_macros | модуль | `CLASS_MODULE_MACROS` | `module_macros` | `macros` | 1 | 0 | 1 | 60 |
| module_otaclient | модуль | `CLASS_MODULE_OTACLIENT` | `module_otaclient` | (`ota`) | 1 | 1 | 1 | — |
| module_rgb | модуль | `CLASS_MODULE_RGB` | `module_rgb` | — | 1 | 0 | 0 | — |
| module_i2c-mapper | модуль | `CLASS_MODULE_I2C_MAPPER` | `module_i2c_mapper` | — | 1 | 0 | 0 | — |
| module_template | модуль | `CLASS_MODULE_TEMPLATE` | `module_template` | — | 1 | 0 | 0 | — |
| device_clock-mech | устройство | `CLASS_DEVICE_CLOCKMECH` | `device_clock_mech` | — | 1 | 0 | 0 | — |
| device_mech-ring | устройство | `CLASS_DEVICE_RINGMECH` | `device_mech_ring` | — | 1 | 0 | 0 | — |
| device_electronica7_rgb | устройство | `CLASS_DEVICE_E7RGB` | `device_electronica7_rgb` | `e7` | 1 | 0 | 1 | 70 |

---

## 4. Нефункциональные требования

### 4.1. Производительность

- NFR-PERF-1: Статические запросы веб-интерфейса обслуживаются асинхронно, без блокировки главного цикла.
- NFR-PERF-2: Периодические задачи (анимация, опрос датчиков, макросы) не должны блокировать `loop()`; разбор Lua-сценариев ограничен 2 за тик.
- NFR-PERF-3: Дамп больших JSON в порт отключён по умолчанию (`JSON_DUMP_LOG`).
- NFR-PERF-4: Тяжёлые операции в веб-обработчиках выполняются кооперативно через EERTOS, а не синхронно.

### 4.2. Надёжность

- NFR-REL-1: Сброс watchdog до и после `TaskManager()`.
- NFR-REL-2: Обновление прошивки/ФС не должно оставлять устройство неработоспособным при прерывании (два OTA-слота, корректное монтирование ФС).
- NFR-REL-3: При отказе Wi-Fi драйвера — бэкофф и контролируемый рестарт, а не цикл инициализации раз в секунду.
- NFR-REL-4: Длительные операции (OTA, FS-обновление, программирование, ручной тест) выполняются в режимах ядра с таймаутами; конфликты разрешаются по `prio` (при равенстве — FCFS).
- NFR-REL-5: Конфиги сохраняются merge-способом (в существующий документ), где это применимо.

### 4.3. Безопасность

- NFR-SEC-1: HTTP-аутентификация (Basic) через `secret.json`; рабочие маршруты модулей защищены `checkAuth()`.
- NFR-SEC-2: `secret.json` скрыт из браузера ФС при `HIDE_SECRET` (403); `config_sys.json` — при `HIDE_CONFIG`.
- NFR-SEC-3: Пароль администратора проверяется `isAdminPassValid`; имя/серийник хранятся с CRC-контролем.
- NFR-SEC-4: Восстановление доступа — только через публичные `/recover*` с секретным ответом.
- NFR-SEC-5: Строки, вставляемые в HTML/JSON, экранируются только `escapeHtml`/`escapeJson`.

### 4.4. Ресурсы и масштабируемость

- NFR-RES-1: Большие массивы (файлы/правила макросов) выносятся в heap, а не в `.bss`.
- NFR-RES-2: Бюджет heap макросов `MACRO_HEAP_BUDGET=100 КБ`, критический порог 95%.
- NFR-RES-3: Ограничения шины: ≤64 ресурсов, 16 namespace, 16 подписок, 6 async-вызовов; ≤12 задач `core_task`.
- NFR-RES-4: Набор компонентов ограничен свободным местом в flash/FS (таблицы разделов).

### 4.5. Сопровождаемость

- NFR-MAINT-1: Единый контракт компонента: `begin(ModContext&)`, `web_Init()`, `loop()` (опц.), `register_resources()` (опц.); прямой вызов модулей из `main.cpp`/`FSWebServerLib.cpp` запрещён — только через registry.
- NFR-MAINT-2: Слоистая структура: `<module>_types.h`, `<module>.h`, `<module>.cpp`, `<module>_engine.cpp`, `<module>_led.h`, `<module>_engine.h`.
- NFR-MAINT-3: Единый порядок функций в `.cpp`.
- NFR-MAINT-4: Stateless-хелперы — в `common/` или `<компонент>/common_module.*` (namespace `ns_<dirname>`); дублирование запрещено.
- NFR-MAINT-5: Стиль кода сохраняется; новые комментарии — на русском; `#elif`/`#else` в условных блоках запрещены.

---

## 5. Ограничения проектирования

### 5.1. Аппаратные и платформенные

- CON-1: Платформы — `espressif8266` (D1 mini; 4 МБ flash, 2 МБ FS, ldscript `eagle.flash.4m2m.ld`) и `espressif32` (WROOM/CAM/C3); framework — Arduino.
- CON-2: ФС — **LittleFS**. ESP32: `partitions_esp32.csv` (nvs/otadata/app0 0x130000/app1 0x130000/spiffs 0x18E000/sysid/nvs_key); macros-сборки: `partitions_esp32_macro.csv` (app0/app1 0x150000, spiffs 0x14E000).
- CON-3: Два OTA-слота — обязательное условие FOTA.
- CON-4: `module_macros` — только ESP32.
- CON-5: Платформенные различия — в `#if ESP32`/`#if ESP8266`; новые блоки — только `#if`/`#endif`.

### 5.2. Сборочные

- CON-6: `src/modules_registry.cpp/.h` — генерируемые, не в git; пересоздаются pre-скриптом `1_registry_pre_build.py` под текущий env.
- CON-7: Порядок скриптов: `1_registry_pre_build` → `2_version_builder` → `3_module_version_gen` → `4_fs_builder` → `5_set_fs_data_dir` (pre); `6_copy_fw` → `7_copy_fs` (post).
- CON-8: Компоненты — отдельные репозитории; для сборки устройства должны быть склонированы оно само и все модули из его `.ini`.
- CON-9: env-конфиги компонентов перечисляются в `extra_configs` явными путями (glob-маски `src/*/*.ini` и `src/*/*/*.ini` закомментированы); при смене `src_filter`/`build_flags` нужна перегенерация registry.
- CON-10: Инструменты — PlatformIO (VSCode); монитор 115200, скорость загрузки 921600.
- CON-11: Выбор ISP/SWD — через `src_filter` (подключается один `submodule_*`), а не через `#if PROGTYPE_*`.

### 5.3. Организационные

- CON-12: Внешние зависимости ограничены `lib_deps`: ArduinoJson 7, ESPAsyncWebServer-esphome, AsyncTCP-esphome (ESP32) / ESPAsyncTCP + ESPAsyncUDP (ESP8266), NeoPixelBus, LiquidCrystal_I2C.
- CON-13: Форк `ESPAsyncWebServer.h` в `src/` перекрывает версию из `libdeps` (include path `-Isrc`).
- CON-14: Лицензия — «as-is» (см. README).

---

## 6. Интерфейсы

### 6.1. Пользовательский интерфейс (Web UI)

- UI-1: Единый каркас: `page_head.html` (статическое меню Main/NTP/System/WiFi/OTA/Resources + правый столбец из `web/_menu.html` модулей) и `page_bottom.html`.
- UI-2: Общие JS: `GetJson.js` (`GetJson`, `GetText`, `ApplyJson`, `ParseCVT`, `ApplyCVT`) и `GetMarkup.js`; дублирование `applyCvtData` запрещено.
- UI-3: `GetMarkup.js` вставляет фрагмент на месте тега `<markup>` (`replaceWith`), **не** перезаписывая `document.body.innerHTML`.
- UI-4: Сохранение настроек — AJAX `fetch` без перезагрузки; ответ `text/plain "OK"`.
- UI-5: Периодический опрос времени/состояния — только там, где нужен (пример — `module_template`, `state.html`).
- UI-6: Страница ресурсов `state.html` (дерево ресурсов, вызовы функций) и `macros.html` (менеджер сценариев).
- UI-7: `update.html` (MD5-верификация `spark-md5.js`, прогресс) и `otaclient.html`.
- UI-8: Программатор: `project.html` (проект/чип), `prog.html` (загрузка/удаление/программирование), `avrcfg.html` (фьюзы AVR).

### 6.2. Программный интерфейс (HTTP API)

- API-1: Текстовые ответы — `text/plain "OK"` либо CVT-строки (`ключ|тип|значение`).
- API-2: JSON-ответы: `/state/catalog`, `/state/info`, `/state/modules`, `/state/module_mode`, `/i2cmapper/scan`, `/otaclient/teststatus`, `/editor/list`, `/macros/list`, `/macros/resources`, `/all`, `/_version_fs.json`.
- API-3: Все рабочие маршруты компонентов защищены `checkAuth`; публичны только вход/восстановление и captive-portal-эндпоинты.
- API-4: Ресурсная шина: `GET /state/set?name=<ns.field>&value=<v>`, `GET /state/call?name=<ns.func>&args=...`, `GET /state/info?name=...`. Типы: BOOL/I32/F32 (3 знака)/STR/TIME/ENUM.
- API-5: Async-функции: `call` возвращает код запуска, результат — через callback/событие; таймаут по умолчанию 10 с.
- API-6: Софт-интерфейс компонента: `begin(ModContext&)`, `web_Init()`, `loop()`, `register_resources()`; `ModContext = { fs, hostname, password }`.
- API-7: Планировщик: `SetTask`, `SetTimerTask`, `DelTimerTask`; `every`/`after`/`cancel` (`core_task`).
- API-8: Терминальные команды (см. 6.3) — публичный отладочный интерфейс.

### 6.3. Последовательный терминал (115200)

| Команда | Назначение | Доступность |
|---------|-----------|-------------|
| `help` | список команд | всегда |
| `?` | информация о системе (about) | всегда |
| `reset` | перезагрузка | всегда |
| `echo` | эхо-тест | всегда |
| `id` | показать/установить имя и серийник | всегда |
| `1` | диагностика | всегда |
| `led` | тест светодиода | всегда |
| `udpp` / `udpc` / `udps` | отладка UDP | только `MODULE_UDP` |
| `macro <sub>` | управление макросами | только `MODULE_MACROS` |
| `ds-alarm` / `ds-sqw` / `ds-sqr` | отладка DS3231 | только `MODULE_DS3231` |
| `i2c-scan` | сканер I2C | только `MODULE_I2C_MAPPER` |
| `c-*` / `r-*` | отладка механических часов | только соответствующие `device_*` |
| `flash`/`flash2`/`stm32`/`swdf`/`avr` | отладка программатора | закомментированы, неактивны |

### 6.4. Аппаратные интерфейсы

| Интерфейс | Назначение | Выводы по умолчанию | Компонент |
|-----------|-----------|---------------------|-----------|
| AVR ISP | MISO/MOSI/SCK/RST | 19 / 23 / 18 / 5 | `submodule_isp` |
| STM32 SWD | CLK/DATA | 21 / 19 | `submodule_swd` |
| I2C (DS3231) | SDA/SCL | ESP32 21/22, ESP8266 4/5 | `module_ds3231` |
| I2C (LCD) | SDA/SCL | ESP32 21/22, ESP8266 4/5 | `module_lcd-i2c` |
| I2C (сканер) | SDA/SCL | ESP32 21/22, ESP8266 4/5 | `module_i2c-mapper` |
| WS2812 (лента) | DATA | из `config_rgb.json` | `module_rgb` |
| WS2812 (матрица) | DATA | 16 | `device_electronica7_rgb` |
| DS3231 SQW/INT# | прерывание | 13 | `module_ds3231`, `device_clock-mech`, `device_mech-ring` |
| Шаговый драйвер | STEP/DIR/EN | 12 / 33 / 14 | `device_clock-mech` |
| Датчики часов | SENS_MIN/SENS_HOUR | 26 / 25 | `device_clock-mech` |
| Подсветка датчиков | SENS_LED | 27 | `device_clock-mech`, `device_mech-ring` |
| Бой часов | STEP/EN/SENS | 17 / 16 / 32 | `device_mech-ring` |
| Статусный светодиод | CONNECTION_LED | 2 (ESP8266/ESP32), 4 (cam), `-1` = выкл | `core_led` |
| Кнопка AP | AP_ENABLE_BUTTON | `-1` = выкл | `core_wifi` |
| GPIO-демо (шаблон) | GPIO1/GPIO2 | ESP32 32/33, ESP8266 16/14 | `module_template` |

---

## 7. Требования к данным

### 7.1. Файлы конфигурации на FS

| Файл | Владелец | Основные поля |
|------|----------|---------------|
| `config_sys.json` | core_sys | `deviceName`, `deviceSerial` |
| `secret.json` | core_sys | `auth`, `user`, `pass`, `secq`, `seca` |
| `_version_fs.json` | 4_fs_builder (генерация) | версия FW, список модулей, статистика ФС, git-инфо, build_info |
| `config_wifi0..3.json` | core_wifi | `ssid`, `pass`, `dhcp`, `ip[4]`, `netmask[4]`, `gateway[4]`, `dns[4]` |
| `config_wifi.json` | core_wifi | `scantime`, `aptime` |
| `config_ntp.json` | core_ntp | `ntp0`, `ntp1`, `ntp2`, `NTPperiod`, `timeZone`, `daylight` |
| `config_state.json` | core_state | `test_timeout_s`, `op_timeout_s` |
| `config_prog.json` | module_prog | `project`, `chip_name` |
| `prog_filelist.json` | module_prog | `filename`, `upload_date`, `md5`, `prog_date`, `prog_status`, `prog_time`, `prog_error`, `prog_error_stage`, `prog_error_percent`, `prog_speed` |
| `avrisp_cfg.json` | submodule_isp | `chips[]`: `signature`, `name`, `flash_size`, `page_size` |
| `swd_cfg.json` | submodule_swd | `chips[]`: `idcode`, `name`, `family`, `flash_size`, `flash_start`, `page_size`, `word_size`, `csw_value` |
| `config_udp.json` | module_udp | `udpPortTx`, `udpPortRx`, `udpTimeOut`, `udpkeyword`, `udpPowerOn`, `udpResponse`, `udpMissLimit` |
| `config_ds3231.json` | module_ds3231 | `addr`, `autoPoll`, `pollInterval`, `sqw*`, `ctrl*` |
| `config_lcd-i2c.json` | module_lcd-i2c | `i2cAddr`, `cols`, `rows`, `backlight`, `display_lines[]` |
| `config_macros.json` | module_macros | `enabled`, `files[]` (`name`, `prio`, `run`, `created`) |
| `config_otaclient.json` | module_otaclient | `timeOut`, `powerOn`, `serverAddress`, `serverPort`, `manifestPath` |
| `config_rgb.json` | module_rgb | `dataPin`, `numLeds`, `brightness`, `mode`, `effectSpeed`, `solidColor`, `grad*Color`, `individualColors[]`, `eqBands`, `eqLedsPerBand` |
| `config_template.json` | module_template | `gpio1State`, `gpio2State`, `blinkInterval`, `demoSampleText`, `demoArray[3]` |
| `config_clock-mech.json` | device_clock-mech | `enable_status`, `timeSource`, `stepsPerRevolution`, `pollInterval`, `errorLimitSteps`, `sensorLedEnabled` |
| `config_ring-mech.json` | device_mech-ring | `enable_status`, `stepsPerRevolution`, `pollInterval`, `errorLimitSteps`, `ringPauseOne`, `ringPauseTwo`, `firstPosition`, `time_begin`, `time_end`, `timeSource` |
| `config_e7rgb.json` | device_electronica7_rgb | `busMode`, `mode`, `dataPin`, `brightness`, `effect`, `effectDir`, `digitsColor*`, `animSpeed`, `colorsCount`, `cycleMode`, `palette[8]`, `origin`, `direction`, `layout`, `timeFx*`, `rain*`, `fontFile`, `manualText` |

### 7.2. Данные пользователя на FS

- Файлы прошивок в корне FS (`firmware.bin`, `littlefs.bin`, пользовательские `.bin`/`.hex`) и `prog_filelist.json`.
- Сценарии макросов `/macros/*.lua`; шрифты `/e7fonts/*.fnt`.
- Статические веб-файлы, собранные `4_fs_builder.py` в `web_debug/web_<env>/`.

### 7.3. Форматы прошивок

- **Intel HEX** — построчный разбор с CRC, контролем адресов/переполнения; в AVR-пути загружается целиком, в SWD-пути — потоковая запись страницами.
- **BIN** — потоковое чтение постранично (`.bin`/`.binary`).
- Имена OTA-файлов: `<ENV>-FIRMWARE-<VERSION>.bin`, `<ENV>-FILESYS-<VERSION>.bin` (шаблоны `firmware.bin`/`littlefs.bin`, разделители `-FIRMWARE-`/`-FILESYS-`).

### 7.4. Версионирование

- Формат: `MAJOR.MINOR.DATE.BUILD`, например `0.082.20260912_2233.1530`.
- Счётчики: `version_counter.txt` (MAJOR/MINOR), `build_counter.txt` (BUILD); MINOR инкрементируется при смене git-commit проекта, BUILD — при каждой сборке.
- Генерируемые (не в git): `version.h`, `.version_hashes`, `version_counter.txt`, `build_counter.txt`, `modules_registry.*`; заголовки `*_version.h` отслеживаются в git.
- Версии компонентов независимы; инкрементируются при изменении хеша содержимого (последний коммит, затрагивающий папку).

### 7.5. Хранение и доступ

- Все конфиги — в LittleFS в корне; доступ только через `core_json`.
- `secret.json` вне доступа браузера ФС при `HIDE_SECRET`; в репозиторий не коммитится.
- Сохранение конфигов — только по явному действию (web/терминал/bus `save`), не из периодических задач.

---

## 8. Критерии приёмки

### 8.1. Функциональные (сквозные сценарии)

- AC-1: Устройство поднимает AP при отсутствии сохранённой сети; captive portal открывается; после ввода данных подключается к сети.
- AC-2: Веб-интерфейс открывается по hostname/mDNS и IP; меню содержит пункты всех включённых модулей; страницы отображаются без ошибок в консоли браузера.
- AC-3: Авторизация обязательна для рабочих маршрутов; при `HIDE_SECRET` `/secret.json` возвращает 403.
- AC-4: NTP-синхронизация выполняется после подключения; ресурсы `time.now/valid/source` корректны.
- AC-5: Веб-обновление: файл с корректным именем принимается, MD5 подтверждается, устройство перезагружается с новой версией; при несоответствии версии — предупреждение; запись `littlefs.bin` монтирует новую ФС.
- AC-6: `module_otaclient` при доступном сервере обнаруживает новую версию, скачивает и прошивает; при недоступном — сообщает об ошибке без блокировки.
- AC-7: AVR ISP: чтение сигнатуры/фьюзов, прошивка HEX/BIN, верификация, прогресс; при неверном чипе — ошибка без повреждения конфига.
- AC-8: STM32 SWD: чтение IDCODE, определение чипа, mass-erase + прошивка + reset; алгоритмы F1/F4 выбираются верно.
- AC-9: Редактор ФС: список, создание, редактирование, сохранение, удаление, загрузка (до 1 МБ), занятость ФС.
- AC-10: `/state/catalog` содержит ресурсы всех включённых компонентов; `/state/set` и `/state/call` работают; ошибки соответствуют кодам `-1..-13`.
- AC-11: Макросы: сценарий запускается, cron/cond/on-триггеры срабатывают, `set`/`call`/`calls`/`run` выполняются, stop останавливает файл; при переполнении heap — предупреждение без падения.
- AC-12: `device_electronica7_rgb`: `call("e7.save")` и веб-Save персистят конфиг, а `set("e7.effect", ...)` — нет; режимы `off/auto/macro` соблюдаются.
- AC-13: `device_clock-mech`/`device_mech-ring`: калибровка по датчикам, установка/отслеживание времени; сохранение только по явному действию.
- AC-14: UDP: broadcast-обнаружение и ответ на ключевое слово, статус сервера, фоллбэк; `begin()` вызывается при подключении Wi-Fi.
- AC-15: `module_template`/`module_gpio` собираются и открывают свои страницы.

### 8.2. Нефункциональные

- AC-16: Прошивка для каждого целевого env собирается без блокирующих ошибок (`pio run -e <env>`).
- AC-17: Артефакты появляются в `proj_fwbins/<ENV>-FIRMWARE-<VERSION>.bin` и `<ENV>-FILESYS-<VERSION>.bin`.
- AC-18: `_version_fs.json` в корне ФС содержит версию, список компонентов и статистику.
- AC-19: `modules_registry.cpp` содержит вызовы только для компонентов выбранного env (без `#if`) и перегенерируется при смене env.
- AC-20: Устройство не уходит в циклический ресет; при неисправном Wi-Fi — предсказуемое поведение без watchdog-сбоев.
- AC-21: Heap-бюджет макросов соблюдается (`/macros/heap`); открытие `macros.html` не вызывает `abort()`.
- AC-22: Ни один `module_*`/`device_*` не компилируется без ядра; сборка ядра без внешних компонентов (`TestSolo32`/`TestSolo8266`) работает.

### 8.3. Открытые требования (не реализовано; учесть при приёмке)

- OPEN-1: bus-функция `<namespace>.save` реализована только у `device_electronica7_rgb` (`e7.save`); `core_*.save` и `save` у остальных модулей отсутствуют. Масштабировать правило apply/save на все компоненты с resource-bus.
- OPEN-2: `handleSave` сохраняет конфиг целиком; нужно сохранять только реально изменённые поля.
- OPEN-3: Guard от цикла `esp_wifi_init` (`WIFI_SCAN_FAILED` → бэкофф → рестарт) не проверен инъекцией реального отказа драйвера; в небезопасных режимах рестарт откладывается бессрочно.
- OPEN-4: `module_macros`: после сдвига массива `_files` указатели upvalue активных интерпретаторов устаревают — нужно пересоздание интерпретаторов или стабильные id/указатели.
- OPEN-5: UI `macros.html` (сплиттер, чекбоксы `SEL`/`CUR_EDIT`, `indeterminate`, кнопки start/stop) в браузере не проверялся.
- OPEN-6: `desc` остановленного макрос-файла остаётся в таблице до перезагрузки (косметика).
- OPEN-7: `module_rgb`/`module_gpio`/`module_editor` не имеют resource-bus и недоступны из макросов.
- OPEN-8: `targets/targets_user.ini` и `src/module_rgb/esp32_macrotest.ini` отсутствуют, но упомянуты в `extra_configs`.
- OPEN-9: Отладочные команды программатора в терминале закомментированы.
- OPEN-10: README.md частично устарел (не отражает `core_state`/`core_task`/`module_macros`).

---

## Приложение A. Определения `define` и флагов сборки

| Флаг | Значение |
|------|----------|
| `USE_LITTLEFS` | Включить LittleFS (задаётся в env ESP8266/ESP32) |
| `HIDE_SECRET` | Скрыть `secret.json` из браузера ФС (403) |
| `HIDE_CONFIG` | Скрыть `config_sys.json` из браузера ФС (403) |
| `RELEASE` | Превратить все отладочные макросы в пустышки |
| `CONNECTION_LED` | GPIO статусного светодиода: в env — 2 (ESP8266/ESP32), 4 (esp32cam); fallback в `main.h` — `-1` (выключено) |
| `AP_ENABLE_BUTTON` | GPIO кнопки принудительного AP (`-1` — выключено) |
| `PROGTYPE_ISP` | Сборка с AVR ISP (определяется env/`src_filter`) |
| `PROGTYPE_SWD` | Сборка с STM32 SWD |
| `PIN_MISO` / `PIN_MOSI` / `PIN_SCK` / `PIN_RST` | Выводы ISP |
| `SWDPIN_CLK` / `SWDPIN_DATA` | Выводы SWD |
| `MODULE_UDP` | Модуль UDP broadcast |
| `MODULE_EDITOR` | Модуль редактора ФС |
| `MODULE_GPIO` | Модуль GPIO |
| `MODULE_LCD_I2C` | Модуль LCD I2C |
| `MODULE_DS3231` | Модуль DS3231 |
| `MODULE_MACROS` | Модуль макросов (ESP32) |
| `MODULE_OTACLIENT` | OTA-клиент (внедряется в core-группы) |
| `MODULE_RGB` | Модуль RGB |
| `MODULE_I2C_MAPPER` | Модуль сканера I2C |
| `MODULE_TEMPLATE` | Шаблон модуля |
| `DEVICE_CLOCKMECH` / `DEVICE_RINGMECH` / `DEVICE_E7RGB` | Устройства |
| `DS3231_SQW_PIN` | Пин SQW/INT# DS3231 (по умолчанию 13) |
| `E7_USE_RMT` | Использовать RMT-метод NeoPixelBus в `device_electronica7_rgb` |
| `JSON_DUMP_LOG` | Включить дамп JSON в лог (по умолчанию выключен) |
| `DEBUG_*` | Отладочные макросы по компонентам (`DEBUGSYS`, `DEBUG_WIFI`, `DEBUGNTP`, `DEBUGOTA`, `DEBUGJSON`, `DEBUGLOGLED`, `DEBUGSTATE`, `DEBUGTASK`, `DEBUG_PROG`, `DEBUG_ISP`, `DEBUG_SWD`, `DEBUG_UDP`, `DEBUGEDITOR`, `DEBUGDS3231`, `DEBUGLCD`, `DEBUGMACROS`, `DEBUGOTACLIENT`, `DEBUG_RGB`, `DEBUGI2CMAPPER`, `DEBUGTEMPLATE`, `DEBUG_GPIO`, `DEBUG_CLOCKMECH`, `DEBUG_RINGMECH`, `DEBUG_E7RGB`) |

---

**Конец документа TRS.md.**

