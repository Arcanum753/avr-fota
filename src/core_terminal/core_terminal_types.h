#ifndef _CORE_TERMINAL_TYPES_h
#define _CORE_TERMINAL_TYPES_h

// ============================================================
// core_terminal_types.h — типы и define'ы ядра core_terminal.
// Реализация: core_terminal.cpp (шаблон) и
// core_terminal_engine.cpp (реализации команд).
// ============================================================

#include <Arduino.h>

typedef void (*TerminalModuleInit)(void);

#define TERMINAL_MODULE_SLOTS 8

#endif // _CORE_TERMINAL_TYPES_h
