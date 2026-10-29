#ifndef _CORE_NTP_TYPES_h
#define _CORE_NTP_TYPES_h

// ============================================================
// core_ntp_types.h — типы, структуры и define'ы ядра core_ntp.
// Реализация: core_ntp.cpp (шаблон) и core_ntp_engine.cpp
// (логика синхронизации).
// ============================================================

#include <Arduino.h>

#define CONFIG_FILE_NTP "/config_ntp.json"

// Ограничения конфига и библиотеки NTP.
#define NTP_SERVER_COUNT        3     // основной сервер + два запасных
#define NTP_SERVER_NAME_MAX     39    // SERVER_NAME_LENGTH=40; toCharArray сохраняет 39 символов
#define NTP_PERIOD_MAX_MIN      720   // верхняя граница периода опроса, мин
#define NTP_PERIOD_DEFAULT_MIN  15    // дефолт (как в поставляемом config_ntp.json)

#define NTPSERVER_DFLT0 "pool.ntp.org"
#define NTPSERVER_DFLT1 "1.ru.pool.ntp.org"
#define NTPSERVER_DFLT2 "0.gentoo.pool.ntp.org"
#define NTP_TIMEOUT 5000 // milliseconds

typedef struct {
    String ntpServerName0;
    String ntpServerName1;
    String ntpServerName2;
    long updateNTPTimeEvery;
} strNtpConfig;

#endif // _CORE_NTP_TYPES_h
