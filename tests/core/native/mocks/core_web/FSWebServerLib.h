#ifndef _MOCK_FSWEBSERVERLIB_H
#define _MOCK_FSWEBSERVERLIB_H

// Хост-заглушка core_web/FSWebServerLib.h. Заменяет реальный заголовок
// в native-тестах (include_dir моков имеет приоритет над src_dir).

#include "Arduino.h"
#include "IPAddress.h"
#include "ESPAsyncWebServer.h"

#define HTML_INDEX  "index.html"

class AsyncFSWebServer {
public:
    explicit AsyncFSWebServer(uint16_t port = 80) { (void)port; }

    bool checkAuth(AsyncWebServerRequest*) { return true; }
    bool handleFileRead(String path, AsyncWebServerRequest*) { (void)path; return false; }
    void restart_esp() {}
    const String getHostName() { return String(); }
    void serialShowAbout() {}
    String getResetReason() { return String("MOCK"); }
    String getFsVersionStr() { return String("0.000.00000000_0000.0000"); }

    template <typename F> void on(const char* url, HTTPMethod method, F handler) {
        (void)url; (void)method; (void)handler;
    }
    template <typename F> void on(const char* url, F handler) { (void)url; (void)handler; }
    template <typename F> void on(const String& url, F handler) { (void)url; (void)handler; }

    void begin(void*) {}
    void begin() {}
    void addHandler(void*) {}
    void onNotFound(std::function<void(AsyncWebServerRequest*)>) {}
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _MOCK_FSWEBSERVERLIB_H
