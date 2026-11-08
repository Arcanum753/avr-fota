#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

#include "core_terminal/core_terminal.h"

// Newline character '\r' or '\n'
char newlineChar = '\r';
// Separator character between commands and arguments
char delimiterChar = ' ';

SerialTerminal term(newlineChar, delimiterChar);

// ============================================================
// Реестр модулей терминала
// ============================================================

// Дескрипторы в heap: фиксированный массив на 24 записи не влезает в DRAM ESP8266.
static TerminalModuleDesc* _modules = nullptr;
static uint8_t             _moduleCount = 0;

void TerminalRegisterModule(const char* component, const char* name, const char* desc,
                            TerminalCmdFn handler, const char* const* commands,
                            const char* helpText, bool editable) {
    if (component == nullptr || name == nullptr || handler == nullptr) { return; }
    if (_moduleCount >= TERMINAL_MAX_MODULES) {
        Serial.printf("Terminal: module slots full, '%s' ignored\r\n", name);
        return;
    }
    for (uint8_t i = 0; i < _moduleCount; i++) {
        if (strcmp(_modules[i].name, name) == 0) { return; }
    }

    TerminalModuleDesc* grown = (TerminalModuleDesc*)realloc(
        _modules, sizeof(TerminalModuleDesc) * (size_t)(_moduleCount + 1));
    if (grown == nullptr) {
        Serial.printf("Terminal: out of memory, '%s' ignored\r\n", name);
        return;
    }
    _modules = grown;

    TerminalModuleDesc& d = _modules[_moduleCount++];
    d.component = component;
    d.name      = name;
    d.desc      = (desc != nullptr) ? desc : "";
    d.handler   = handler;
    d.commands  = commands;
    d.helpText  = (helpText != nullptr) ? helpText : "";
    d.editable  = editable;

    // Регистрируем команду сразу: модули могут вызывать это из begin()
    // в любом порядке относительно TerminalInit().
    term.addCommand(name, handler);
}

void TerminalRegisterAlias(const char* name, TerminalCmdFn handler) {
    if (name == nullptr || handler == nullptr) { return; }
    term.addCommand(name, handler);
}

uint8_t TerminalModuleCount() { return _moduleCount; }

const TerminalModuleDesc* TerminalModuleAt(uint8_t index) {
    if (index >= _moduleCount) { return nullptr; }
    return &_modules[index];
}

bool TerminalHasModule(const char* component) {
    if (component == nullptr) { return false; }
    if (strcmp(component, "core_terminal") == 0) { return true; }
    for (uint8_t i = 0; i < _moduleCount; i++) {
        if (strcmp(_modules[i].component, component) == 0) { return true; }
    }
    return false;
}

// ============================================================
// Подтверждение действий (y/n)
// ============================================================

static TerminalCmdFn _confirmCb       = nullptr;
static uint32_t       _confirmDeadline = 0;

void TerminalConfirm(const char* prompt, TerminalCmdFn onYes) {
    _confirmCb = onYes;
    _confirmDeadline = millis() + TERMINAL_CONFIRM_TIMEOUT_MS;
    Serial.printf("%s (y/N): ", (prompt != nullptr) ? prompt : "Confirm?");
}

void TerminalCancelConfirm() {
    _confirmCb = nullptr;
}

static void TerminalConfirmYesCmd() {
    if (_confirmCb == nullptr) {
        Serial.println("No confirmation pending.");
        return;
    }
    TerminalCmdFn cb = _confirmCb;
    _confirmCb = nullptr;
    cb();
}

static void TerminalConfirmNoCmd() {
    if (_confirmCb == nullptr) {
        Serial.println("No confirmation pending.");
        return;
    }
    _confirmCb = nullptr;
    Serial.println("Cancelled.");
}

static void TerminalConfirmTick() {
    if (_confirmCb == nullptr) { return; }
    if ((int32_t)(millis() - _confirmDeadline) >= 0) {
        _confirmCb = nullptr;
        Serial.println("\r\nConfirmation timed out.");
    }
}

// Любая команда, кроме y/n, отменяет ожидание подтверждения.
static void TerminalPreCommand(const char* command) {
    if (_confirmCb == nullptr || command == nullptr) { return; }
    if (strcmp(command, "y") == 0 || strcmp(command, "n") == 0) { return; }
    _confirmCb = nullptr;
    Serial.println("\r\nConfirmation cancelled.");
}

// ============================================================
// Инициализация и главный цикл
// ============================================================

void TerminalInit() {
    term.addCommand("help", TerminalHelpCmd);
    term.addCommand("echo", TerminalEchoCmd);
    term.addCommand("y",    TerminalConfirmYesCmd);
    term.addCommand("n",    TerminalConfirmNoCmd);

    term.setPreCommandHandler(TerminalPreCommand);
    term.setDefaultHandler(TerminalDefaultCmd);
    term.setSerialEcho(true);

    Serial.println("\r\nSerial terminal ready. Type 'help'.");
}

void TerminalLoop() {
    TerminalConfirmTick();
    term.readSerial();
}
