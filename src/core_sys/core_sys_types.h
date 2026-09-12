#ifndef _CORE_SYS_TYPES_h
#define _CORE_SYS_TYPES_h

// ============================================================
// core_sys_types.h — типы, структуры и define'ы ядра core_sys.
// Реализация: core_sys.cpp (шаблон) и core_sys_engine.cpp
// (информация о системе + чтение FS-версии).
// ============================================================

#include <Arduino.h>

#define CONFIG_FILE_SYS             "/config_sys.json"
#define SECRET_FILE                 "/secret.json"

#define FS_VERSION_JSON_PATH        "/_version_fs.json"

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
