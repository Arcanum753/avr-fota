#include "core_web/FSWebServerLib.h"

#include "main.h"
#include "core_ntp.h"
#include "core_state/core_state.h"
#include "core_sys/core_sys.h"

#include "common/TimeLib.h"
#include "common_module.h"

// ============================================================
// Источник времени для core_sys (Time Source Provider API)
// ============================================================

// Один удачный синк запоминаем сами: NTP.getTime() на ESP32/ESP8266 асинхронный,
// а SyncStatus() библиотеки сбрасывается на время каждого запроса и после
// таймаута. Опираться на него нельзя — иначе core_sys объявляет источник
// невалидным на каждом опросе (дребезг ntp <-> ds3231).
static volatile uint32_t s_ntpLastSyncMs    = 0;
static volatile uint32_t s_ntpLastSyncEpoch = 0;
// Адаптивный порог: max(CORE_SYS_TIME_NTP_STALE_S, 2 * период).
static volatile uint32_t s_ntpStaleSec      = CORE_SYS_TIME_NTP_STALE_S;

// Отложенные действия из контекста колбэка AsyncUDP (задача lwIP): трогать
// String-конфиг и NTPClient оттуда нельзя, поэтому только ставим флаг.
static volatile bool s_failoverPending     = false;
static volatile bool s_primaryResetPending = false;

static uint32_t ntpElapsedSec() {
	return ns_core_ntp::ntpElapsedSec(millis(), s_ntpLastSyncMs);
}

// Прочитать время источника (read-only): last sync + прошедшее время.
static bool ntpGetTime(time_t& out) {
	if (s_ntpLastSyncMs == 0) { return false; }
	uint32_t elapsed = ntpElapsedSec();
	if (elapsed > s_ntpStaleSec) { return false; }
	out = (time_t)s_ntpLastSyncEpoch + (time_t)elapsed;
	return out > CORE_SYS_TIME_MIN_VALID;
}

// Причина невалидности для UI/логов.
static const char* ntpStatus() {
	if (s_ntpLastSyncMs == 0) { return "no sync"; }
	if (ntpElapsedSec() > s_ntpStaleSec) { return "sync stale (no internet?)"; }
	return "";
}

// Переустановить TZ/DST в NTPClientLib при смене часового пояса.
static int ntpOnTimeTzChanged(void* user, int argc, const BusValue* argv, BusValue& result) {
	(void)argc; (void)argv; (void)result;
	CLASS_CORE_NTP* self = (CLASS_CORE_NTP*)user;
	if (self != nullptr) { self->applyTimeZone(); }
	return 0;
}

void CLASS_CORE_NTP::registerTimeSource() {
	core_sys.addTimeSource("ntp", 100, ntpGetTime, nullptr, ntpStatus);
	core_state.on("time.tz_changed", ntpOnTimeTzChanged, this);
}

// ============================================================
// Состояние последнего синка (для /ntp/info и диагностики)
// ============================================================

uint32_t CLASS_CORE_NTP::ntpLastSyncEpoch() const {
	return s_ntpLastSyncEpoch;
}

const char* CLASS_CORE_NTP::ntpStatusText() const {
	return ntpStatus();
}

bool CLASS_CORE_NTP::ntpTimeFresh() const {
	return (s_ntpLastSyncMs != 0) && (ntpElapsedSec() <= s_ntpStaleSec);
}

void CLASS_CORE_NTP::ntpUpdateStaleSec() {
	uint32_t period = (_ntpConfig.updateNTPTimeEvery > 0)
	                ? (uint32_t)_ntpConfig.updateNTPTimeEvery : 0;
	s_ntpStaleSec = ns_core_ntp::ntpStaleSec(period, CORE_SYS_TIME_NTP_STALE_S);
}

