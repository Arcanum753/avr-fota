
#include "main.h"
#include "version.h"
#include <ArduinoJson.h>
#include "FSWebServerLib.h"


#if defined(ESP32)
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <ESPmDNS.h>
#endif

#if defined(ESP8266)
#include <FS.h>
#include <ESP8266mDNS.h>
#endif

#if defined(MODULE_GPIO)
#include "module_gpio/module_gpio.h"
#endif

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#if (MODULE_OTACLIENT == 1)
#include "module_otaclient/module_otaclient.h"
#endif

#ifdef PROGTYPE_SWD
#include "module_prog_swd/module_prog_swd.h"
#endif

#ifdef PROGTYPE_ISP
#include "module_prog_isp/module_prog_isp.h"
#endif




#include "debug.h"

#include "core_ota/core_ota.h"
#include "core_ntp/core_ntp.h"
#include "core_editor/core_editor.h"
#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"

#include "core_led/core_led.h"

#include "common.h"

AsyncFSWebServer ESPHTTPServer(80);



AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}
// esp8266/esp32 flash file system
#if defined(ESP32)
    void AsyncFSWebServer::begin(fs::SPIFFSFS* fs)
#endif
#if defined(ESP8266)
    void AsyncFSWebServer::begin(FS* fs)                         
#endif
{
	_fs = fs;
	DBG_OUTPUT_PORT.begin(115200);
	DBG_OUTPUT_PORT.print("\n\n");
#ifndef RELEASE
	DBG_OUTPUT_PORT.setDebugOutput(true);
#endif // RELEASE

	// If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
	if (AP_ENABLE_BUTTON >= 0) {	pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP); 	}
	if (AP_ENABLE_BUTTON >= 0) {
		modWifiClass._apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", modWifiClass._apConfig.APenable);
	}

    if (!_fs) { _fs->begin();  }// If SPIFFS is not started

	ModClassJson.setFs(&SPIFFS); // !!!MUST!!! be set as first as possible!

	loadHTTPAuth();
	defaultConfigSys();
	if (load_config_Sys() == false) {  save_configSys(); 	}

	modWifiClass.begin(&SPIFFS); // wifi load cfg and set callback hooks
	
	serialShowAbout();
	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server

	modWifiClass.webInit();	//WIFI INIT start here
	
	modNtpClass.begin();
	modNtpClass.webInit();
	
	String mdnsName =  getHostName();
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation. // TODO
	MDNS.addService("http", "tcp", 80);
	
	modOtaClass.setFs(&SPIFFS);
	modOtaClass.begin(getHostName(), _httpAuth.wwwPassword ); 
	modOtaClass.webInit();
	
	ModClassEdit.setFs(&SPIFFS);
	ModClassEdit.webInit();

#if defined(MODULE_GPIO)
	ModClassGpio.setFs(&SPIFFS);
	ModClassGpio.webInit();
#endif

#if (MODULE_OTACLIENT == 1)
	otaClient.begin();
	otaClient.webInit();
#endif
	
#ifdef PROGTYPE_SWD
	progSwd.setFs(&SPIFFS);
	progSwd.begin();
	progSwd.web_Init();
#endif

#ifdef PROGTYPE_ISP
	progIsp.setFs(&SPIFFS);
	progIsp.begin();
	progIsp.web_Init();
#endif
}

bool AsyncFSWebServer::loadHTTPAuth() {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(SECRET_FILE, jsonDoc) == false){
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		DEBUGLOG("Huh\n\r");
		return false;
	}
	_httpAuth.auth = jsonDoc["auth"];
	_httpAuth.wwwUsername = jsonDoc["user"].as<String>();
	_httpAuth.wwwPassword = jsonDoc["pass"].as<String>();
	DEBUGLOG(_httpAuth.auth ? "Secret initialized.\r\n" : "Auth disabled.\r\n");
	if (_httpAuth.auth) {
		DEBUGLOG("User: %s\r\n", _httpAuth.wwwUsername.c_str());
		DEBUGLOG("Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
	}
	return true;
}

