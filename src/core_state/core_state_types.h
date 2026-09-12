#ifndef _CORE_STATE_TYPES_h
#define _CORE_STATE_TYPES_h

// ============================================================
// core_state_types.h — типы, структуры и define'ы ядра core_state.
// Реализация: core_state.cpp (шаблон) и core_state_engine.cpp
// (реестр, события, async, режимы).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

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

#endif // _CORE_STATE_TYPES_h
