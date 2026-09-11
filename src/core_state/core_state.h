#ifndef _CORE_STATE_H
#define _CORE_STATE_H

#include "main.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "mod_context.h"

#include "core_web/FSWebServerLib.h"

#if defined(DEBUG_STATE)
#define DEBUGSTATE(...) DBG_MOD("[C_STATE] ", __VA_ARGS__)
#endif
#if !defined(DEBUG_STATE)
#define DEBUGSTATE(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif

#define CONFIG_FILE_STATE            "/config_state.json"

#define CORE_STATE_MAX_RES           64
#define CORE_STATE_MAX_NS            16
#define CORE_STATE_MAX_SUBS          16
#define CORE_STATE_MAX_ASYNC         6
#define CORE_STATE_MAX_CODES         8
#define CORE_STATE_MAX_ENUM          8
#define CORE_STATE_MAX_ARGS          4
#define CORE_STATE_EV_QUEUE          16
#define CORE_STATE_NAME_LEN          56
#define CORE_STATE_NS_LEN            24
#define CORE_STATE_FIELD_LEN         32
#define CORE_STATE_NS_NAME_LEN       24

// ============================================================
// Коды возврата шины (отрицательные — ошибки ядра)
// ============================================================
#define BUS_OK                  0
#define BUS_ERR_NOT_REGISTERED  (-1)   // модуль отсутствует в сборке
#define BUS_ERR_NOT_FOUND       (-2)   // имя ресурса не зарегистрировано
#define BUS_ERR_BAD_TYPE        (-3)   // не тот тип аргумента
#define BUS_ERR_BAD_ARGC        (-4)   // неверное число аргументов
#define BUS_ERR_BAD_VALUE       (-5)   // значение вне диапазона
#define BUS_ERR_READONLY        (-6)   // попытка записи в read-only
#define BUS_ERR_DISABLED        (-7)   // модуль в режиме off
#define BUS_ERR_BUSY            (-8)   // модуль занят
#define BUS_ERR_NOT_READY       (-9)   // не выполнено предусловие (нет сети и т.п.)
#define BUS_ERR_TIMEOUT         (-10)  // операция не завершилась
#define BUS_ERR_INTERNAL        (-11)  // внутренняя ошибка
#define BUS_ERR_NOT_SUPPORTED   (-12)  // недоступно в этом режиме
#define BUS_ERR_DENIED          (-13)  // нет прав

// ============================================================
// Значение ресурса
// ============================================================
struct BusValue {
    enum Kind : uint8_t { NONE = 0, BOOL, I32, F32, STR, TIME, ENUM };

    Kind    kind = NONE;
    union {
        bool    b;
        int32_t i;
        float   f;
        int64_t t;
    };
    String  s;

    BusValue() { i = 0; }

    static BusValue bo(bool v)      { BusValue r; r.kind = BOOL; r.b = v; return r; }
    static BusValue i32(int32_t v)  { BusValue r; r.kind = I32;  r.i = v; return r; }
    static BusValue f32(float v)    { BusValue r; r.kind = F32;  r.f = v; return r; }
    static BusValue str(const String& v) { BusValue r; r.kind = STR; r.s = v; return r; }
    static BusValue tm(int64_t v)   { BusValue r; r.kind = TIME; r.t = v; return r; }
    static BusValue en(int32_t v)   { BusValue r; r.kind = ENUM; r.i = v; return r; }
};

// ============================================================
// Метаданные ресурса
// ============================================================
struct BusResInfo {
    const char*      name      = nullptr;
    BusValue::Kind   kind      = BusValue::NONE;
    const char*      desc      = nullptr;
    bool             writable  = false;
    bool             isEvent   = false;
    bool             isFunc    = false;
    bool             async     = false;
    uint32_t         timeoutMs = 0;
    const char*      enumVals[CORE_STATE_MAX_ENUM] = { nullptr };
    uint8_t          enumCount = 0;
};

// Сигнатура sync-функции/подписчика: user, argc, argv, результат.
typedef int (*BusCb)(void* user, int argc, const BusValue* argv, BusValue& result);
// Сигнатура async-функции: user, handle, argc, argv.
typedef int (*BusAsyncCb)(void* user, uint32_t handle, int argc, const BusValue* argv);

