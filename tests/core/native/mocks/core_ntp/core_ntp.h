#ifndef _MOCK_CORE_NTP_H
#define _MOCK_CORE_NTP_H

// Хост-заглушка core_ntp/core_ntp.h (для тестов core_wifi).

#include "Arduino.h"

class CLASS_CORE_NTP {
public:
    void begin() {}
    void begin(void*) {}
    void ntpOnConnected() { _connected++; }
    void ntpOnDisconected() { _disconnected++; }
    void sendTimeData() {}
    int  connectedCount() const { return _connected; }
    int  disconnectedCount() const { return _disconnected; }
private:
    int _connected = 0;
    int _disconnected = 0;
};

// Глобальный объект ядра: объявление для engine-файлов; определяется в тесте.
extern CLASS_CORE_NTP core_ntp;

#endif // _MOCK_CORE_NTP_H