// working with pages vvv
void AsyncFSWebServer::html_send_chipinfo(AsyncWebServerRequest *request) {
    DEBUGLOG(__FUNCTION__); DEBUGLOG("\r\n");
    
#if defined(ESP8266)
    // Максимально простая версия для ESP8266 - минимум операций
    ESP.wdtFeed();
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "x_chipid|%08X|div\n"
        "x_mhz|%d|div\n"
        "x_sdk|%s|div\n"
        "x_reason|%s|div\n",
        ESP.getChipId(),
        ESP.getCpuFreqMHz(),
        ESP.getSdkVersion(),
        getResetReason().c_str()  // .c_str() вместо создания новой String
    );
    request->send(200, "text/plain", buffer);
    
#endif
#if defined(ESP32)
    // Для ESP32 оставляем как было
    esp_task_wdt_reset();
    String values = "";
    values += "x_chipid|" + (String)ESP.getChipModel() + "|div\n";
    values += "x_mhz|" + (String)ESP.getCpuFreqMHz() + "|div\n";
    values += "x_sdk|" + (String)ESP.getSdkVersion() + "|div\n";
    values += "x_reason|" + getResetReason() + "|div\n";
    request->send(200, "text/plain", values);
#endif
}

void AsyncFSWebServer::restart_esp() {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	modWifiClass.wifiStatus = FS_STAT_RESET;
	WiFi.disconnect(true, false);
	// Only call _fs->end() if it hasn't been already ended by the OTA update process.
	// OTA already ended the filesystem in html_uploadUpdateFile() before calling Update.begin().
	// Calling _fs->end() again on an already-ended FS causes corruption and crash (Exception 9).
	if (!_ota_fsEndCalled) {
		_fs->end();
	}
	delay(1000);
	ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";

	request->send(200, "text/plain", values);

}

void AsyncFSWebServer::set_wwwauth_configuration(AsyncWebServerRequest *request)	{
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	DEBUGLOG("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0)	{
		bool save	   = false;
		_httpAuth.auth = false;
		for (uint8_t i = 0; i < request->args(); i++)		{
			if (request->argName(i) == "authconf") {
				save = true;
				continue;
			}
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
				continue;
			}
		}
		if (!_httpAuth.auth)		{
			_httpAuth.wwwUsername = "";
			_httpAuth.wwwPassword = "";
		}

		if (save)		{
			request->send_P(200, "text/html", Page_GeneralSys);
			saveHTTPAuth();
		}
	}
}

bool AsyncFSWebServer::saveHTTPAuth() {
	//flag_config = false;
	DEBUGLOG("Save secret\r\n");
	JsonDocument jsonDoc;

	jsonDoc["auth"] = _httpAuth.auth;
	jsonDoc["user"] = _httpAuth.wwwUsername;
	jsonDoc["pass"] = _httpAuth.wwwPassword;

	//TODO add AP data to html Sam Arcanum
	File configFile = _fs->open(SECRET_FILE, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open secret file for writing\r\n");
		configFile.close();
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif // RELEASE
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}


bool AsyncFSWebServer:: handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUGEDIT("handleFileRead: %s\r\n", path.c_str());
	// CANNOT RUN DELAY() INSIDE CALLBACK
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 1, 30); 	}	// Show activity on LED
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
	ESP.wdtFeed();
#endif
	if (path.endsWith("/")) {	path += HTML_INDEX;	}
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	
	// Сброс watchdog перед операциями SPIFFS (могут быть медленными)
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
	ESP.wdtFeed();
#endif
	
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) { path += ".gz"; }
		DEBUGEDIT("Content type: %s\r\n", contentType.c_str());
		
		// Сброс watchdog после exists() и перед beginResponse()
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    	ESP.wdtFeed();
#endif
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(".gz")) {response->addHeader("Content-Encoding", "gzip");}
		
		// Сброс watchdog после beginResponse() и перед send()
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    	ESP.wdtFeed();
#endif
		DEBUGEDIT("File %s exist\r\n", path.c_str());
		request->send(response);
		
		// Сброс watchdog после send()
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    	ESP.wdtFeed();
#endif
		DEBUGEDIT("File %s Sent\r\n", path.c_str());
		return true;
	}
	else
		DEBUGEDIT("Cannot find %s\n", path.c_str());
	return false;
}