// ============================================================
// Режимы ядра
// ============================================================
enum CoreMode : uint8_t {
    CORE_MODE_INIT = 0,     // старт, восстановление
    CORE_MODE_NORMAL,       // штатная работа
    CORE_MODE_OTA,          // длительная операция: обновление
    CORE_MODE_FS_UPDATE,    // длительная операция: обновление FS
    CORE_MODE_PROG,         // штатный рабочий режим программатора
    CORE_MODE_TEST,         // ручной режим с таймаутом
    CORE_MODE_COUNT
};

// ============================================================
// Ядро ресурсной шины: реестр ресурсов, pull-чтение, push-события,
// async-вызовы и режимы системы.
// ============================================================
class CLASS_CORE_STATE {
public:
    void begin(ModContext& ctx);
    void register_resources();
    void web_Init();
    void loop();

    // --- Регистрация ресурсов ---
    bool regState  (const char* name, BusValue::Kind kind, const char* desc, bool writable);
    bool regStateAs(const char* full_name, BusValue::Kind kind, const char* desc, bool writable);
    bool regEnum   (const char* name, int count, const char* const* values, const char* desc);
    bool regEvent  (const char* name, const char* desc);
    bool regFunc   (const char* name, const char* sig, const char* desc, BusCb fn, void* user);
    bool regFuncAsync(const char* name, const char* sig, const char* desc, BusAsyncCb fn,
                      void* user, uint32_t timeout_ms = 10000);
    bool regFuncCode(const char* func_name, int code, const char* meaning);

    // --- Контекст регистрации/вызова ---
    void setNamespace(const char* ns);
    void clearNamespace();
    void setPrivileged(bool on);

    // --- Чтение значений (pull) ---
    bool   getBool (const char* name, bool def = false);
    int32_t getInt (const char* name, int32_t def = 0);
    float  getF32  (const char* name, float def = 0.0f);
    String getStr  (const char* name, const String& def = "");
    int64_t getTime(const char* name, int64_t def = 0);
    bool   has     (const char* name);
    int    info    (const char* name, BusResInfo& out);
    int    mode    (const char* module_namespace, int new_mode);

    // --- Запись значений (push: эмитит "state.<full_name>") ---
    int setBool (const char* name, bool value);
    int setInt  (const char* name, int32_t value);
    int setF32  (const char* name, float value);
    int setStr  (const char* name, const String& value);
    int setTime (const char* name, int64_t value);

    // --- События ---
    uint32_t on  (const char* evt, BusCb cb, void* user);
    void     off (uint32_t handle);
    void     emit(const char* evt, const BusValue& v = BusValue());
    int      signal(const char* sig, const BusValue& v);

    // --- Вызовы функций ---
    int  call      (const char* name, int argc, BusValue args[], BusValue& result);
    int  call_async(const char* name, BusCb cb, void* user, int argc, BusValue args[]);
    void cancel_async(int handle);
    void asyncComplete(uint32_t handle, int rc, const BusValue& result = BusValue());
    void setAsyncOwner(void* ownerToken);
    void asyncCancelFor(void* ownerToken);

    // --- Режимы ---
    int setMode(int m);
    int getMode();
    int requestMode(int m);
    int requestMode(int m, uint8_t prio);

    // --- Каталог для UI ---
    void catalogToJson(JsonDocument& doc);

private:
    // Версия
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void   html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleCatalog(AsyncWebServerRequest *request);
    void handleInfo(AsyncWebServerRequest *request);
    void handleSet(AsyncWebServerRequest *request);
    void handleCall(AsyncWebServerRequest *request);

    // Конфиг
    void defaultConfigState();
    bool loadConfigState();
    bool saveConfigState();

