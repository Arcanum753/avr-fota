#include "main.h"
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include <ArduinoJson.h>
#include "FSWebServerLib.h"
// #include "debug.h"
#include "module_wifi.h"
#include "module_udp.h"
#include "module_ntp.h"
#include "common.h"


NTPMOD_CLASS ntpModClass(false);


NTPMOD_CLASS :: NTPMOD_CLASS (bool _in) { dumb = _in; }


// init
void NTPMOD_CLASS::ntpBegin (){
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	_ntpServerCount = 0;
	
	defaultConfigNTP(); 
    if (load_config_NTP() == false ) { save_configNTP(); 	}
	_ntpServerNow = _ntpConfig.ntpServerName0;
	// Enable NTP sync
	if (_ntpConfig.updateNTPTimeEvery > 0) { updateTimeFromNTP = true;	}		
}


// on WiFi connect
void NTPMOD_CLASS::ntpOnConnected (){
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	if (updateTimeFromNTP == true) { // Enable NTP sync
        NTP.setInterval ( _ntpConfig.updateNTPTimeEvery * MINUTES);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent(	[this](NTPSyncEvent_t event)	{	ntpOnSyncHandler(event);	});
		NTP.begin(_ntpServerNow, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	}
}



void NTPMOD_CLASS::ntpOnDisconected () {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	NTP.stop(); 
}


void NTPMOD_CLASS::ntpOnSyncHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);

    if ( _ntpevent == timeSyncd) 		{ 
		DEBUGNTP("NTP_timeSyncd: "); 	
		DEBUGNTP(NTP.getTimeDateString(NTP.getLastNTPSync()).c_str() );	
		DEBUGNTP(" \r\n");
	}
	if ( _ntpevent == noResponse) 		{ DEBUGNTP("NTP_noResponse \r\n"); 		}
	if ( _ntpevent == invalidAddress) 	{ DEBUGNTP("NTP_invalidAddress \r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGNTP("NTP_requestSent \r\n"); 		}	
	if ( _ntpevent == errorSending) 	{ DEBUGNTP("NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGNTP("NTP_responseError \r\n"); 	}

	if ( _ntpevent == noResponse) 		{	ntpSwitchReserv();	}
	if ( _ntpevent == invalidAddress) 	{	ntpSwitchReserv();	}
	if ( _ntpevent == responseError) 	{	ntpSwitchReserv();	}
				
	if (WiFi.status() != WL_CONNECTED) 	{
		NTP.stop(); 
	}
}

void NTPMOD_CLASS::ntpSwitchReserv (){

	if  (_ntpServerCount == 0)	{_ntpServerNow = _ntpConfig.ntpServerName0;}
	if  (_ntpServerCount == 1)	{_ntpServerNow = _ntpConfig.ntpServerName1;}
	if  (_ntpServerCount == 2)	{_ntpServerNow = _ntpConfig.ntpServerName2;}
	_ntpServerCount++;
	if (_ntpServerCount > 2) _ntpServerCount = 0;
}




bool NTPMOD_CLASS::load_config_NTP() {
	JsonDocument jsonDoc;
	if (!ESPHTTPServer.load_jsonDoc(CONFIG_FILE_NTP, jsonDoc))	{
		return false;
	}
	_ntpConfig.ntpServerName0 		= jsonDoc["ntp0"].as<const char *>();
	_ntpConfig.ntpServerName1 		= jsonDoc["ntp1"].as<const char *>();
	_ntpConfig.ntpServerName2 		= jsonDoc["ntp2"].as<const char *>();
	_ntpConfig.updateNTPTimeEvery 	= jsonDoc["NTPperiod"].as<long>();
	_ntpConfig.timezone 			= jsonDoc["timeZone"].as<long>();
	_ntpConfig.daylight 			= jsonDoc["daylight"].as<long>();

	DEBUGNTP("NTP Server0: %s\r\n", _ntpConfig.ntpServerName0.c_str());
	DEBUGNTP("NTP Server1: %s\r\n", _ntpConfig.ntpServerName1.c_str());
	DEBUGNTP("NTP Server2: %s\r\n", _ntpConfig.ntpServerName2.c_str());
	return true;
}

bool NTPMOD_CLASS::save_configNTP() {
	DEBUGNTP("Save config NTP \r\n");
	JsonDocument jsonDoc;
	jsonDoc["ntp0"] 		= _ntpConfig.ntpServerName0;
	jsonDoc["ntp1"] 		= _ntpConfig.ntpServerName1;
	jsonDoc["ntp2"] 		= _ntpConfig.ntpServerName2;
	jsonDoc["NTPperiod"] 	= _ntpConfig.updateNTPTimeEvery;
	jsonDoc["timeZone"] 	= _ntpConfig.timezone;
	jsonDoc["daylight"] 	= _ntpConfig.daylight;
	return ESPHTTPServer.save_jsonDoc(jsonDoc, CONFIG_FILE_NTP);
}

void NTPMOD_CLASS::defaultConfigNTP() {
	// DEFAULT CONFIG NTP
	_ntpConfig.ntpServerName0 = NTPSERVER_DFLT0;
	_ntpConfig.ntpServerName1 = NTPSERVER_DFLT1;
	_ntpConfig.ntpServerName2 = NTPSERVER_DFLT2;
	_ntpConfig.updateNTPTimeEvery = 15;
	_ntpConfig.timezone = 10;  // Moscow
	_ntpConfig.daylight = 0;
	
}

void NTPMOD_CLASS::webInit ()	{
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	
	ESPHTTPServer.on("/ntp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		this->send_NTP_info_html(request);
	});
	
	ESPHTTPServer.on("/ntp/conf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		this->send_NTP_configuration_values_html(request);
	});

	ESPHTTPServer.on("/ntp.html", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		this->send_NTP_configuration_html(request);
	});
}


