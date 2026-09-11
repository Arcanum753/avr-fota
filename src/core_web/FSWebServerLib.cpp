
#include "main.h"
#include "FSWebServerLib.h"
#include "common_module.h"


#if defined(ESP32)
#include <LittleFS.h>
#include <esp_task_wdt.h>
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
#include "core_wifi/core_wifi.h"

#include "modules_registry.h"

#include "core_sys/core_sys.h"

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

    if (!_fs) { _fs->begin();  }// If LittleFS is not started

	g_ctx.fs = fs;

	// Инициализация ядер/модулей/устройств через сгенерированный registry.
	// core_sys.begin() идёт первым (после core_json) и заполняет
	// g_ctx.hostname / g_ctx.password для core_wifi и mDNS.
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

// ============================================================
// Тонкие форвардеры к core_sys (совместимость публичного API)
// ============================================================

const String AsyncFSWebServer::getHostName() { return core_sys.getHostName(); }

void AsyncFSWebServer::serialShowAbout() { core_sys.serialShowAbout(); }

String AsyncFSWebServer::getResetReason() { return core_sys.getResetReason(); }

String AsyncFSWebServer::getFsVersionStr() { return core_sys.getFsVersionStr(); }

// ============================================================
// Web-инфраструктура
// ============================================================

void AsyncFSWebServer::restart_esp() {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	core_wifi.notifyRestart();
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

bool AsyncFSWebServer:: handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUGHTTP("handleFileRead: %s\r\n", path.c_str());
	// CANNOT RUN DELAY() INSIDE CALLBACK
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 1, 30); 	}	// Show activity on LED
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
	ESP.wdtFeed();
#endif
	if (path.endsWith("/")) {	path += HTML_INDEX;	}
	String contentType = ns_core_web::getContentType(path, request);
	
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
		DEBUGHTTP("Content type: %s\r\n", contentType.c_str());
		
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
		DEBUGHTTP("File %s exist\r\n", path.c_str());
		request->send(response);
		
		// Сброс watchdog после send()
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
#if defined(ESP8266)
    	ESP.wdtFeed();
#endif
		DEBUGHTTP("File %s Sent\r\n", path.c_str());
		return true;
	}
	else
		DEBUGHTTP("Cannot find %s\n", path.c_str());
	return false;
}

void AsyncFSWebServer::serverInit() {
	// Запрещаем Keep-Alive, чтобы браузер не держал открытые
	// TCP-соединения - они вызывали сброс ESP при длительном простое
	DefaultHeaders::Instance().addHeader("Connection", "close");

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
	return core_sys.checkAuth(request);
}
