#include "core_web/FSWebServerLib.h"

#include "main.h"
#include "core_json/core_json.h"
#include "core_ntp.h"

#include "common/common.h"
#include "core_state/core_state.h"
#include "core_ntp_version.h"

CLASS_CORE_NTP core_ntp;

// ============================================================
// begin()
// ============================================================

// init
void CLASS_CORE_NTP::begin (){

	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	_ntpServerCount = 0;
	
	defaultConfigNTP(); 
    if (load_config_NTP() == false ) { save_configNTP(); 	}
	_ntpServerNow = _ntpConfig.ntpServerName0;
	// Enable NTP sync
	if (_ntpConfig.updateNTPTimeEvery > 0) { updateTimeFromNTP = true;	}		

	core_state.signal("time.valid", BusValue::bo(false));
	core_state.signal("time.source", BusValue::str(_ntpServerNow));
}

// унифицированный конструктор контекста
void CLASS_CORE_NTP::begin (ModContext& ctx){
	_fs = ctx.fs;
	begin();
}

// ============================================================
// register_resources()
// ============================================================
void CLASS_CORE_NTP::register_resources() {
	DEBUGNTP("%s\r\n", __FUNCTION__);

	core_state.regState("now",    BusValue::TIME, "current local time (epoch)", false);
	core_state.regState("valid",  BusValue::BOOL, "NTP synced", false);
	core_state.regState("source", BusValue::STR,  "current NTP server", false);
	core_state.regEvent("synced", "NTP time synced");
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_CORE_NTP::web_Init ()	{
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	
	ESPHTTPServer.on("/ntp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		send_NTP_info_html(request);
	});
	
	ESPHTTPServer.on("/ntp/conf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		send_NTP_configuration_values_html(request);
	});

	ESPHTTPServer.on("/ntp.html", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		html2ntp_configuration(request);
	});
	ESPHTTPServer.on("/ntp/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_NTP::send_NTP_info_html(AsyncWebServerRequest *request) {
	DEBUGNTP(__FUNCTION__);	DEBUGNTP("\r\n");
	String values = "";

	bool ntpSynced = NTP.SyncStatus();

	values += "x_ntp_sync|" + (ntpSynced ? (String)NTP.getTimeDateString(NTP.getLastNTPSync()) : "—") + "|div\n";
	values += "x_ntp_time|" + (ntpSynced ? (String)NTP.getTimeStr() : "—") + "|div\n";
	values += "x_ntp_date|" + (ntpSynced ? (String)NTP.getDateStr() : "—") + "|div\n";
	values += "x_ntp_adr|" 	+ (String)NTP.getNtpServerName() + "|div\n";
	values += "x_uptime|" 	+ (String)NTP.getUptimeString() + "|div\n";

	time_t lastBoot = NTP.getLastBootTime();
	values += "x_last_boot|" + (lastBoot > 0 ? NTP.getTimeDateString(lastBoot) : "—") + "|div\n";

	request->send(200, "text/plain", values);
}

// ntp.html vvv
void CLASS_CORE_NTP::html2ntp_configuration(AsyncWebServerRequest *request) {
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	if (request->args() > 0)  {// Save Settings
		_ntpConfig.daylight = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver0") {
				_ntpConfig.ntpServerName0 = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "ntpserver1") {
				_ntpConfig.ntpServerName1 = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "ntpserver2") {
				_ntpConfig.ntpServerName2 = urldecode(request->arg(i));
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
				continue;
			}
		}
		save_configNTP();

		setTime(NTP.getTime()); //set time
	}
	ESPHTTPServer.handleFileRead("/ntp.html", request);
}

void CLASS_CORE_NTP::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
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

// ============================================================
// Конфиг
// ============================================================

void CLASS_CORE_NTP::defaultConfigNTP() {
	// DEFAULT CONFIG NTP
	_ntpConfig.ntpServerName0 = NTPSERVER_DFLT0;
	_ntpConfig.ntpServerName1 = NTPSERVER_DFLT1;
	_ntpConfig.ntpServerName2 = NTPSERVER_DFLT2;
	_ntpConfig.updateNTPTimeEvery = 15;
	_ntpConfig.timezone = 10;  // Moscow
	_ntpConfig.daylight = 0;
	
}

bool CLASS_CORE_NTP::load_config_NTP() {
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(CONFIG_FILE_NTP, doc)) return false;
	_ntpConfig.ntpServerName0 = doc["ntp0"].as<String>();
	_ntpConfig.ntpServerName1 = doc["ntp1"].as<String>();
	_ntpConfig.ntpServerName2 = doc["ntp2"].as<String>();
	_ntpConfig.updateNTPTimeEvery = doc["NTPperiod"].as<int32_t>();
	_ntpConfig.timezone = doc["timeZone"].as<int32_t>();
	_ntpConfig.daylight = doc["daylight"].as<int32_t>();

	DEBUGNTP("NTP Server0: %s\r\n", _ntpConfig.ntpServerName0.c_str());
	DEBUGNTP("NTP Server1: %s\r\n", _ntpConfig.ntpServerName1.c_str());
	DEBUGNTP("NTP Server2: %s\r\n", _ntpConfig.ntpServerName2.c_str());
	return true;
}

bool CLASS_CORE_NTP::save_configNTP() {
	DEBUGNTP("Save config NTP \r\n");
	JsonDocument doc;
	core_json.jsonFileLoadDoc(CONFIG_FILE_NTP, doc);
	doc["ntp0"] = _ntpConfig.ntpServerName0;
	doc["ntp1"] = _ntpConfig.ntpServerName1;
	doc["ntp2"] = _ntpConfig.ntpServerName2;
	doc["NTPperiod"] = _ntpConfig.updateNTPTimeEvery;
	doc["timeZone"] = _ntpConfig.timezone;
	doc["daylight"] = _ntpConfig.daylight;
	return core_json.jsonFileSaveDoc(CONFIG_FILE_NTP, doc);
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_CORE_NTP::getVersionStr(){
    return String(CORE_NTP_VERSION);
}

String CLASS_CORE_NTP::getGeneratedTime(){
    return String(CORE_NTP_GENERATED_TIME);
}

String CLASS_CORE_NTP::getCommitDateStr(){
    return String(CORE_NTP_COMMIT_DATE_STR);
}

void CLASS_CORE_NTP::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGNTP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "ntpversion|"     + getVersionStr()    + "|div\n";
    values += "ntpgentime|"     + getGeneratedTime() + "|div\n";
    values += "ntpgendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

