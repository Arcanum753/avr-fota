#ifndef _MODULE_MACROS_ENGINE_h
#define _MODULE_MACROS_ENGINE_h

// ============================================================
// module_macros_engine.h — данные и интерфейс исполнительной машины
// сценариев (Tcl-интерпретатор pTcl + cron/ccronexpr).
// Реализация: module_macros_engine.cpp
// ============================================================

#include "main.h"

#include "tcl.h"
#include "ccronexpr.h"

// Ограничения прототипа (ESP8266 RAM ограничен — лимиты ниже)
#if defined(ESP32)
#define MACRO_MAX_FILES       16     ///< максимум файлов-сценариев в списке
#define MACRO_MAX_ENTS        24     ///< максимум сущностей в одном файле
#elif defined(ESP8266)
#define MACRO_MAX_FILES       8      ///< максимум файлов-сценариев в списке
#define MACRO_MAX_ENTS        8      ///< максимум сущностей в одном файле
#endif
#define MACRO_EV_QUEUE        8      ///< размер очереди внешних событий (term/button)

/// Типы сущностей сценария
#define MACRO_ENT_CRON        0      ///< «момент времени» по cron-выражению
#define MACRO_ENT_COND        1      ///< «условие» по фронту false->true
#define MACRO_ENT_BUTTON      2      ///< событие с веб-страницы / внешнего вызова (macro btn)
#define MACRO_ENT_TERM        3      ///< событие из терминала (macro msg)

/// Внешнее событие (очередь term/button)
typedef struct {
    uint8_t type;            ///< MACRO_ENT_BUTTON или MACRO_ENT_TERM
    String  spec;            ///< спецификатор (может содержать несколько слов-параметров)
} MacroEvent;

struct MacroFile;

/// Контекст разбора/исполнения Tcl-файла. Передаётся как arg в tcl_register.
typedef struct {
    struct MacroFile* file;  ///< файл, которому принадлежит интерпретатор
    bool parsing;            ///< true — идёт разбор файла (регистрация сущностей)
} PtclMacroCtx;

/// Одна сущность сценария (строка «таблицы условий и моментов времени»)
typedef struct {
    uint8_t   type;          ///< MACRO_ENT_*
    String    spec;          ///< cron-выражение / Tcl-условие / токен button|term
    String    body;          ///< тело на Tcl
    cron_expr expr;          ///< разобранное cron-выражение (для MACRO_ENT_CRON)
    time_t    next;          ///< следующее срабатывание cron (0 — не инициализировано)
    bool      lastCond;      ///< предыдущее состояние условия (для MACRO_ENT_COND)
} MacroEntity;

/// Файл-сценарий: метаданные (сохраняются в JSON) + runtime-состояние
typedef struct MacroFile {
    // --- метаданные (config_macros.json) ---
    String    name;          ///< полный путь: /macros/xxx.tcl
    uint8_t   prio;          ///< приоритет 0..7 (0 — высший)
    bool      run;           ///< включён пользователем
    uint32_t  created;       ///< время создания (локальное, TimeLib)

    // --- runtime-состояние (не сохраняется) ---
    bool      active;        ///< файл запущен и успешно разобран
    String    err;           ///< текст последней ошибки (пусто — ошибок нет)
    struct tcl* tcl;         ///< интерпретатор файла (только когда active)
    PtclMacroCtx ctx;        ///< контекст для команд Tcl (ctx.file указывает на этот файл)
    MacroEntity ents[MACRO_MAX_ENTS];
    uint8_t   nEnts;         ///< число сущностей в ents
} MacroFile;

/// Структура конфига — сохраняется в config_macros.json
typedef struct {
    bool enabled;            ///< модуль включён
} strMacrosConfig;

/**
 * Выполняет Tcl-скрипт по командам верхнего уровня (используется tcl.c
 * для тел пользовательских proc). Реализовано в module_macros_engine.cpp.
 * \param t интерпретатор pTcl
 * \param src null-терминированный текст сценария
 * \return FNORMAL при успехе либо код потока управления/FERROR
 */
extern "C" int macroTclEvalScript(struct tcl* t, const char* src);

#endif // _MODULE_MACROS_ENGINE_h
