
#include "main.h"
#include "version.h"
#include "FSWebServerLib.h"


#if defined(ESP32)
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <ESPmDNS.h>
#endif

#if defined(ESP8266)
#include <LittleFS.h>
#include <ESP8266mDNS.h>
#endif

// Заголовки модулей (module_*/submodule_*/device_*) подключаются автоматически
// в сгенерированном файле src/modules_registry.cpp (генератор python/module_registry_gen.py).

// GZIP_ENABLED — включает поддержку .gz версий статических файлов.
// При включении сервер ищет и отдаёт файлы с расширением .gz (например index.html.gz),
// что позволяет хранить упакованные файлы в littlefs для экономии места.
// Требует предварительной gzip-упаковки всех файлов из data/ перед сборкой littlefs.
// Раскомментируйте, если ваши файлы в littlefs предварительно сжаты gzip.
//#define GZIP_ENABLED

#include "debug.h"

#include "core_ota/core_ota.h"
#include "core_ntp/core_ntp.h"
#include "core_editor/core_editor.h"
#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"

#include "modules_registry.h"

#include "core_led/core_led.h"

#include "common.h"

AsyncFSWebServer ESPHTTPServer(80);



AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}
// esp8266/esp32 flash file system
#if defined(ESP32)
    void AsyncFSWebServer::begin(fs::LittleFSFS* fs)
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
		core_wifi._apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", core_wifi._apConfig.APenable);
	}

    if (!_fs) { _fs->begin();  }// If LittleFS is not started

	// JSON-ядро должно быть инициализировано первым (используется loadHTTPAuth/load_config_Sys
	// и всеми begin(ctx) через core_json). Это соответствует старому // !!!MUST!!! be set as first.
	g_ctx.fs = fs;
	core_json.begin(g_ctx);

	loadHTTPAuth();
	defaultConfigSys();
	if (load_config_Sys() == false) {  save_configSys(); 	}

	// Заполняем остальные поля глобального контекста модулей (hostname, password)
	g_ctx.hostname = getHostName();
	g_ctx.password = _httpAuth.wwwPassword;

	// Инициализация ядер/модулей/устройств через сгенерированный registry
	core_begin(g_ctx);
	modules_begin(g_ctx);
	dev_begin(g_ctx);

	serialShowAbout();
	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server

	// Регистрация веб-путей ядер/модулей/устройств
	core_web_Init();
	modules_web_Init();
	dev_web_Init();

	String mdnsName =  getHostName();
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation. // TODO
	MDNS.addService("http", "tcp", 80);
}

bool AsyncFSWebServer::loadHTTPAuth() {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(SECRET_FILE, doc)) {
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		DEBUGLOG("Huh\n\r");
		return false;
	}
	_httpAuth.auth = doc["auth"].as<bool>();
	_httpAuth.wwwUsername = doc["user"].as<String>();
	_httpAuth.wwwPassword = doc["pass"].as<String>();
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
    ESP.wdtFeed();
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "x_chipid|%s|div\n"
        "x_mhz|%d|div\n"
        "x_sdk|%s|div\n"
        "x_reason|%s|div\n",
        String(ESP.getChipId(), HEX).c_str(),
        ESP.getCpuFreqMHz(),
        ESP.getSdkVersion(),
        getResetReason().c_str()
    );
    request->send(200, "text/plain", buffer);
#endif
#if defined(ESP32)
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
	core_wifi.wifiStatus = FS_STAT_RESET;
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
	JsonDocument doc;
	core_json.jsonFileLoadDoc(SECRET_FILE, doc);
	doc["auth"] = _httpAuth.auth;
	doc["user"] = _httpAuth.wwwUsername;
	doc["pass"] = _httpAuth.wwwPassword;
	return core_json.jsonFileSaveDoc(SECRET_FILE, doc);
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
	
	// Сброс watchdog перед операциями LittleFS (могут быть медленными)
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
	ESP.wdtFeed();
#endif

#if GZIP_ENABLED
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) { path += ".gz"; }
#else
	if (_fs->exists(path)) {
#endif
		DEBUGEDIT("Content type: %s\r\n", contentType.c_str());
		
		// Сброс watchdog после exists() и перед beginResponse()
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    	ESP.wdtFeed();
#endif
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
#if GZIP_ENABLED
		if (path.endsWith(".gz")) {response->addHeader("Content-Encoding", "gzip");}
#endif
		
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
	request->send(200, "text/plain", values);
}


