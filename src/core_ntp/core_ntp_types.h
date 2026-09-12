#ifndef _CORE_NTP_TYPES_h
#define _CORE_NTP_TYPES_h

// ============================================================
// core_ntp_types.h — типы, структуры и define'ы ядра core_ntp.
// Реализация: core_ntp.cpp (шаблон) и core_ntp_engine.cpp
// (логика синхронизации).
// ============================================================

#include <Arduino.h>

#define CONFIG_FILE_NTP "/config_ntp.json"


#define NTPSERVER_DFLT0 "pool.ntp.org";
#define NTPSERVER_DFLT1 "0.ru.pool.ntp.org";
#define NTPSERVER_DFLT2 "0.gentoo.pool.ntp.org";
#define NTP_TIMEOUT 5000 // milliseconds

typedef struct {
    String ntpServerName0;
    String ntpServerName1;
    String ntpServerName2;
    long updateNTPTimeEvery;
    long timezone;
    bool daylight;
} strNtpConfig;

#endif // _CORE_NTP_TYPES_h
