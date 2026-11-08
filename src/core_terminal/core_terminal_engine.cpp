#include <Arduino.h>
#include <string.h>

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Встроенные команды терминала
// ============================================================

String TerminalGetArgLine() {
    char* rest = term.getRemaining();
    if (rest == nullptr) { return String(""); }
    return ns_core_terminal::unquote(String(rest));
}

static void printModuleCommands(const TerminalModuleDesc& d) {
    if (d.commands == nullptr) {
        Serial.print("-");
        return;
    }
    bool first = true;
    for (uint8_t i = 0; d.commands[i] != nullptr; i++) {
        if (!first) { Serial.print(" "); }
        Serial.print(d.commands[i]);
        first = false;
    }
    if (first) { Serial.print("-"); }
}

void TerminalPrintHelp() {
    // Алфавитная сортировка индексов (модулей немного).
    uint8_t order[TERMINAL_MAX_MODULES];
    uint8_t n = TerminalModuleCount();
    for (uint8_t i = 0; i < n; i++) { order[i] = i; }
    for (uint8_t i = 1; i < n; i++) {
        uint8_t key = order[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0) {
            const TerminalModuleDesc* a = TerminalModuleAt(order[j]);
            const TerminalModuleDesc* b = TerminalModuleAt(key);
            if (a != nullptr && b != nullptr && strcmp(a->name, b->name) > 0) {
                order[j + 1] = order[j];
                j--;
            } else {
                break;
            }
        }
        order[j + 1] = key;
    }

    Serial.println("\r\nModules:");
    for (uint8_t i = 0; i < n; i++) {
        const TerminalModuleDesc* d = TerminalModuleAt(order[i]);
        if (d == nullptr) { continue; }
        Serial.printf("  %-6s ", d->name);
        printModuleCommands(*d);
        Serial.println();
    }

    Serial.print("\r\nEditable: ");
    bool first = true;
    for (uint8_t i = 0; i < n; i++) {
        const TerminalModuleDesc* d = TerminalModuleAt(order[i]);
        if (d == nullptr || !d->editable) { continue; }
        if (!first) { Serial.print(" "); }
        Serial.print(d->name);
        first = false;
    }
    if (first) { Serial.print("-"); }

    Serial.print("\r\nRead-only: ");
    first = true;
    for (uint8_t i = 0; i < n; i++) {
        const TerminalModuleDesc* d = TerminalModuleAt(order[i]);
        if (d == nullptr || d->editable) { continue; }
        if (!first) { Serial.print(" "); }
        Serial.print(d->name);
        first = false;
    }
    if (first) { Serial.print("-"); }

    Serial.println("\r\nBuilt-ins: help echo");
    Serial.println("Use '<module> help' for syntax.");
}

void TerminalPrintText(const char* text) {
    if (text == nullptr) { return; }
#if defined(ESP8266)
    Serial.print(FPSTR(text));
#endif
#if !defined(ESP8266)
    Serial.print(text);
#endif
}

void TerminalPrintModuleHelp(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        TerminalPrintHelp();
        return;
    }

    String want = name;
    want.toLowerCase();
    for (uint8_t i = 0; i < TerminalModuleCount(); i++) {
        const TerminalModuleDesc* d = TerminalModuleAt(i);
        if (d == nullptr) { continue; }
        String have = d->name;
        have.toLowerCase();
        if (have == want) {
            Serial.println();
            TerminalPrintText(d->helpText);
            return;
        }
    }
    Serial.printf("Unknown module '%s'. Type 'help' for the module list.\r\n", name);
}

void TerminalHelpCmd() {
    char* arg = term.getNext();
    if (arg != nullptr) {
        TerminalPrintModuleHelp(arg);
        return;
    }
    TerminalPrintHelp();
}

void TerminalEchoCmd() {
    term.EchoOnOff();
}

void TerminalDefaultCmd(const char* command) {
    Serial.printf("Unknown command '%s'. Type 'help'.\r\n",
                  (command != nullptr) ? command : "");
}