void AsyncFSWebServer::html_system_Save(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	if (request->args() > 0) { // Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "name") 		{ _sysConfig.deviceName 	= urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "serial") 	{ _sysConfig.deviceSerial 	= urldecode(request->arg(i));	continue; }
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
		DBG_MOD("[C_HTTP] ", "%s\r\n", request->url().c_str());
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
	//use it to load content from LittleFS
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
			// Сначала пробуем отдать кастомную 404.html из файловой системы
			if (!this->handleFileRead("/404.html", request)) {
				AsyncWebServerResponse *response = request->beginResponse(404, "text/plain", "FileNotFound");
				response->addHeader("Connection", "close");
				response->addHeader("Access-Control-Allow-Origin", "*");
				request->send(response);
			}
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
	DBG_MOD("[C_HTTP] ", "\n\r\t\t**About** \n\r ");
	DBG_MOD("[C_HTTP] ", "Project env: %s\n\r ", BUILD_ENV);	
	DBG_MOD("[C_HTTP] ", "git branch: %s\n\r ", GIT_BRANCH);	
	DBG_MOD("[C_HTTP] ", "ver date: %s\n\r ", BUILD_TIME);	
	DBG_MOD("[C_HTTP] ", "Fiemware ver: %s\n\r ", String(VERSION_BUILD).c_str());
	DBG_MOD("[C_HTTP] ", "File system ver: %s\n\r ", getFsVersionStr().c_str());
	
	DBG_MOD("[C_HTTP] ", "Device serial number: %s\n\r ", _sysConfig.deviceSerial.c_str());	
	#if defined(ESP32)
	DBG_MOD("[C_HTTP] ", "Flash chip size: %u\r\n", ESP.getFlashChipSize());
	#endif
	#if ESP8266
	DBG_MOD("[C_HTTP] ", "Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
	#endif
	DBG_MOD("[C_HTTP] ", "Scketch size: %u\r\n", 		ESP.getSketchSize());
	if (_fs) {
#if defined(ESP32)
		DBG_MOD("[C_HTTP] ", "FS total: %u\r\n", 		_fs->totalBytes());
		DBG_MOD("[C_HTTP] ", "FS used: %u\r\n", 		_fs->usedBytes());
		DBG_MOD("[C_HTTP] ", "FS free: %u\r\n", 		_fs->totalBytes() - _fs->usedBytes());
#endif
#if defined(ESP8266)
		FSInfo fs_info;
		if (_fs->info(fs_info)) {
			DBG_MOD("[C_HTTP] ", "FS total: %u\r\n", 		fs_info.totalBytes);
			DBG_MOD("[C_HTTP] ", "FS used: %u\r\n", 		fs_info.usedBytes);
			DBG_MOD("[C_HTTP] ", "FS free: %u\r\n", 		fs_info.totalBytes - fs_info.usedBytes);
		}
#endif
	}

	DBG_MOD("[C_HTTP] ", "wifi ssid: %s \n", WiFi.SSID().c_str());
	DBG_MOD("[C_HTTP] ", "IP Address: %s \n", WiFi.localIP().toString().c_str());
	#if defined(ESP32)
    DBG_MOD("[C_HTTP] ", "WifiHostName  %s \n\r", 	WiFi.getHostname());
    #elif defined(ESP8266)
	DBG_MOD("[C_HTTP] ", "WifiHostName  %s \n\r", 	WiFi.hostname().c_str());
    #endif
	
	DBG_MOD("[C_HTTP] ", "Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
	DBG_MOD("[C_HTTP] ", "DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
	DBG_MOD("[C_HTTP] ", "local DNS hostname  http://%s.local \n\r", getHostName().c_str());
	DBG_MOD("[C_HTTP] ", "or you can connect directly  http://%s \n\r", WiFi.localIP().toString().c_str());
	
	
}

// *.html vvv
void AsyncFSWebServer::html_version_info(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "devicename|"  	+ _sysConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" 	+ _sysConfig.deviceSerial 		+ "|div\n";
	values += "versionapp|" 	+ String(FIRMWARE_VERSION) + "|div\n";
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
    
    String fullString;
    if (core_json.jsonParseNestedStr(jsonStr, "filesystem|version|full_string", fullString)) {
        _sysConfig.fsVersion = fullString;
        DEBUGLOG("getFsVersionStr: FS version = %s\n", _sysConfig.fsVersion.c_str());
        return _sysConfig.fsVersion;
    }
    
    DEBUGLOG("getFsVersionStr: filesystem.version.full_string not found in JSON\n");
    return "";
}

bool AsyncFSWebServer::load_config_Sys() {
	JsonDocument doc;
	if (!core_json.jsonFileLoadDoc(CONFIG_FILE_SYS, doc)) return false;
	_sysConfig.deviceName = doc["deviceName"].as<String>();
	_sysConfig.deviceSerial = doc["deviceSerial"].as<String>();
	return true;
}

void AsyncFSWebServer::defaultConfigSys() {
	// DEFAULT CONFIG SYSTEM
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
	JsonDocument doc;
	core_json.jsonFileLoadDoc(CONFIG_FILE_SYS, doc);
	doc["deviceName"] = _sysConfig.deviceName;
	doc["deviceSerial"] = _sysConfig.deviceSerial;
	return core_json.jsonFileSaveDoc(CONFIG_FILE_SYS, doc);
}