# core_ntp — NTP-клиент (ядро)

> **Ядровый модуль.** Компилируется всегда. Источник времени для `core_sys`.

- **Файлы:** `core_ntp.h`, `core_ntp_types.h`, `core_ntp.cpp` (шаблон), `core_ntp_engine.cpp`
  (логика), `NtpClientLib.*` (форк библиотеки)
- **Registry:** `object=core_ntp`, `define=CORE_NTP`, `namespace=ntp`, `web=1`, `loop=0`,
  `res=1`, `prio=70`, `time_source=1`
- **Зависит от ядра:** `core_web`, `core_sys`, `core_state`, `core_json`

## Назначение

Синхронизация времени по NTP (3 сервера: основной + 2 запасных), период `NTPperiod`
(`config_ntp.json`). Вызов `ntpOnConnected()` — из `core_wifi` при подключении.

## Источник времени: эталон read-only

`core_ntp` — **источник времени** для `core_sys`, **не владелец** `time.*` и не владелец
TZ/DST. Ресурсы `time.*` принадлежат `core_sys`; здесь своего namespace-контента нет
(`register_resources()` — пустая заглушка для контракта registry).

```cpp
// один удачный синк запоминаем сами: NTP.getTime() на ESP32/ESP8266 асинхронный (0)
static uint32_t s_ntpLastSyncMs    = 0;
static time_t   s_ntpLastSyncEpoch = 0;

static bool ntpGetTime(time_t& out) {
    if (!NTP.SyncStatus()) return false;
    if (s_ntpLastSyncMs == 0) return false;
    uint32_t elapsed = (millis() - s_ntpLastSyncMs) / 1000;
    if (elapsed > CORE_SYS_TIME_NTP_STALE_S) return false;
    out = s_ntpLastSyncEpoch + (time_t)elapsed;
    return out > CORE_SYS_TIME_MIN_VALID;
}

static const char* ntpStatus() {
    if (!NTP.SyncStatus()) return "no sync";
    if (s_ntpLastSyncMs == 0 || (millis() - s_ntpLastSyncMs) / 1000 > CORE_SYS_TIME_NTP_STALE_S) {
        return "sync stale (no internet?)";
    }
    return "";
}

void CLASS_CORE_NTP::registerTimeSource() {
    core_sys.addTimeSource("ntp", 100, ntpGetTime, nullptr, ntpStatus);
    core_state.on("time.tz_changed", ntpOnTimeTzChanged, this);
}
```

- `s_ntpLastSyncMs`/`s_ntpLastSyncEpoch` обновляются в `ntpOnSyncHandler(timeSyncd)`;
  сбрасываются в `ntpOnDisconected()`.
- При смене TZ/DST (`time.tz_changed`) `applyTimeZone()` переустанавливает
  `NTP.setTimeZone(core_sys.timeZoneHours(), core_sys.timeZoneMinutes())`,
  `NTP.setDayLight(core_sys.daylight())` и при включённом NTP — `NTP.begin(...)`.
- **`NTP.getTime()` не используется** для чтения текущего времени (асинхронный).

| Параметр | Значение |
|---|---|
| Имя источника | `ntp` |
| Приоритет | `100` |
| `get()` | `SyncStatus() && (now-lastSync) ≤ 1800 c && t ≥ 2020-01-01` |
| `set()` | нет (read-only) |
| `status()` | `"no sync"` / `"sync stale (no internet?)"` |

**При создании нового источника времени — копировать отсюда.**

## Конфиг `/config_ntp.json`

`ntp0`, `ntp1`, `ntp2`, `NTPperiod` (минуты). Поля `timeZone`/`daylight` **удалены** —
TZ/DST в `config_time.json` (владелец `core_sys`, страница `time.html`).

## Веб-интерфейс

`GET /ntp/info`, `POST /ntp/save`, `GET /ntp/ver`; страница `ntp.html` (3 сервера + период).
Селект TZ и чекбокс DST убраны.

## Тестирование

L4: `tests/core/core_ntp/http_api/test_ntp_api.py` (при наличии `DEVICE_HOST`).
