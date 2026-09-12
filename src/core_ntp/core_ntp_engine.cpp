#include "core_web/FSWebServerLib.h"

#include "main.h"
#include "core_ntp.h"
#include "core_state/core_state.h"

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
		NTP.begin(_ntpServerNow, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	}
}

void CLASS_CORE_NTP::ntpOnDisconected () {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	NTP.stop(); 
}

void CLASS_CORE_NTP::ntpOnSyncHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);

    if ( _ntpevent == timeSyncd) 		{ 
		DEBUGNTP("NTP_timeSyncd: "); 	
		DEBUGNTP(NTP.getTimeDateString(NTP.getLastNTPSync()).c_str() );	
		DEBUGNTP(" \r\n");

		core_state.signal("time.now", BusValue::tm((int64_t)now()));
		core_state.signal("time.valid", BusValue::bo(true));
		core_state.signal("time.source", BusValue::str(_ntpServerNow));
		core_state.emit("time.synced");
	}
	if ( _ntpevent == noResponse) 		{ DEBUGNTP("NTP_noResponse \r\n"); 		}
	if ( _ntpevent == invalidAddress) 	{ DEBUGNTP("NTP_invalidAddress \r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGNTP("NTP_requestSent \r\n"); 		}	
	if ( _ntpevent == errorSending) 	{ DEBUGNTP("NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGNTP("NTP_responseError \r\n"); 	}

	if ( _ntpevent == noResponse) 		{	ntpSwitchReserv();	}
	if ( _ntpevent == invalidAddress) 	{	ntpSwitchReserv();	}
	if ( _ntpevent == responseError) 	{	ntpSwitchReserv();	}

	if ( _ntpevent == noResponse || _ntpevent == invalidAddress
	     || _ntpevent == responseError) {
		core_state.signal("time.valid", BusValue::bo(false));
	}
				
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