// system.html vvv
void AsyncFSWebServer::html_system_Load(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "name|"		+ _sysConfig.deviceName		+ "|input\n";
	values += "serial|" 	+ _sysConfig.deviceSerial 	+ "|input\n";
	values += "scantime|" 	+ String(_sysConfig.wifiScanTime )	+ "|input\n";
	values += "aptime|" 	+ String(_sysConfig.wifiAPLifeTime) 	+ "|input\n";
	request->send(200, "text/plain", values);
}


void AsyncFSWebServer::html_system_Save(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	if (request->args() > 0) { // Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "name") 		{ _sysConfig.deviceName 	= urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "serial") 	{ _sysConfig.deviceSerial 	= urldecode(request->arg(i));	continue; }

			if (request->argName(i) == "scantime") { 
				int val = request->arg(i).toInt();
				// Проверка min/max
				if (val < -1) val = -1;
				if (val > 4) val = 4;
				_sysConfig.wifiScanTime = val; 
			}
            if (request->argName(i) == "aptime") { 
				int val = request->arg(i).toInt();
				// Проверка min/max
				if (val < 0) val = 0;
				if (val > 10) val = 10;
				_sysConfig.wifiAPLifeTime = val; 
			}
		}
		request->send_P(200, "text/html", Page_GeneralSys);
		save_configSys();
	}
	else {	handleFileRead(request->url(), request);	}
}
// system.html ^^^

String getContentType(String filename, AsyncWebServerRequest *request) {
	if 	(request->hasArg("download")) 		{return "application/octet-stream";}
	else if (filename.endsWith(".htm"))  	{return "text/html";}
	else if (filename.endsWith(".html")) 	{return "text/html";}
	else if (filename.endsWith(".css")) 	{return "text/css";}
	else if (filename.endsWith(".json")) 	{return "application/json";}
	else if (filename.endsWith(".js"))   	{return "application/javascript";}
	else if (filename.endsWith(".png")) 	{return "image/png";}
	else if (filename.endsWith(".gif")) 	{return "image/gif";}
	else if (filename.endsWith(".jpg")) 	{return "image/jpeg";}
	else if (filename.endsWith(".ico")) 	{return "image/x-icon";}
	else if (filename.endsWith(".xml")) 	{return "text/xml";}
	else if (filename.endsWith(".pdf")) 	{return "application/x-pdf";}
	else if (filename.endsWith(".zip")) 	{return "application/x-zip";}
	else if (filename.endsWith(".gz"))  	{return "application/x-gzip";}
	else if (filename.endsWith(".hex")) 	{return "text/html";} // TODO ??
	return "text/plain";
}

void AsyncFSWebServer::serverInit() {
	// Запрещаем Keep-Alive, чтобы браузер не держал открытые
	// TCP-соединения - они вызывали сброс ESP при длительном простое
	DefaultHeaders::Instance().addHeader("Connection", "close");

//system.html vvv	
	on("/system/restart", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		DBG_OUTPUT_PORT.println(request->url());
		request->send_P(200, "text/html", Page_IndexRefresh);
		this->restart_esp();
	});	
	on("/system/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_wwwauth_configuration_values_html(request);
	});	

	on("/system/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_send_chipinfo(request);
	});	
	on("/system/version", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_version_info(request);
	});	
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_system_Save(request);
	});	
	
	on("/system/savewwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->set_wwwauth_configuration(request);
	});	
	on("/system/devconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->html_system_Load(request);
	});	

//system.html ^^^

	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound([this](AsyncWebServerRequest *request) {
		DEBUGLOGFH("Not found: %s\r\n", request->url().c_str());
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
#if defined(ESP8266)
		ESP.wdtFeed();
#endif
		// Не создаём response заранее — handleFileRead сам отправит ответ
		// или мы отправим 404. AsyncWebServer сам управляет памятью response после send().
		if (!this->handleFileRead(request->url(), request)) {
			AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "FileNotFound");
			response->addHeader("Connection", "close");
			response->addHeader("Access-Control-Allow-Origin", "*");
			request->send(response);
			// НЕ удаляем response — AsyncWebServer сам освободит память после отправки
		}
	});

	_evs.onConnect([](AsyncEventSourceClient* client) {
		DEBUGLOG("Event source client connected from %s\r\n", client->client()->remoteIP().toString().c_str());
	});
	addHandler(&_evs);


