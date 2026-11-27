#ifndef _CORE_TERMINAL_TYPES_h
#define _CORE_TERMINAL_TYPES_h

// ============================================================
// core_terminal_types.h — типы и define'ы ядра core_terminal.
// Реализация: core_terminal.cpp (регистрация/цикл/подтверждения) и
// core_terminal_engine.cpp (help/echo).
// ============================================================

#include <Arduino.h>

typedef void (*TerminalCmdFn)(void);

// Описание модуля в терминале. component — имя компонента (core_sys и т.п.),
// name — команда верхнего уровня (sys/fs/ntp/wifi/led).
struct TerminalModuleDesc {
    const char*        component;
    const char*        name;
    const char*        desc;
    TerminalCmdFn      handler;
    const char* const* commands;
    const char*        helpText;
    bool               editable;
};

#define TERMINAL_MAX_MODULES        24
#define TERMINAL_CONFIRM_TIMEOUT_MS 30000

#endif // _CORE_TERMINAL_TYPES_h
