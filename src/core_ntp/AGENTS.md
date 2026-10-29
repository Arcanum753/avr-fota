# core_ntp — NTP-клиент (ядро)

> **Ядровый модуль.** Компилируется всегда. Источник времени для `core_sys`.

- **Файлы:** `core_ntp.h`, `core_ntp_types.h`, `core_ntp.cpp` (шаблон), `core_ntp_engine.cpp`
  (логика), `common_module.h/.cpp` (`ns_core_ntp`), `NtpClientLib.*` (форк библиотеки)
- **Registry:** `object=core_ntp`, `define=CORE_NTP`, `namespace=ntp`, `web=1`, `loop=1`,
  `res=0`, `prio=70`, `time_source=1`. `core_loop()` вызывает `core_ntp.loop()`
  (генератор добавляет его в список ядровых циклов) — там применяются отложенные действия.
- **Зависит от ядра:** `core_web`, `core_sys`, `core_state`, `core_json`

## Назначение

Синхронизация времени по NTP (3 сервера: основной + 2 запасных), период `NTPperiod`
(`config_ntp.json`). Вызов `ntpOnConnected()` — из `core_wifi` при подключении.

## Источник времени: эталон read-only

`core_ntp` — **источник времени** для `core_sys`, **не владелец** `time.*` и не владелец
TZ/DST. Ресурсов шины у модуля нет, поэтому в `[registry]` стоит `res = 0` (правило
«`res=1` ⇒ `<ns>.save`» не нарушается); регистрация источника времени от этого не зависит.

```cpp
// один удачный синк запоминаем сами: NTP.getTime() на ESP32/ESP8266 асинхронный (0)
static volatile uint32_t s_ntpLastSyncMs    = 0;
static volatile uint32_t s_ntpLastSyncEpoch = 0;
static volatile uint32_t s_ntpStaleSec      = CORE_SYS_TIME_NTP_STALE_S; // адаптивный

static bool ntpGetTime(time_t& out) {
    if (s_ntpLastSyncMs == 0) return false;
    uint32_t elapsed = ns_core_ntp::ntpElapsedSec(millis(), s_ntpLastSyncMs);
    if (elapsed > s_ntpStaleSec) return false;
    out = (time_t)s_ntpLastSyncEpoch + (time_t)elapsed;
    return out > CORE_SYS_TIME_MIN_VALID;
}

static const char* ntpStatus() {
    if (s_ntpLastSyncMs == 0) return "no sync";
    if (ns_core_ntp::ntpElapsedSec(millis(), s_ntpLastSyncMs) > s_ntpStaleSec) {
        return "sync stale (no internet?)";
    }
    return "";
}

void CLASS_CORE_NTP::registerTimeSource() {
    core_sys.addTimeSource("ntp", 100, ntpGetTime, nullptr, ntpStatus);
    core_state.on("time.tz_changed", ntpOnTimeTzChanged, this);
}
```

- **`NTP.SyncStatus()` в критерии валидности НЕ используется.** Библиотека ставит
  `status = ntpRequested` на время каждого запроса (`NTPClientLib.cpp:308`) и `unsyncd` на
  таймаут (`:426`); если опираться на `SyncStatus()`, `core_sys` объявляет источник невалидным
  на каждом опросе и переключается на другой источник — дребезг `ntp ↔ ds3231`.
- `s_ntpLastSyncMs`/`s_ntpLastSyncEpoch` (32 бита) обновляются в `ntpOnSyncHandler(timeSyncd)`
  (сначала epoch, затем `millis()`); сбрасываются в `ntpOnDisconected()` — при потере Wi-Fi
  источник невалиден сразу.
- **Адаптивный stale:** `s_ntpStaleSec = max(CORE_SYS_TIME_NTP_STALE_S, 2 × NTPperiod × 60)`.
  При периоде 5 мин это 30 мин, при 720 мин — 24 ч. Долгий период осознанно отодвигает переход
  на DS3231.
- При смене TZ/DST (`time.tz_changed`) `applyTimeZone()` переустанавливает
  `NTP.setTimeZone(core_sys.timeZoneHours(), core_sys.timeZoneMinutes())`,
  `NTP.setDayLight(core_sys.daylight())` и **перебазирует кэш до `NTP.begin()`**:
  `s_ntpLastSyncEpoch = now() - elapsed` (при `elapsed <= s_ntpStaleSec` и `now() >= 2020`).
  Проверять `timeStatus() == timeSet` нельзя: `begin()` вызывает провайдер, тот возвращает 0,
  и TimeLib ставит `timeNeedsSync`. `begin()` вызывается только если провайдер ещё не запущен
  (`_ntpStarted`), иначе сброс `_lastSyncd` ломает последующие сдвиги TZ.
