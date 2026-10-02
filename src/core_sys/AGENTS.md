# core_sys — ядро системы

> **Ядровый модуль.** Компилируется всегда. Владеет идентичностью устройства,
> `config_sys.json`, HTTP-аутентификацией/восстановлением, информацией о системе и
> **подсистемой виртуального времени**.

- **Файлы:** `core_sys.h`, `core_sys_types.h`, `core_sys.cpp` (шаблон), `core_sys_engine.cpp`
  (инфо/FS-версия), `core_sys_time.cpp` (виртуальное время), `common_module.*`,
  `ident_store.*`, `eertos.*`
- **Registry:** `object=core_sys`, `namespace=system` (для `system.*`), `res=1`

## Назначение

- Идентичность (имя/серийник в NVRAM), `config_sys.json`, `secret.json`.
- HTTP-аутентификация и восстановление (`/recover*`).
- Информация о системе (причина сброса, chipinfo, about), кэш версии FS.
- Хранилище EERTOS (`eertos.*`) — кооперативный планировщик.

## Виртуальное время системы

`core_sys` — **единственный владелец** времени и часового пояса. Потребители получают время
только через `core_sys.timeNow()` или ресурсы шины `time.*`. Прямые `now()`, `NTP.getTime()`,
`module_ds3231.getTime()` в потребителях запрещены.

Функциональные требования:

- **FR-SYS-TIME-1.** Реестр источников времени (до `CORE_SYS_TIME_MAX_SOURCES`), гибкий состав
  по сборке.
- **FR-SYS-TIME-2.** Выбор активного источника: `enabled` + валидный `get()`, максимальный `prio`,
  FCFS при равенстве.
- **FR-SYS-TIME-3.** Публикация ресурсов `time.*` в `core_state` (владелец namespace `time`).
- **FR-SYS-TIME-4.** Часовой пояс/DST — в `config_time.json`; применяются к NTP-источнику через
  `time.tz_changed`.
- **FR-SYS-TIME-5.** TZ/DST перенесены из `config_ntp.json` (однократная миграция legacy).
- **FR-SYS-TIME-6.** Обратная синхронизация подчинённых RTC (`syncIntervalS`).
- **FR-SYS-TIME-7.** Аварийный timestamp: при отсутствии валидных источников TimeLib продолжает
  идти от последнего валидного (+1/сек); `time.valid=false`.
- **FR-SYS-TIME-8.** Backward-jump: источник с временем в прошлом не выбирается (кроме явного
  `time.set` / `time.sync_from` / первого валидного после старта).
- **FR-SYS-TIME-9.** Форсированный опрос `time.sync_from` / `/time/sync`.
- **FR-SYS-TIME-10.** `time.save` / `/time/save` — явная персистенция конфига (правило apply/save).
- **FR-SYS-TIME-11.** Веб-страница `time.html` и маршруты `/time/*` под `checkAuth`.
- **FR-SYS-TIME-12.** Runtime-детект дубликата имени источника (`addTimeSource` → `false`,
  `DEBUGSYS("duplicate time source '%s'")`).

## Time Source Provider API

Любой модуль-источник предоставляет 3 статических колбэка и регистрируется через
`core_sys.addTimeSource()`.

```cpp
typedef bool        (*TimeGetFn)(time_t& out);   // true — время достоверно
typedef bool        (*TimeSetFn)(time_t in);     // nullptr — read-only
typedef const char* (*TimeStatusFn)();           // "" — ok, иначе причина

core_sys.addTimeSource("ntp", 100, ntpGetTime, nullptr, ntpStatus);
```

- Имя источника = имя модуля без префикса (`core_ntp` → `"ntp"`, `module_ds3231` → `"ds3231"`).
- **Порядок `status()` → `get()`**: `status()` вызывается первым, каждый такт, для всех
  `enabled`-источников. Если результат непустой — источник считается невалидным, `get()` **не**
  вызывается. `get()` вызывается только если `status()` пуст или `nullptr`.
- Контракт `status()`: **дешёвый** health-check (I2C-read ≤ 2 транзакции, без сети),
  идемпотентный, без побочных эффектов; возвращает статический литерал.
- `get(out)` не блокирует, без побочных эффектов (кроме допустимого чтения статуса);
  `get()` вызывается не чаще раза в секунду, **кроме** форсированного опроса.
- `set(in)` опционален; вызывается только при `time.set` / `time.sync_from` и обратной
  синхронизации.
- Регистрация происходит до первого тика `time.tick`.

**Форсированный опрос.** `time.sync_from` (bus) и `/time/sync` (web) вызывают `timeTick()`
немедленно, но **не сбрасывают период** автоматического тика: следующий плановый `time.tick`
придёт через остаток интервала 1000 мс от последнего планового срабатывания (EERTOS
`SetTimerTask` идемпотентен по указателю, `time.tick` уже стоит в очереди).

**Backward-jump.** При выборе нового активного: если время кандидата меньше системного более
чем на `CORE_SYS_TIME_BACKJUMP_MAX_S` (2) и системное время было валидно последние
`CORE_SYS_TIME_BACKJUMP_GRACE_S` (600) — кандидат отклоняется, `status()` показывает
`"time in past (use time.sync_from)"`. Откат разрешён через `time.set` / `time.sync_from` или
при первом валидном источнике после старта.

**Fallback.** При отсутствии валидных источников: `setTime(t)` в `timeTick` **не** вызывается,
если `timeStatus() == timeSet` (не конфликтуем с sync-provider NTPClientLib) — TimeLib тикает
сам. `setTime(t)` вызывается только при: (i) первом успехе после старта (`_timeLastValid == 0`),
(ii) смене активного (`_timeActiveIdx != <индекс активного>`, включая возврат из fallback
`255 → idx`).