    // Внутренняя логика
    int  findRes(const char* name);
    int  findFunc(const char* name);
    int  findNs(const char* ns, bool create);
    void makeFullName(char* out, const char* name);
    BusValue valueToKind(const BusValue& v, BusValue::Kind k);
    int  writeRes(int idx, const BusValue& v, bool checkAccess);
    void queueEvent(const char* evt, const BusValue& v);
    void dispatchEvents();
    void processAsyncTimeouts();
    void processModeTimeouts();

protected:
#if defined(ESP32)
    fs::LittleFSFS*             _fs = nullptr;
#endif
#if defined(ESP8266)
    FS*                         _fs = nullptr;
#endif

    struct BusRes {
        char            name[CORE_STATE_NAME_LEN]  = { 0 };
        char            ns[CORE_STATE_NS_LEN]      = { 0 };
        char            field[CORE_STATE_FIELD_LEN] = { 0 };
        BusValue::Kind  kind      = BusValue::NONE;
        const char*     desc      = nullptr;
        bool            writable  = false;
        bool            isEvent   = false;
        bool            isFunc    = false;
        bool            async     = false;
        BusCb           fn        = nullptr;
        BusAsyncCb      afn       = nullptr;
        void*           user      = nullptr;
        uint32_t        timeoutMs = 0;
        uint8_t         ownerId   = 0;
        const char*     enumVals[CORE_STATE_MAX_ENUM] = { nullptr };
        uint8_t         enumCount = 0;
        struct { int code; const char* meaning; } codes[CORE_STATE_MAX_CODES];
        uint8_t         codeCount = 0;
        BusValue        value;
    };

    struct BusSub {
        bool     used = false;
        char     evt[CORE_STATE_NAME_LEN] = { 0 };
        BusCb    cb   = nullptr;
        void*    user = nullptr;
        uint32_t handle = 0;
    };

    struct BusAsync {
        bool         used    = false;
        uint32_t     handle  = 0;
        BusAsyncCb   fn      = nullptr;
        void*        user    = nullptr;
        BusCb        cb      = nullptr;
        void*        cbUser  = nullptr;
        uint32_t     startMs = 0;
        uint32_t     timeoutMs = 0;
        void*        owner   = nullptr;
        int          argc    = 0;
        BusValue     args[CORE_STATE_MAX_ARGS];
    };

    struct BusEv {
        char     evt[CORE_STATE_NAME_LEN] = { 0 };
        BusValue v;
    };

    struct BusDone {
        bool     used = false;
        uint32_t handle = 0;
        int      rc = 0;
        BusValue result;
        BusCb    cb = nullptr;
        void*    cbUser = nullptr;
    };

    struct BusNs {
        char     name[CORE_STATE_NS_NAME_LEN] = { 0 };
        uint8_t  id = 0;
        bool     privileged = false;
    };

    BusRes      _res[CORE_STATE_MAX_RES];
    uint16_t    _resCount = 0;

    BusSub      _subs[CORE_STATE_MAX_SUBS];
    uint32_t    _subNextHandle = 1;

    BusAsync    _async[CORE_STATE_MAX_ASYNC];
    uint32_t    _asyncNextHandle = 1;
    void*       _asyncOwner = nullptr;

    BusEv       _evQueue[CORE_STATE_EV_QUEUE];
    uint8_t     _evHead = 0;
    uint8_t     _evTail = 0;
    uint8_t     _evCount = 0;

    BusDone     _doneQueue[CORE_STATE_MAX_ASYNC];
    uint8_t     _doneCount = 0;

    BusNs       _ns[CORE_STATE_MAX_NS];
    uint8_t     _nsCount = 0;
    int8_t      _currentNs = -1;      // namespace регистрации/вызова, -1 = нет
    bool        _privileged = false;

    // Режимы
    uint8_t     _mode = CORE_MODE_INIT;
    bool        _safe = false;
    bool        _idle = false;
    uint32_t    _modeSinceMs = 0;
    uint32_t    _opTimeoutMs = 0;     // абсолютный таймаут длительной операции
    bool        _opHasTimeout = false;
    uint8_t     _opPrio = 50;

    // Конфиг
    uint32_t    _testTimeoutMs = 1800000UL;   // 30 мин
    uint32_t    _opMaxTimeoutMs = 1800000UL;  // 30 мин
};

extern CLASS_CORE_STATE core_state;

#endif // _CORE_STATE_H
