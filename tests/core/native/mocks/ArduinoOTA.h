#ifndef _MOCK_ARDUINOOTA_H
#define _MOCK_ARDUINOOTA_H

#include "Arduino.h"
#include <functional>

class MockArduinoOTA {
public:
    void setHostname(const char*) {}
    void setPassword(const char*) {}
    void setPort(uint16_t) {}
    void begin() {}
    void handle() {}

    template <typename F> void onStart(F) {}
    template <typename F> void onEnd(F) {}
    template <typename F> void onProgress(F) {}
    template <typename F> void onError(F) {}
};

extern MockArduinoOTA ArduinoOTA;

typedef int ota_error_t;
#define OTA_AUTH_ERROR 1
#define OTA_BEGIN_ERROR 2
#define OTA_CONNECT_ERROR 3
#define OTA_RECEIVE_ERROR 4
#define OTA_END_ERROR 5

#endif // _MOCK_ARDUINOOTA_H
