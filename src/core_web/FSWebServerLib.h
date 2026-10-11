// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h



#include "main.h"

#include <WiFiClient.h>
#include "common/TimeLib.h"

#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <LittleFS.h>
#endif

#if defined(ESP8266)
#include <LittleFS.h>
#endif

#include <Ticker.h>

#if defined(ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// Глобальный рекурсивный мьютекс «системных» операций. Сериализует веб-обработчики
// (задача AsyncTCP) и задачи главного цикла (тик макросов/Lua, терминал, …).
// Устраняет гонки за heap/Lua/ресурсную шину, из-за которых ESP32 падал при
// одновременной работе страницы и тика сценариев.
SemaphoreHandle_t systemLock();

struct SysLockGuard {
    SemaphoreHandle_t m;
    SysLockGuard() : m(systemLock()) { if (m != nullptr) { xSemaphoreTakeRecursive(m, portMAX_DELAY); } }
    ~SysLockGuard() { if (m != nullptr) { xSemaphoreGiveRecursive(m); } }
};
#else
// На ESP8266 веб-задача совмещена с главным циклом — сериализация не нужна.
struct SysLockGuard {
    SysLockGuard() {}
    ~SysLockGuard() {}
};
#endif


#define HTML_INDEX  "index.html"


class AsyncFSWebServer : public AsyncWebServer {
public:
    AsyncFSWebServer(uint16_t port);
#if ESP32
    void begin(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
#endif
	// Тонкие форвардеры к core_sys (совместимость публичного API).
	const String getHostName();
	   void serialShowAbout();
	   String getResetReason() ;
	   String getFsVersionStr();

    bool checkAuth(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    void restart_esp();

    // Обёртки маршрутов: каждый веб-обработчик берёт systemLock и потому не
    // выполняется одновременно с задачами главного цикла.
    AsyncCallbackWebHandler& on(const char* uri, ArRequestHandlerFunction onRequest);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload);
    AsyncCallbackWebHandler& on(const char* uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload, ArBodyHandlerFunction onBody);
    void onNotFound(ArRequestHandlerFunction fn);

protected:
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif
public:
    AsyncEventSource _evs = AsyncEventSource("/events");

private:
    void serverInit();
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h