void NTPMOD_CLASS::send_NTP_info_html(AsyncWebServerRequest *request) {
	DEBUGNTP(__FUNCTION__);	DEBUGNTP("\r\n");
	String values = "";

	values += "x_ntp_sync|" + (String)NTP.getTimeDateString(NTP.getLastNTPSync()) + "|div\n";
	values += "x_ntp_time|" + (String)NTP.getTimeStr() + "|div\n";
	values += "x_ntp_date|" + (String)NTP.getDateStr() + "|div\n";
	values += "x_ntp_adr|" 	+ (String)NTP.getNtpServerName() + "|div\n";
	values += "x_uptime|" 	+ (String)NTP.getUptimeString() + "|div\n";
	values += "x_last_boot|" + NTP.getTimeDateString(NTP.getLastBootTime()) + "|div\n";

	request->send(200, "text/plain", values);
}


// ntp.html vvv
void NTPMOD_CLASS::send_NTP_configuration_html(AsyncWebServerRequest *request) {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	if (request->args() > 0)  {// Save Settings
		_ntpConfig.daylight = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver0") {
				_ntpConfig.ntpServerName0 = urldecode(request->arg(i));
				DEBUGNTP("ntpServerName0: %s\r\n", _ntpConfig.ntpServerName0);
				continue;
			}
			if (request->argName(i) == "ntpserver1") {
				_ntpConfig.ntpServerName1 = urldecode(request->arg(i));
				DEBUGNTP("ntpServerName1: %s\r\n", _ntpConfig.ntpServerName1);
				continue;
			}
			if (request->argName(i) == "ntpserver2") {
				_ntpConfig.ntpServerName2 = urldecode(request->arg(i));
				DEBUGNTP("ntpServerName2: %s\r\n", _ntpConfig.ntpServerName2);
				continue;
			}
			if (request->argName(i) == "update") {
				_ntpConfig.updateNTPTimeEvery = request->arg(i).toInt();
				NTP.setInterval(_ntpConfig.updateNTPTimeEvery * 60);
				continue;
			}
			if (request->argName(i) == "tz") {
				_ntpConfig.timezone = request->arg(i).toInt();
				  NTP.setTimeZone(_ntpConfig.timezone / 10);
				continue;
			}
			if (request->argName(i) == "dst") {
				_ntpConfig.daylight = true;
				DEBUGNTP("Daylight Saving: %d\r\n", _ntpConfig.daylight);
				continue;
			}
		}
		save_configNTP();

		setTime(NTP.getTime()); //set time
	}
	ESPHTTPServer.handleFileRead("/ntp.html", request);

}


void NTPMOD_CLASS::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
	DEBUGNTP(__FUNCTION__);	DEBUGNTP("\r\n");
	String values = "";
	values += "ntpserver0|" 	+ (String)_ntpConfig.ntpServerName0 			+ "|input\n";
	values += "ntpserver1|" 	+ (String)_ntpConfig.ntpServerName1 			+ "|input\n";
	values += "ntpserver2|" 	+ (String)_ntpConfig.ntpServerName2 			+ "|input\n";

	values += "ntpserver0_d|" 	+ (String)_ntpConfig.ntpServerName0 			+ "|div\n";
	values += "ntpserver1_d|" 	+ (String)_ntpConfig.ntpServerName1 			+ "|div\n";
	values += "ntpserver2_d|" 	+ (String)_ntpConfig.ntpServerName2 			+ "|div\n";

	values += "update|" 	+ (String)_ntpConfig.updateNTPTimeEvery 			+ "|input\n";
	values += "tz|" 		+ (String)_ntpConfig.timezone 						+ "|input\n";
	values += "dst|" 		+ (String)(_ntpConfig.daylight ? "checked" : "") 	+ "|chk\n";
	request->send(200, "text/plain", values);
}


// ntp.html ^^^


void NTPMOD_CLASS::sendTimeData() {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	DEBUGNTP("sendTimeData %s\r\n", NTP.getTimeDateString().c_str());
}





