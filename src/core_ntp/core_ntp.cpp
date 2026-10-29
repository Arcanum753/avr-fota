#include "core_web/FSWebServerLib.h"

#include "main.h"
#include "core_json/core_json.h"
#include "core_ntp.h"

#include "common/common.h"
#include "common_module.h"
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

	// Рантайм NTP запускается при подключении Wi-Fi (ntpOnConnected -> applyNtpRuntime).
	// Здесь только готовим состояние: адаптивный stale и признак включённости.
	ntpUpdateStaleSec();
	updateTimeFromNTP = _ntpConfig.updateNTPTimeEvery > 0;
	_ntpServerNow = _ntpConfig.ntpServerName0;
}

// унифицированный конструктор контекста
void CLASS_CORE_NTP::begin (ModContext& ctx){
	_fs = ctx.fs;
	begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_CORE_NTP::web_Init ()	{
	DEBUGNTP(__PRETTY_FUNCTION__);	DEBUGNTP("\r\n");
	
	ESPHTTPServer.on("/ntp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		send_NTP_info_html(request);
	});
	
	ESPHTTPServer.on("/ntp/conf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		send_NTP_configuration_values_html(request);
	});

	ESPHTTPServer.on("/ntp.html", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
		html2ntp_configuration(request);
	});
	ESPHTTPServer.on("/ntp/ver", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {		return request->requestAuthentication(); }
        html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_NTP::send_NTP_info_html(AsyncWebServerRequest *request) {
	DEBUGNTP(__FUNCTION__);	DEBUGNTP("\r\n");
	String values = "";

	// Статус строим по кэшу последнего синка, а не по SyncStatus(): библиотека
	// сбрасывает статус на время запроса и после таймаута, хотя время ещё свежее.
	uint32_t lastSync = ntpLastSyncEpoch();
	bool     fresh    = ntpTimeFresh();

	String syncLine = "—";
	if (lastSync > 0) {
		syncLine = NTP.getTimeDateString((time_t)lastSync);
		if (!fresh) { syncLine += " (stale)"; }
	}

	values += "x_ntp_sync|" + syncLine + "|div\n";
	values += "x_ntp_time|" + (fresh ? (String)NTP.getTimeStr() : String("—")) + "|div\n";
	values += "x_ntp_date|" + (fresh ? (String)NTP.getDateStr() : String("—")) + "|div\n";
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
		// Сначала разбираем в локальные переменные: _ntpConfig меняем только после
		// успешной валидации.
		String s0 = _ntpConfig.ntpServerName0;
		String s1 = _ntpConfig.ntpServerName1;
		String s2 = _ntpConfig.ntpServerName2;
		long   period = _ntpConfig.updateNTPTimeEvery;

		for (uint8_t i = 0; i < request->args(); i++) {
			String name = request->argName(i);
			if (name == "ntpserver0") {
				s0 = urldecode(request->arg(i));
				s0.trim();
				continue;
			}
			if (name == "ntpserver1") {
				s1 = urldecode(request->arg(i));
				s1.trim();
				continue;
			}
			if (name == "ntpserver2") {
				s2 = urldecode(request->arg(i));
				s2.trim();
				continue;
			}
			if (name == "update") {
				// Пустое поле = «оставить как есть»: страница заполняет его асинхронно,
				// а toInt("") == 0 молча выключил бы NTP.
				String raw = request->arg(i);
				raw.trim();
				if (raw.length() > 0) { period = raw.toInt(); }
				continue;
			}
		}

		const char* err = ntpValidateConfig(s0, s1, s2, period);
		if (err != nullptr) {
			DEBUGNTP("ntp save rejected: %s\r\n", err);
			request->send(400, "text/plain", err);
			return;
		}

		_ntpConfig.ntpServerName0 = s0;
		_ntpConfig.ntpServerName1 = s1;
		_ntpConfig.ntpServerName2 = s2;
		_ntpConfig.updateNTPTimeEvery = period;

		save_configNTP();
		// NTPClient не трогаем из задачи веб-сервера: применяем в loop().
		_pendingApply = true;
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
	_ntpConfig.updateNTPTimeEvery = NTP_PERIOD_DEFAULT_MIN;
}

bool CLASS_CORE_NTP::load_config_NTP() {
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(CONFIG_FILE_NTP, doc)) return false;

	// Дефолты по полям: отсутствующий или пустой ключ не должен оставлять пустое имя.
	_ntpConfig.ntpServerName0 = doc["ntp0"].as<String>();
	_ntpConfig.ntpServerName1 = doc["ntp1"].as<String>();
	_ntpConfig.ntpServerName2 = doc["ntp2"].as<String>();

	if (!ns_core_ntp::ntpServerNameValid(_ntpConfig.ntpServerName0, true)) {
		DEBUGNTP("ntp0 invalid, use default\r\n");
		_ntpConfig.ntpServerName0 = NTPSERVER_DFLT0;
	}
	if (!ns_core_ntp::ntpServerNameValid(_ntpConfig.ntpServerName1, false)) {
		DEBUGNTP("ntp1 invalid, disabled\r\n");
		_ntpConfig.ntpServerName1 = "";
	}
	if (!ns_core_ntp::ntpServerNameValid(_ntpConfig.ntpServerName2, false)) {
		DEBUGNTP("ntp2 invalid, disabled\r\n");
		_ntpConfig.ntpServerName2 = "";
	}

	long period = doc["NTPperiod"] | NTP_PERIOD_DEFAULT_MIN;
	if (period < 0) { period = 0; }
	if (period > NTP_PERIOD_MAX_MIN) {
		DEBUGNTP("NTPperiod %ld clamped to %d\r\n", period, NTP_PERIOD_MAX_MIN);
		period = NTP_PERIOD_MAX_MIN;
	}
	_ntpConfig.updateNTPTimeEvery = period;

	DEBUGNTP("NTP Server0: %s\r\n", _ntpConfig.ntpServerName0.c_str());
	DEBUGNTP("NTP Server1: %s\r\n", _ntpConfig.ntpServerName1.c_str());
	DEBUGNTP("NTP Server2: %s\r\n", _ntpConfig.ntpServerName2.c_str());
	DEBUGNTP("NTP period: %ld min\r\n", _ntpConfig.updateNTPTimeEvery);
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