**RTC-обратная синхронизация** — `set(now())` для источников с `set()` и `prio` ниже активного;
дефолт `syncIntervalS = 3600` (0 — выключено).

**Эталонные реализации:**
- read-only — `src/core_ntp/` (`../core_ntp/AGENTS.md`);
- с записью — `src/module_ds3231/` (`../module_ds3231/AGENTS.md`).

### Как добавить новый источник (чеклист)

1. Модуль собран в env (`src_filter` включает его папку).
2. В `<module>.cpp` — статические колбэки `<name>GetTime` / `<name>SetTime` (или пропустить) /
   `<name>Status` (или пропустить).
3. В `<module>.h` — `void registerTimeSource();`.
4. `<name>` = имя модуля без префикса; дефолтный `prio`: NTP 100, DS3231 50, GPS 200, HTTP 30.
5. В `<module>.ini` → `[registry]` → `time_source = 1`.
6. Критерий валидности `get()`: NTP `SyncStatus() && (now-lastSync)<STALE`; DS3231
   `isConnected() && !OSF && t>=2020-01-01`; GPS `fix && satellites>=N`; HTTP успешный ответ.
7. При поддержке записи — `set()`; иначе `nullptr`.
8. `registerTimeSource()` реализуется через `core_sys.addTimeSource(...)`.
9. Обновить `<module>/AGENTS.md`.

Генератор `python/module_registry_gen.py` сам вставляет `<module>.registerTimeSource();` в
`core_begin`/`modules_begin`/`dev_begin` сразу после `*_register_resources()`, до `begin()`
периферии.

## Bus-контракт (namespace `time`)

| Имя | Тип | RW | Смысл |
|---|---|---|---|
| `time.now` | TIME | ro | системное время (epoch) или fallback |
| `time.now_str` | STR | ro | `"YYYY-MM-DD HH:MM:SS"` |
| `time.hour` / `minute` / `second` | I32 | ro | компоненты (обновляются в `timeTick`) |
| `time.valid` | BOOL | ro | есть валидный источник |
| `time.source` | STR | ro | имя активного (`""` если нет) |
| `time.source_count` | I32 | ro | число источников |
| `time.sources` | STR | ro | CSV имён |
| `time.tz` | I32 | rw | часовой пояс, десятые доли часа (`30` = +03:00) |
| `time.dst` | BOOL | rw | летнее время |

События: `time.synced`, `time.lost`, `time.source_changed`, `time.tz_changed`,
`time.source_invalid` (STR `"<name>:<reason>"`). `source_changed` эмитится только при переходе
активного с одного имени на другое (в т.ч. первый выбор); при потере активного — не эмитится.
`time.sources`/`time.source_count` обновляются только в `addTimeSource` (через `timeEmitSources`).

Функции: `time.set` (TIME→), `time.sync_from` (STR→), `time.save` (→).

## Конфиг `/config_time.json`

```json
{ "timeZone": 30, "daylight": false, "syncIntervalS": 3600,
  "sources": { "ntp": { "prio": 100, "enabled": true } } }
```

- Файл создаётся только явным `time.save` / `/time/save` (правило apply/save). Если файла нет:
  legacy `timeZone`/`daylight` из `/config_ntp.json` (однократная миграция), иначе дефолты в
  памяти (`timeZone=0`, `daylight=false`, `syncIntervalS=3600`).
- `time.tz`/`time.dst` — rw: запись через шину применяется на ближайшем `timeTick`
  (реконсиляция шины и `_timeTzDec`/`_timeDst`).

## Веб-интерфейс

`time.html` (пункт меню — `STATIC_MENU_LINKS` в `python/gen_page_head.py`).

| Метод | URL | Назначение |
|---|---|---|
| GET | `/time/info` | CVT: `time_now`, `time_now_str`, `time_valid`, `time_source`, `time_sources`, `time_tz`, `time_dst`, `time_sync_interval` |
| GET | `/time/sources` | JSON-массив источников (`AsyncResponseStream`) |
| POST | `/time/save` | `tz`, `dst`, `syncIntervalS`, `<name>_prio`, `<name>_enabled` |
| POST | `/time/set` | `value=<epoch>` |
| POST | `/time/sync` | `source=<name>` |
| GET | `/time/ver` | версия |

Все под `checkAuth`.

## Слоистая структура

- `core_sys_types.h` — define'ы/типы (в т.ч. `strTimeSource`, `TimeGetFn/TimeSetFn/TimeStatusFn`).
- `core_sys.h` — класс `CLASS_CORE_SYS`.
- `core_sys.cpp` — шаблон: `begin`, `register_resources` (делегирует `registerTimeResources`),
  `web_Init`.
- `core_sys_engine.cpp` — информация о системе, FS-версия.
- `core_sys_time.cpp` — реестр источников, `timeTick`, конфиг, bus-функции, web-обработчики.
- `common_module.*` — `ns_core_sys`: `isAdminPassValid`, `identCrcSkip`, `timeSelectActive`,
  `timeCsvNames`, `timeParseTz`, `timeFormatNowStr`, `timeSelectBackJump`.

## Тестирование

- L1/L2 — `tests/core/core_sys/test/test_l2_core_sys.cpp` (единый TU), прелюдия
  `override_prelude_core_sys.h`, реальные `core_state`/`core_task`/`core_json`/`common/Time.cpp`.
- L4 — `tests/core/core_sys/http_api/test_time_api.py`.
- Гейт покрытия: `core_sys_time.cpp` и `core_sys/common_module.cpp` ≥80% (пофайлово).
