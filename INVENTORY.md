# Реестр компонентов avr-fota

Сводный реестр всех компонентов проекта: `core_*`, `module_*`, `submodule_*`, `device_*`.

> **Опциональные компоненты.** Все `module_*`, `submodule_*` и `device_*` — опциональны,
> подключаются через `src_filter` + `build_flags` и **неработоспособны без ядра** (см.
> `TRS.md` §2.1). Ядра `core_*` присутствуют в любой сборке.

## Как пользоваться

| Что нужно | Где искать |
|-----------|------------|
| Требования по ядру (`core_*`, EERTOS, `common/`, НФТ, ограничения, интерфейсы ядра) | `TRS.md` |
| Требования, интерфейсы и конфиги конкретного модуля/устройства | `src/<component>/AGENTS.md` |
| Конвенции слоистой структуры и план выделения слоёв | `LAYERS.md` |
| Как собирается прошивка (registry, pre/post-скрипты, FS) | `BUILD.md` |
| Методология и план тестирования | `TESTING.md` |
| Определения `define`/флагов сборки | `TRS.md`, Приложение A |

## Реестр

Колонка «Документация» — путь к `AGENTS.md` от корня либо раздел `TRS.md`. Колонка «Состав»
заполнена только для составного репозитория `module_program`.

| Компонент | Тип | Папка | Состав | Документация | Флаг активации | Платформа | Зависит от модулей | Namespace |
|---|---|---|---|---|---|---|---|---|
| core_web | ядро | `src/core_web/` | — | TRS §3.1.1 | не требуется (вне registry) | обе | — | — |
| core_sys | ядро | `src/core_sys/` | — | TRS §3.1.2 | CORE_SYS (всегда) | обе | — | system |
| core_wifi | ядро | `src/core_wifi/` | — | TRS §3.1.3 | CORE_WIFI | обе | вызывает `module_udp.begin()` | wifi |
| core_ntp | ядро | `src/core_ntp/` | — | TRS §3.1.4 | CORE_NTP | обе | — | time |
| core_ota | ядро | `src/core_ota/` | — | TRS §3.1.5 | CORE_OTA | обе | — | ota |
| core_json | ядро | `src/core_json/` | — | TRS §3.1.6 | всегда | обе | — | — |
| core_led | ядро | `src/core_led/` | — | TRS §3.1.7 | всегда (`ledInit` вручную) | обе | — | — |
| core_terminal | ядро | `src/core_terminal/` | — | TRS §3.1.8 | всегда | обе | опц. `module_udp`/`module_macros`/`module_ds3231`/`module_i2c-mapper` | — |
| core_state | ядро | `src/core_state/` | — | TRS §3.1.9 | всегда | обе | — | system |
| core_task | ядро | `src/core_task/` | — | TRS §3.1.10 | всегда | обе | — | task |
| EERTOS | ядро (планировщик) | `src/core_sys/eertos.*` | — | TRS §3.1.11 | всегда | обе | — | — |
| common | ядро (утилиты) | `src/common/` | — | TRS §3.1.12 | всегда | обе | — | — |
| module_program | модуль | `src/module_program/` | `module_prog`, `submodule_isp`, `submodule_swd` | `src/module_program/AGENTS.md` | PROGTYPE_ISP / PROGTYPE_SWD | ESP32 (env) | — | — |
| module_udp | модуль | `src/module_udp/` | — | `src/module_udp/AGENTS.md` | MODULE_UDP | обе | — | — |
| module_editor | модуль | `src/module_editor/` | — | `src/module_editor/AGENTS.md` | MODULE_EDITOR | обе | — | — |
| module_ds3231 | модуль | `src/module_ds3231/` | — | `src/module_ds3231/AGENTS.md` | MODULE_DS3231 | обе (SQW — ESP32) | — | — |
| module_gpio | модуль | `src/module_gpio/` | — | `src/module_gpio/AGENTS.md` | MODULE_GPIO | обе | — | — |
| module_lcd-i2c | модуль | `src/module_lcd-i2c/` | — | `src/module_lcd-i2c/AGENTS.md` | MODULE_LCD_I2C | обе | — | — |
| module_macros | модуль | `src/module_macros/` | — | `src/module_macros/AGENTS.md` | MODULE_MACROS | только ESP32 | — | macros |
| module_otaclient | модуль | `src/module_otaclient/` | — | `src/module_otaclient/AGENTS.md` | MODULE_OTACLIENT | обе | `core_ota` | (ota) |
| module_rgb | модуль | `src/module_rgb/` | — | `src/module_rgb/AGENTS.md` | MODULE_RGB | обе | — | — |
| module_i2c-mapper | модуль | `src/module_i2c-mapper/` | — | `src/module_i2c-mapper/AGENTS.md` | MODULE_I2C_MAPPER | обе (env — esp32) | — | — |
| module_template | модуль | `src/module_template/` | — | `src/module_template/AGENTS.md` | MODULE_TEMPLATE | обе | — | — |
| device_clock-mech | устройство | `src/device_clock-mech/` | — | `src/device_clock-mech/AGENTS.md` | DEVICE_CLOCKMECH | ESP32 | `module_udp`, `module_otaclient`, `module_ds3231` | — |
| device_mech-ring | устройство | `src/device_mech-ring/` | — | `src/device_mech-ring/AGENTS.md` | DEVICE_RINGMECH | ESP32 | + `device_clock-mech` | — |
| device_electronica7_rgb | устройство | `src/device_electronica7_rgb/` | — | `src/device_electronica7_rgb/AGENTS.md` | DEVICE_E7RGB | ESP32 | `module_udp`, `module_otaclient`, (`module_macros`) | e7 |

## Репозитории

Мульти-репозиторная компоновка: ядро `avr-fota` — один репозиторий; каждый `module_*` и
`device_*` — отдельный репозиторий под аккаунтом `Arcanum753` (ветка `main`), клонируемый в
`src/<имя>`. `module_program` — **один** репозиторий, включающий `module_prog/`,
`submodule_isp/`, `submodule_swd/`. `module_template` — часть ядра (не клон).

Для сборки устройства необходимо клонировать само устройство и все указанные в его `.ini`
модули. Ядро игнорирует внешние папки через `.gitignore` (`/src/module_*/`, `/src/device_*/`,
`!/src/module_template/`).
