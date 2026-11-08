#ifndef _CORE_TERMINAL_H
#define _CORE_TERMINAL_H

#include <Arduino.h>

#include "ErriezSerialTerminal.h"
#include "core_terminal_types.h"

extern SerialTerminal term;

void TerminalInit();
void TerminalLoop();

// Регистрация модуля: component — имя компонента, name — команда верхнего уровня.
void TerminalRegisterModule(const char* component, const char* name, const char* desc,
                            TerminalCmdFn handler, const char* const* commands,
                            const char* helpText, bool editable);
// Скрытая команда/алиас (не попадает в общий help).
void TerminalRegisterAlias(const char* name, TerminalCmdFn handler);
// Есть ли у компонента терминальные команды (для stat).
bool TerminalHasModule(const char* component);
uint8_t TerminalModuleCount();
const TerminalModuleDesc* TerminalModuleAt(uint8_t index);

// Подтверждение действия через скрытые y/n с таймаутом.
void TerminalConfirm(const char* prompt, TerminalCmdFn onYes);
void TerminalCancelConfirm();

// Справка.
void TerminalPrintHelp();
void TerminalPrintModuleHelp(const char* name);
// Печать текста, который может лежать в PROGMEM (ESP8266).
void TerminalPrintText(const char* text);

// Остаток строки после последнего getNext(): trim и снятие кавычек.
String TerminalGetArgLine();

// Встроенные команды (реализация — core_terminal_engine.cpp).
void TerminalHelpCmd();
void TerminalEchoCmd();
void TerminalDefaultCmd(const char* command);

#endif // _CORE_TERMINAL_H