#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET


#ifdef HIDE_CONFIG
	on(CONFIG_FILE_SYS, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_CONFIG

	//get heap status, analog input value and all GPIO statuses in one json call
	on("/all", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = "{";
		json += "\"heap\":" + String(ESP.getFreeHeap());
		json += ", \"analog\":" + String(analogRead(A0));
		//json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
		json += "}";
		request->send(200, "text/json", json);
		json = String();
	});
	//server.begin(); --> Not here
	DEBUGLOG("HTTP server started\r\n");
}

bool AsyncFSWebServer::checkAuth(AsyncWebServerRequest *request) {
	if (!_httpAuth.auth) {	return true;}
	else {
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}

}





String AsyncFSWebServer:: getResetReason() {
    String reason = "Unknown";
    
    #if defined(ESP32)
        esp_reset_reason_t r = esp_reset_reason();
        switch(r) {
            case ESP_RST_POWERON:    {reason = "Power on"; break;}
            case ESP_RST_SW:         {reason = "Software reset"; break;}
            case ESP_RST_PANIC:      {reason = "Exception/Panic"; break;}
            case ESP_RST_TASK_WDT:   {reason = "Task watchdog"; break;}
            case ESP_RST_WDT:        {reason = "Hardware watchdog"; break;}
            case ESP_RST_BROWNOUT:   {reason = "Brownout"; break;}
            default: {break;}
        }
		#elif defined(ESP8266)
        rst_info *resetInfo = ESP.getResetInfoPtr();
        switch(resetInfo->reason) {
			case REASON_DEFAULT_RST:      { reason = "Power on"; break;}
            case REASON_WDT_RST:          { reason = "Watchdog"; break;}
            case REASON_EXCEPTION_RST:    { reason = "Exception"; break;}
            case REASON_SOFT_WDT_RST:     { reason = "Software watchdog"; break;}
            case REASON_SOFT_RESTART:     { reason = "Software restart"; break;}
            case REASON_DEEP_SLEEP_AWAKE: { reason = "Deep sleep wake"; break;}
            case REASON_EXT_SYS_RST:      { reason = "External reset"; break;}
			default: {break;}
        }
    #endif
    
    return reason;
}







