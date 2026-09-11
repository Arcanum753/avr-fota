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
