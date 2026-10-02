#ifndef _CORE_SYS_TYPES_h
#define _CORE_SYS_TYPES_h

// ============================================================
// core_sys_types.h — типы, структуры и define'ы ядра core_sys.
// Реализация: core_sys.cpp (шаблон), core_sys_engine.cpp
// (информация о системе + чтение FS-версии), core_sys_time.cpp
// (подсистема виртуального времени).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#include "common/TimeLib.h"

#define CONFIG_FILE_SYS             "/config_sys.json"
#define SECRET_FILE                 "/secret.json"

#define FS_VERSION_JSON_PATH        "/_version_fs.json"

// ============================================================
// Подсистема виртуального времени (Time Source Provider API)
// ============================================================

#define CONFIG_FILE_TIME            "/config_time.json"
// Legacy-конфиг NTP: источник TZ/DST для однократной миграции.
#define TIME_LEGACY_NTP_CFG         "/config_ntp.json"

#define CORE_SYS_TIME_MAX_SOURCES   6
#define CORE_SYS_TIME_NAME_LEN      16
#define CORE_SYS_TIME_NTP_STALE_S   1800UL        // 30 минут
#define CORE_SYS_TIME_MIN_VALID     1577836800L   // 2020-01-01
#define CORE_SYS_TIME_BACKJUMP_GRACE_S  600UL
#define CORE_SYS_TIME_BACKJUMP_MAX_S    2L

// Прочитать время источника.
//   true  — время достоверно, out заполнен;
//   false — не готов / нет данных / время недостоверно.
typedef bool (*TimeGetFn)(time_t& out);

// Записать время в источник.
//   true  — запись успешна;
//   false — не поддерживается или ошибка I2C.
// Опционален: read-only источник (NTP) — nullptr.
typedef bool (*TimeSetFn)(time_t in);

// Человекочитаемая причина невалидности (для UI и логов).
//   "" / nullptr — источник ok; иначе — короткая строка-причина.
typedef const char* (*TimeStatusFn)();

struct strTimeSource {
    char         name[CORE_SYS_TIME_NAME_LEN];
    int32_t      prio;         // больше = важнее
    bool         enabled;
    TimeGetFn    get;
    TimeSetFn    set;
    TimeStatusFn status;
    bool         validLast;
    const char*  lastReason;   // причина последнего отказа ("" / nullptr = ok)
    uint32_t     lastOkMs;
    bool         registered;
};

typedef struct {
    String deviceName;
    String deviceSerial;
} strSysConfig;

typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
    String wwwQuestion;
    String wwwAnswer;
} strHTTPAuth;

#endif // _CORE_SYS_TYPES_h