- **`NTP.getTime()` не используется** для чтения текущего времени (асинхронный).

| Параметр | Значение |
|---|---|
| Имя источника | `ntp` |
| Приоритет | `100` |
| `get()` | `lastSync != 0 && (now-lastSync) ≤ s_ntpStaleSec && t ≥ 2020-01-01` |
| `set()` | нет (read-only) |
| `status()` | `"no sync"` / `"sync stale (no internet?)"` |

**При создании нового источника времени — копировать отсюда.**

## Failover серверов

Ошибка запроса (`noResponse`/`invalidAddress`) из колбэка AsyncUDP только ставит флаг
`s_failoverPending`; в `loop()` `ntpSwitchReserv()` выбирает следующий **непустой** сервер по
кругу 0→1→2→0 (`ns_core_ntp::ntpNextServerIndex`) и применяет его через
`NTP.setNtpServerName()`. `responseError` сервер не меняет: после него ещё взведён таймаут
запроса, и поздний `noResponse` переключил бы сервер второй раз (пропуск рабочего резерва).
После успешного синка `loop()` возвращает основной сервер (`ntp0`).

**Правило:** `String`-конфиг и `NTPClient` (`begin`/`stop`/`setNtpServerName`) трогает только
главный цикл. Колбэк синка и задачи (веб-сервер, системные события Wi-Fi) лишь выставляют
флаги `_pendingApply` / `_pendingStop` / `s_failoverPending` / `s_primaryResetPending`.

## Конфиг `/config_ntp.json`

`ntp0`, `ntp1`, `ntp2`, `NTPperiod` (минуты, 0..720; 0 — NTP выключен). Поля
`timeZone`/`daylight` **удалены** — TZ/DST в `config_time.json` (владелец `core_sys`, секция
«Time Sources» на `system.html`).

- Отсутствующие ключи подставляются дефолтами (`ntp0` = `pool.ntp.org`); пустой/слишком
  длинный `ntp0` → дефолт, `ntp1`/`ntp2` → выключен.
- Предел имени сервера — **39** символов (`SERVER_NAME_LENGTH=40`, `toCharArray` сохраняет 39).
- `period = 0` → `NTP.stop()`, кэш синка сбрасывается, источник невалиден. Повторное включение
  (period > 0) поднимает NTP без перезагрузки (`applyNtpRuntime()`).
- Страница: пустое поле «Update every» = «оставить как есть» (`toInt("") == 0` иначе молча
  выключал бы NTP); `0` принимается только при явно переданном нуле.

## Применение конфига

Единая точка — `applyNtpRuntime()`; вызывается **только из `loop()`** (по флагу `_pendingApply`,
который ставят обработчик `/ntp.html` и `ntpOnConnected()`). Пересчёт адаптивного stale,
`updateTimeFromNTP = period > 0`, `NTP.setInterval(period × 60)`, `NTP.setNTPTimeout`, подписка
на события синка, сброс на основной сервер. `applyTimeZone()` вызывает `NTP.begin()` только
если провайдер ещё не запущен; после `begin()` при валидном системном времени интервал
принудительно ставится на `period` (иначе `begin()` взводит короткие 15 с). Валидация —
`ntpValidateConfig()` (период 0..720, `ntp0` обязателен, `ntp1`/`ntp2` — пустые или валидные);
при ошибке страница получает `400` и конфиг не меняется. `ntpOnDisconected()` ставит
`_pendingStop`, чтобы `AsyncUDP` удалялся в главном цикле, а не в обработчике события.

## Веб-интерфейс

`GET /ntp/info`, `GET /ntp/conf`, `POST /ntp.html`, `GET /ntp/ver` — все под `checkAuth`;
страница `ntp.html` (3 сервера + период). `/ntp/info` строит `x_ntp_sync`/`x_ntp_time` по кэшу
последнего синка: при свежем кэше — время, при протухшем — метка с `(stale)`, при отсутствии
синка — `—`.

## Тестирование

L1 (native): `tests/core/core_ntp/test/test_l1_ntp.cpp` — `ns_core_ntp::ntpStaleSec`,
`ntpElapsedSec` (в т.ч. переполнение `millis()` и `lastMs == 0`), `ntpServerNameValid`,
`ntpNextServerIndex` (в т.ч. пустые слоты).

L4: `tests/core/core_ntp/http_api/test_ntp_api.py` (при наличии `DEVICE_HOST`).
