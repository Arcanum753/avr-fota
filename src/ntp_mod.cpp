#include "main.h"
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include <ArduinoJson.h>
#include "FSWebServerLib.h"
#include "debug.h"
#include "wifi_mod.h"
#include "udphelper.h"
#include "ntp_mod.h"


NTPMOD_CLASS ntpModClass(false);


NTPMOD_CLASS :: NTPMOD_CLASS (bool _in) {
	 dumb = _in;
 }


 
bool NTPMOD_CLASS::load_config_NTP() {
	JsonDocument jsonDoc;
	if (!ESPHTTPServer.load_jsonDoc(CONFIG_FILE_NTP, jsonDoc)){
		return false;
	}
	_ntpConfig.ntpServerName0 		= jsonDoc["ntp0"].as<const char *>();
	_ntpConfig.ntpServerName1 		= jsonDoc["ntp1"].as<const char *>();
	_ntpConfig.ntpServerName2 		= jsonDoc["ntp2"].as<const char *>();
	_ntpConfig.updateNTPTimeEvery 	= jsonDoc["NTPperiod"].as<long>();
	_ntpConfig.timezone 			= jsonDoc["timeZone"].as<long>();
	_ntpConfig.daylight 			= jsonDoc["daylight"].as<long>();

	DEBUGLOG("NTP Server0: %s\r\n", _ntpConfig.ntpServerName0.c_str());
	DEBUGLOG("NTP Server1: %s\r\n", _ntpConfig.ntpServerName1.c_str());
	DEBUGLOG("NTP Server2: %s\r\n", _ntpConfig.ntpServerName2.c_str());
	return true;
}



 bool NTPMOD_CLASS::save_configNTP() {
	DEBUGLOG("Save config NTP \r\n");
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
	_ntpConfig.timezone = 10;
	_ntpConfig.daylight = 1;
	save_configNTP();
}




void NTPMOD_CLASS::ntpHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);
    if ( _ntpevent == timeSyncd) 		{ DEBUGLOG("\t NTP_timeSyncd\r\n"); 	}
	if ( _ntpevent == noResponse) 		{ DEBUGLOG("\t NTP_noResponse \r\n"); 	}
	if ( _ntpevent == invalidAddress) 	{ DEBUGLOG("\t NTP_invalidAddress\r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGLOG("\t NTP_requestSent\r\n"); 	}
	if ( _ntpevent == errorSending) 	{ DEBUGLOG("\t NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGLOG("\t NTP_responseError \r\n"); 	}
	if (WiFi.status() != WL_CONNECTED) {return;}
	if (_ntpevent == noResponse 
            || _ntpevent == invalidAddress 
            || _ntpevent == responseError 
        ) {
		ntpBeginReserv();
	}
}

void NTPMOD_CLASS::ntpBeginReserv (){
	if  (_ntpserveer == 0) 	NTP.begin(_ntpConfig.ntpServerName0, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	if  (_ntpserveer == 1)  NTP.begin(_ntpConfig.ntpServerName1, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	if  (_ntpserveer == 2)  NTP.begin(_ntpConfig.ntpServerName2, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	_ntpserveer++;
	if (_ntpserveer > 2) _ntpserveer = 2;
}

void NTPMOD_CLASS::ntpBegin (){
    if (!load_config_NTP()) { defaultConfigNTP();  	}

	if (_ntpConfig.updateNTPTimeEvery > 0) { // Enable NTP sync
        NTP.setInterval (_ntpConfig.updateNTPTimeEvery * MINUTES);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent([this](NTPSyncEvent_t event){	ntpHandler(event);	});
		ntpBeginReserv();
		NTP.getTime();
	}
}








// ntp.html vvv
void NTPMOD_CLASS::send_NTP_configuration_html(AsyncWebServerRequest *request) {
	//DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
	if (request->args() > 0)  {// Save Settings
		_ntpConfig.daylight = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver0") {
				_ntpConfig.ntpServerName0 = ESPHTTPServer.urldecode(request->arg(i));
				DEBUGLOG("ntpServerName0: %s\r\n", _ntpConfig.ntpServerName0);
				continue;
			}
			if (request->argName(i) == "ntpserver1") {
				_ntpConfig.ntpServerName1 = ESPHTTPServer.urldecode(request->arg(i));
				DEBUGLOG("ntpServerName1: %s\r\n", _ntpConfig.ntpServerName1);
				continue;
			}
			if (request->argName(i) == "ntpserver2") {
				_ntpConfig.ntpServerName2 = ESPHTTPServer.urldecode(request->arg(i));
				DEBUGLOG("ntpServerName2: %s\r\n", _ntpConfig.ntpServerName2);
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
				DEBUGLOG("Daylight Saving: %d\r\n", _ntpConfig.daylight);
				continue;
			}
		}
		save_configNTP();
		NTP.setNtpServerName(_ntpConfig.ntpServerName0.c_str());
		NTP.setDayLight(_ntpConfig.daylight);
		ntpBeginReserv();
		setTime(NTP.getTime()); //set time
	}
	ESPHTTPServer.handleFileRead("/ntp.html", request);
	// request->send_P(200, "text/html", Page_GeneralNtp);


}
void NTPMOD_CLASS::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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


void NTPMOD_CLASS::webInit ()	{

    // ntp.html vvv
	ESPHTTPServer.on("/ntp/info", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		this->send_NTP_configuration_values_html(request);
	});

	ESPHTTPServer.on("/ntp.html", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		this->send_NTP_configuration_html(request);
	});
// ntp.html ^^^



}
