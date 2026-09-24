#ifndef _MOCK_CORE_SYS_H
#define _MOCK_CORE_SYS_H

// Хост-заглушка core_sys/core_sys.h (для тестов core_wifi/core_ota).
// Заменяет реальный заголовок; используется вместе с определяемым в тесте
// глобальным объектом `core_sys`.

#include <stdint.h>
#include "Arduino.h"

class AsyncWebServerRequest;

class CLASS_CORE_SYS {
public:
    const String getHostName() { return String("mock-host"); }
    String getDeviceName() { return String("mock-device"); }
    void   setDeviceName(const String&) {}
    String getDeviceSerial() { return String("0001"); }
    void   setDeviceSerial(const String&) {}
    void   defaultConfigSys() {}
    bool   save_configSys() { return true; }
    bool   saveSysIdentStore() { return true; }

    String getResetReason() { return String("MOCK"); }
    void   serialShowAbout() {}
    String getFsVersionStr() { return String("0.001.20260101_0000.0001"); }
    void   invalidateFsVersionCache() {}
    bool   getFsVersion(int64_t& date, int32_t& build, int32_t& major, int32_t& minor) {
        date = 202601010000LL; build = 1; major = 0; minor = 1; return true;
    }

    bool   httpAuthEnabled() { return false; }
    String getHttpPassword() { return String(); }
    bool   checkAuth(AsyncWebServerRequest*) { return true; }

    void begin(void*) {}
};

// Глобальный объект ядра: объявление для engine-файлов; определяется в тесте.
extern CLASS_CORE_SYS core_sys;

#endif // _MOCK_CORE_SYS_H
