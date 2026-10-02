#include "core_web/FSWebServerLib.h"

#include "main.h"
#include "core_ntp.h"
#include "core_state/core_state.h"
#include "core_sys/core_sys.h"

#include "common/TimeLib.h"

// ============================================================
// Источник времени для core_sys (Time Source Provider API)
// ============================================================

// Оценка времени NTP: NTP.getTime() на ESP32/ESP8266 асинхронный и может
// вернуть 0, поэтому ведём последний удачный синк сами.
static uint32_t s_ntpLastSyncMs    = 0;
static time_t   s_ntpLastSyncEpoch = 0;

static uint32_t ntpElapsedSec() {
	if (s_ntpLastSyncMs == 0) { return 0xFFFFFFFFUL; }
	return (uint32_t)(millis() - s_ntpLastSyncMs) / 1000UL;
}

// Прочитать время источника (read-only): last sync + прошедшее время.
static bool ntpGetTime(time_t& out) {
	if (!NTP.SyncStatus()) { return false; }
	if (s_ntpLastSyncMs == 0) { return false; }
	uint32_t elapsed = ntpElapsedSec();
	if (elapsed > CORE_SYS_TIME_NTP_STALE_S) { return false; }
	out = s_ntpLastSyncEpoch + (time_t)elapsed;
	return out > CORE_SYS_TIME_MIN_VALID;
}

// Причина невалидности для UI/логов.
static const char* ntpStatus() {
	if (!NTP.SyncStatus()) { return "no sync"; }
	if (s_ntpLastSyncMs == 0 || ntpElapsedSec() > CORE_SYS_TIME_NTP_STALE_S) {
		return "sync stale (no internet?)";
	}
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

void CLASS_CORE_NTP::applyTimeZone() {
	NTP.setTimeZone(core_sys.timeZoneHours(), core_sys.timeZoneMinutes());
	NTP.setDayLight(core_sys.daylight());
	if (updateTimeFromNTP) {
		NTP.begin(_ntpServerNow, core_sys.timeZoneHours(), core_sys.daylight(),
		          core_sys.timeZoneMinutes());
	}
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// on WiFi connect
void CLASS_CORE_NTP::ntpOnConnected (){
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	if (updateTimeFromNTP == true) { // Enable NTP sync
        NTP.setInterval ( _ntpConfig.updateNTPTimeEvery * MINUTES);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent(	[this](NTPSyncEvent_t event)	{	ntpOnSyncHandler(event);	});
		NTP.begin(_ntpServerNow, core_sys.timeZoneHours(), core_sys.daylight(),
		          core_sys.timeZoneMinutes());
	}
}

void CLASS_CORE_NTP::ntpOnDisconected () {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	NTP.stop();
	// Чистота состояния: без сброса ntpGetTime и так вернёт false по !SyncStatus().
	s_ntpLastSyncMs    = 0;
	s_ntpLastSyncEpoch = 0;
}

void CLASS_CORE_NTP::ntpOnSyncHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);

    if ( _ntpevent == timeSyncd) 		{ 
		DEBUGNTP("NTP_timeSyncd: "); 	
		DEBUGNTP(NTP.getTimeDateString(NTP.getLastNTPSync()).c_str() );	
		DEBUGNTP(" \r\n");

		s_ntpLastSyncMs    = millis();
		s_ntpLastSyncEpoch = (time_t)NTP.getLastNTPSync();
	}
	if ( _ntpevent == noResponse) 		{ DEBUGNTP("NTP_noResponse \r\n"); 		}
	if ( _ntpevent == invalidAddress) 	{ DEBUGNTP("NTP_invalidAddress \r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGNTP("NTP_requestSent \r\n"); 	}	
	if ( _ntpevent == errorSending) 	{ DEBUGNTP("NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGNTP("NTP_responseError \r\n"); 	}

	if ( _ntpevent == noResponse) 		{	ntpSwitchReserv();	}
	if ( _ntpevent == invalidAddress) 	{	ntpSwitchReserv();	}
	if ( _ntpevent == responseError) 	{	ntpSwitchReserv();	}

	if (WiFi.status() != WL_CONNECTED) 	{
		NTP.stop(); 
	}
}

void CLASS_CORE_NTP::ntpSwitchReserv (){

	if  (_ntpServerCount == 0)	{_ntpServerNow = _ntpConfig.ntpServerName0;}
	if  (_ntpServerCount == 1)	{_ntpServerNow = _ntpConfig.ntpServerName1;}
	if  (_ntpServerCount == 2)	{_ntpServerNow = _ntpConfig.ntpServerName2;}
	_ntpServerCount++;
	if (_ntpServerCount > 2) _ntpServerCount = 0;
}

void CLASS_CORE_NTP::sendTimeData() {
	// DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	DEBUGNTP("sendTimeData %s\r\n", NTP.getTimeDateString().c_str());
}