// Отложенные действия (веб-задача и колбэк синка) применяются здесь, в главном цикле.
void CLASS_CORE_NTP::loop() {
	if (_pendingStop) {
		_pendingStop = false;
		NTP.stop();
		_ntpStarted = false;
	}
	if (_pendingApply) {
		_pendingApply = false;
		applyNtpRuntime();
	}
	if (s_failoverPending) {
		s_failoverPending = false;
		ntpSwitchReserv();
	}
	if (s_primaryResetPending) {
		s_primaryResetPending = false;
		ntpSetServer(0);
	}
}

void CLASS_CORE_NTP::applyTimeZone() {
	NTP.setTimeZone(core_sys.timeZoneHours(), core_sys.timeZoneMinutes());
	NTP.setDayLight(core_sys.daylight());

	// setTimeZone()/setDayLight() сдвинули системное время — перебазируем кэш
	// последнего синка, иначе core_sys отбросит NTP как «время в прошлом».
	// Делаем ДО begin(): begin() сбрасывает _lastSyncd и статус TimeLib
	// (timeStatus() становится timeNeedsSync), поэтому проверять timeSet нельзя.
	if (s_ntpLastSyncMs != 0) {
		uint32_t el = ntpElapsedSec();
		time_t   t  = now();
		if (el <= s_ntpStaleSec && t > CORE_SYS_TIME_MIN_VALID + (time_t)el) {
			s_ntpLastSyncEpoch = (uint32_t)(t - (time_t)el);
			s_ntpLastSyncMs    = millis();
		}
	}

	if (updateTimeFromNTP && !_ntpStarted) {
		if (!NTP.begin(_ntpServerNow, core_sys.timeZoneHours(), core_sys.daylight(),
		               core_sys.timeZoneMinutes())) {
			DEBUGNTP("NTP begin failed\r\n");
			return;
		}
		_ntpStarted = true;
		// begin() взводит короткий интервал (15 c). Если системное время уже
		// валидно (RTC), ждём настроенный период, а не опрашиваем каждые 15 c.
		if (now() > CORE_SYS_TIME_MIN_VALID) {
			setSyncInterval((time_t)(_ntpConfig.updateNTPTimeEvery * MINUTES));
		}
	}
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// on WiFi connect
void CLASS_CORE_NTP::ntpOnConnected (){
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	// Вызывается из обработчика системного события: NTPClient поднимаем в loop().
	_pendingApply = true;
}

void CLASS_CORE_NTP::ntpOnDisconected () {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	// AsyncUDP удаляется только в loop(): из обработчика события это был бы
	// use-after-free для пакета, пришедшего в задачу lwIP.
	_pendingStop = true;
	// Wi-Fi пропал: кэш невалиден сразу (без ожидания stale).
	s_ntpLastSyncMs    = 0;
	s_ntpLastSyncEpoch = 0;
	_ntpServerCount    = 0;
	s_failoverPending     = false;
	s_primaryResetPending = false;
}

void CLASS_CORE_NTP::ntpOnSyncHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);

    if ( _ntpevent == timeSyncd) 		{ 
		DEBUGNTP("NTP_timeSyncd: "); 	
		DEBUGNTP(NTP.getTimeDateString(NTP.getLastNTPSync()).c_str() );	
		DEBUGNTP(" \r\n");

		// Порядок важен: сначала время, затем метка millis() (её читает get()).
		s_ntpLastSyncEpoch = (uint32_t)NTP.getLastNTPSync();
		s_ntpLastSyncMs    = millis();

		// После успеха — вернуться на основной сервер (применит loop()).
		s_primaryResetPending = true;
	}
	if ( _ntpevent == noResponse) 		{ DEBUGNTP("NTP_noResponse \r\n"); 		}
	if ( _ntpevent == invalidAddress) 	{ DEBUGNTP("NTP_invalidAddress \r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGNTP("NTP_requestSent \r\n"); 	}	
	if ( _ntpevent == errorSending) 	{ DEBUGNTP("NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGNTP("NTP_responseError \r\n"); 	}

	// Сервер меняем только здесь (loop()). responseError сюда не входит: после
	// него всё ещё взведён таймаут запроса, и поздний noResponse переключил бы
	// сервер второй раз, пропустив рабочий резерв.
	if ( _ntpevent == noResponse) 		{	s_failoverPending = true;	}
	if ( _ntpevent == invalidAddress) 	{	s_failoverPending = true;	}
}

// Выбор следующего непустого сервера (применяется в ntpSetServer).
void CLASS_CORE_NTP::ntpSwitchReserv (){

	int next = ns_core_ntp::ntpNextServerIndex(_ntpServerCount,
		_ntpConfig.ntpServerName0, _ntpConfig.ntpServerName1, _ntpConfig.ntpServerName2);
	if (next < 0) { return; }   // все имена пустые — остаёмся на текущем
	ntpSetServer(next);
}

// Применить сервер по индексу. Вызывается только из главного цикла.
void CLASS_CORE_NTP::ntpSetServer(int idx) {
	if (idx < 0 || idx >= NTP_SERVER_COUNT) { return; }

	const String* names[NTP_SERVER_COUNT] = {
		&_ntpConfig.ntpServerName0, &_ntpConfig.ntpServerName1, &_ntpConfig.ntpServerName2
	};
	String name = *names[idx];
	if (!name.length()) { return; }

	// Библиотека использует собственный буфер имени (_ntpServerName), поэтому
	// недостаточно поменять _ntpServerNow — нужно применить имя.
	if (!NTP.setNtpServerName(name)) {
		DEBUGNTP("ntpSetServer: setNtpServerName failed\r\n");
		return;
	}

	if (_ntpServerCount != idx) {
		DEBUGNTP("NTP server switched to %s\r\n", name.c_str());
	}
	_ntpServerCount = idx;
	_ntpServerNow   = name;
}

// ============================================================
// Применение конфига NTP
// ============================================================

const char* CLASS_CORE_NTP::ntpValidateConfig(const String& s0, const String& s1,
                                              const String& s2, long period) {
	if (period < 0 || period > NTP_PERIOD_MAX_MIN) { return "period out of range"; }
	if (!ns_core_ntp::ntpServerNameValid(s0, true))  { return "server0 invalid"; }
	if (!ns_core_ntp::ntpServerNameValid(s1, false)) { return "server1 invalid"; }
	if (!ns_core_ntp::ntpServerNameValid(s2, false)) { return "server2 invalid"; }
	return nullptr;
}

// Единая точка применения конфига в рантайме (после присвоения _ntpConfig).
// Вызывается только из loop() (флаг _pendingApply) — NTPClient не трогаем из
// задач веб-сервера и системных событий Wi-Fi.
void CLASS_CORE_NTP::applyNtpRuntime() {
	ntpUpdateStaleSec();
	updateTimeFromNTP = _ntpConfig.updateNTPTimeEvery > 0;
	s_failoverPending     = false;
	s_primaryResetPending = false;

	if (!updateTimeFromNTP) {
		// period = 0 — NTP выключен: остановить провайдер и обнулить кэш.
		NTP.stop();
		_ntpStarted = false;
		s_ntpLastSyncMs    = 0;
		s_ntpLastSyncEpoch = 0;
		_ntpServerCount    = 0;
		_ntpServerNow      = "";
		DEBUGNTP("NTP disabled (period 0)\r\n");
		return;
	}

	NTP.setInterval((int)(_ntpConfig.updateNTPTimeEvery * MINUTES));
	NTP.setNTPTimeout(NTP_TIMEOUT);
	NTP.onNTPSyncEvent([this](NTPSyncEvent_t event) { ntpOnSyncHandler(event); });

	_ntpServerCount = 0;
	_ntpServerNow   = _ntpConfig.ntpServerName0;
	if (_ntpStarted) {
		// Провайдер уже поднят: НЕ вызываем begin() (он сбросил бы _lastSyncd и
		// короткий интервал), только применяем имя основного сервера.
		NTP.setNtpServerName(_ntpServerNow);
	}
	// begin() внутри applyTimeZone() — только если провайдер ещё не запущен.
	applyTimeZone();
}

