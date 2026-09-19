# module_template — шаблон нового модуля

> **Опциональный модуль.** Подключается через `src_filter` + `build_flags`.
> **Работоспособен только в составе сборки, содержащей ядро** (см. `../../TRS.md` §2.1).

Эталонный пример опционального модуля: два GPIO-выхода (мигание), конфиг с массивом,
AJAX-страница с использованием общих `GetJson.js`/`GetMarkup.js`. Служит образцом для
создания новых модулей — при разработке копировать эту папку и переименовывать.

- **Репозиторий:** входит в ядро (не клон)
- **Папка:** `src/module_template/`
- **Флаг активации:** `-D MODULE_TEMPLATE`
- **Registry:** `object=module_template`, `define=MODULE_TEMPLATE`, `web=1`, `loop=0` (без `namespace`/`res`/`prio`)
- **Только для платформы:** обе (ESP8266/ESP32)
- **Зависит от модулей:** —
- **Зависит от ядра:** `core_web`, `core_sys`, `core_json`

## Назначение

Эталонный пример опционального модуля — два GPIO-выхода (мигание), конфиг с массивом,
AJAX-страница с `GetJson.js`/`GetMarkup.js`.

## Функциональные требования

- FR-TEMPLATE-1: Образец структуры: `.h` + `.cpp` (шаблонный блок) + `_types.h` + `_engine.cpp` + `web/`.
- FR-TEMPLATE-2: Образец порядка функций: INCLUDES → объекты → `setFs` → `begin`/`begin(ctx)` → `web_Init` → веб-обработчики → конфиг → версии → логика.
- FR-TEMPLATE-3: Образец паттернов: AJAX-сохранение без перезагрузки, чтение/запись массивов JSON, отложенное сохранение.
- FR-TEMPLATE-4: Не включать в реальные сборки как функциональный модуль (только эталон).

## Аппаратные интерфейсы

| Интерфейс | Выводы по умолчанию | Примечание |
|-----------|---------------------|------------|
| GPIO-демо (шаблон) | ESP32 32/33, ESP8266 16/14 | GPIO1/GPIO2; демонстрационные выводы, в реальном модуле заменяются своей логикой |

> Управление GPIO через `pinMode`/`digitalWrite` — только пример. В новом модуле будет
> своя аппаратная логика.

## Веб-интерфейс

Маршруты: `POST /template/save`, `POST /template/save_demo`, `GET /template/info`,
`/template/time`, `/template/ver`.
Страницы `template.html`, `template2.html`, `_menu.html`; конфиг `web/config_template.json`.

## Конфигурация

`/config_template.json` — `gpio1State`, `gpio2State`, `blinkInterval`, `demoSampleText`,
`demoArray[3]`.

`demoArray` — только демонстрация паттерна `is<JsonArray>()`; в реальном модуле заменить
на свои поля или удалить.

## Слоистая структура

Из `../../LAYERS.md`: `module_template` — шаблон optional-модуля (эталон структуры);
demo-логика в `.cpp`; выделить `_types.h`, `_engine.cpp` (показать паттерн).

Ожидаемый набор файлов (см. `../../LAYERS.md`):

```
module_template_types.h   — define'ы и struct/enum
module_template.h         — класс CLASS_MODULE_TEMPLATE, debug-макрос, extern, Page_*
module_template.cpp       — шаблонный блок (begin, web_Init, обработчики, конфиг, версии)
module_template_engine.cpp — исполнительная логика
module_template.ini       — env-пример + секция [registry]
web/                      — template.html, template2.html, _menu.html, config_template.json
```

Пример секции `[registry]` (из `module_template.ini`):

```ini
[registry]
object = module_template
define = MODULE_TEMPLATE
web = 1
loop = 0
```

## Критерии приёмки

- AC-15: `module_template` и `module_gpio` собираются и открывают свои страницы (модуль `module_gpio` ссылается сюда, см. `../module_gpio/AGENTS.md`).

## Ссылки

- Ядро и конвенции: `../../TRS.md`
- Слоистая структура: `../../LAYERS.md`
- Общие утилиты: `../../TRS.md` §3.1.12 (`common/`)
- Сборка: `../../BUILD.md`
- Реестр компонентов: `../../INVENTORY.md`

> Если модуль читается вне дерева ядра (standalone), корневые документы доступны в
> репозитории ядра avr-fota.
