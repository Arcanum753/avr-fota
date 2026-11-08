#include <Arduino.h>
#include <string.h>

#include "core_led/core_led.h"
#include "core_led/core_led_types.h"
#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Терминальные команды core_led
// ============================================================

static const char* const kLedCommands[] = { "<times> <mask>", nullptr };

static const char kLedHelp[] PROGMEM =
    "led - LED test\r\n"
    "  led <times> <mask>   times: -1 = forever; mask chars: . - + etc.\r\n"
    "  led help             this help\r\n";

static void ledTerminalCmd() {
    char* cmd = term.getNext();
    if (cmd == nullptr || strcmp(cmd, "help") == 0) {
        TerminalPrintText(kLedHelp);
        return;
    }

    long times = 0;
    if (!ns_core_terminal::parseIntStrict(String(cmd), times, -1, 32767)) {
        Serial.println("Usage: led <times> <mask>");
        return;
    }
    char* mask = term.getNext();
    if (mask == nullptr) {
        Serial.println("Usage: led <times> <mask>");
        return;
    }
    if (strlen(mask) > LEDSTRINGLIMIT) {
        Serial.printf("Mask is longer than %d characters.\r\n", LEDSTRINGLIMIT);
        return;
    }
    LedMacroSet(mask, (int16_t)times);
}

void ledTerminalRegister() {
    TerminalRegisterModule("core_led", "led", "LED test",
                           ledTerminalCmd, kLedCommands, kLedHelp, false);
}