void AsyncFSWebServer::serialShowAbout() {
	Serial.printf("\n\r\t\t**About** \n\r ");
	Serial.printf("Project env: %s\n\r ", BUILD_ENV);	
	Serial.printf("git branch: %s\n\r ", GIT_BRANCH);	
	Serial.printf("ver date: %s\n\r ", BUILD_TIME);	
	Serial.printf("Fiemware ver: %s\n\r ", String(VERSION_BUILD).c_str());
	Serial.printf("File system ver: %s\n\r ", getFsVersionStr().c_str());
	
	Serial.printf("Device serial number: %s\n\r ", _sysConfig.deviceSerial.c_str());	
	#if defined(ESP32)
	Serial.printf("Flash chip size: %u\r\n", ESP.getFlashChipSize());
	#endif
	#if ESP8266
	Serial.printf("Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
	#endif
	Serial.printf("Scketch size: %u\r\n", 		ESP.getSketchSize());
	if (_fs) {
		Serial.printf("FS total: %u\r\n", 		_fs->totalBytes());
		Serial.printf("FS used: %u\r\n", 		_fs->usedBytes());
		Serial.printf("FS free: %u\r\n", 		_fs->totalBytes() - _fs->usedBytes());
	}

	Serial.printf("wifi ssid: %s \n", WiFi.SSID().c_str());
	Serial.printf("IP Address: %s \n", WiFi.localIP().toString().c_str());
	#if defined(ESP32)
    Serial.printf("WifiHostName  %s \n\r", 	WiFi.getHostname());
    #elif defined(ESP8266)
	Serial.printf("WifiHostName  %s \n\r", 	WiFi.hostname().c_str());
    #endif
	
	Serial.printf("Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
	Serial.printf("DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
	Serial.printf("local DNS hostname  http://%s.local \n\r", getHostName().c_str());
	Serial.printf("or you can connect directly  http://%s \n\r", WiFi.localIP().toString().c_str());
	
	
}

// *.html vvv
void AsyncFSWebServer::html_version_info(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "devicename|"  	+ _sysConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" 	+ _sysConfig.deviceSerial 		+ "|div\n";
	values += "versionapp|" 	+ String(FIRMWARE_VERSION) + "|div\n";
	values += "versionweb|" 	+ String(VERSION_WEB) + "|div\n";
	values += "versionfs|" 		+ getFsVersionStr() + "|div\n";
	
	values += "gitbranch|" ;values += GIT_BRANCH ;values += "|div\n";
	values += "gitcommit|" ;values += GIT_COMMIT ;values += "|div\n";
	values += "buildenv|" ;values += BUILD_ENV ;values += "|div\n";
	
	request->send(200, "text/plain", values);
}
// *.html ^^^


const String AsyncFSWebServer::getHostName() { return _sysConfig.deviceName+"_"+_sysConfig.deviceSerial; }

String AsyncFSWebServer::getFsVersionStr() {
    if (_sysConfig.fsVersion != "") { return _sysConfig.fsVersion; }
    
    if (!_fs) {
        DEBUGLOG("getFsVersionStr: FS not mounted\n");
        return "";
    }
    
    File jsonFile = _fs->open(FS_VERSION_JSON_PATH, "r");
    if (!jsonFile) {
        DEBUGLOG("getFsVersionStr: %s not found\n", FS_VERSION_JSON_PATH);
        return "";
    }
    
    String jsonStr;
    while (jsonFile.available()) { jsonStr += (char)jsonFile.read(); }
    jsonFile.close();
    
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, jsonStr);
    if (error) {
        DEBUGLOG("getFsVersionStr: JSON parse error: %s\n", error.c_str());
        return "";
    }
    
    const char* fullString = jsonDoc["filesystem"]["version"]["full_string"];
    if (fullString) {
        _sysConfig.fsVersion = String(fullString);
        DEBUGLOG("getFsVersionStr: FS version = %s\n", _sysConfig.fsVersion.c_str());
        return _sysConfig.fsVersion;
    }
    
    DEBUGLOG("getFsVersionStr: filesystem.version.full_string not found in JSON\n");
    return "";
}

bool AsyncFSWebServer::load_config_Sys() {
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(CONFIG_FILE_SYS, jsonDoc) == false){	return false;	}
	_sysConfig.deviceName 			= jsonDoc["deviceName"].as<const char *>();
	_sysConfig.deviceSerial 		= jsonDoc["deviceSerial"].as<const char *>();

	_sysConfig.wifiScanTime 		= jsonDoc["wifiScanTime"].as<int>();
	_sysConfig.wifiAPLifeTime 		= jsonDoc["wifiAPLifeTime"].as<int>();

	return true;
}

void AsyncFSWebServer::defaultConfigSys() {
	// DEFAULT CONFIG SYSTEM
	_sysConfig.wifiScanTime 	= 1;
	_sysConfig.wifiAPLifeTime	= 10;
	#ifdef ESP32
	_sysConfig.deviceName 		= "esp32";    
	_sysConfig.deviceSerial 	=   (String)ESP.getChipModel() ;
	#endif
	#if defined(ESP8266)
	_sysConfig.deviceName 		= "esp8266";
	_sysConfig.deviceSerial 	=   (String)ESP.getChipId() ;
	#endif

}

bool AsyncFSWebServer::save_configSys() {
	DEBUGLOG("Save config SYSTEM\r\n");
	JsonDocument jsonDoc;
	jsonDoc["deviceName"] 		= _sysConfig.deviceName;
	jsonDoc["deviceSerial"] 	= _sysConfig.deviceSerial;
	jsonDoc["wifiScanTime"]		= _sysConfig.wifiScanTime;
	jsonDoc["wifiAPLifeTime"] 	= _sysConfig.wifiAPLifeTime;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_FILE_SYS);
}

uint16_t AsyncFSWebServer::configSys_ApTimeGet() {	return _sysConfig.wifiAPLifeTime;}
int16_t AsyncFSWebServer::configSys_ScanTimeGet() {	return _sysConfig.wifiScanTime;}