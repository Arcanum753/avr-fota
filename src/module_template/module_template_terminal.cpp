#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "module_template.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Терминальные команды module_template
//
// Это эталон для остальных компонентов:
//  * обработчик — статический метод класса, поэтому обращается к
//    приватному состоянию через глобальный объект module_template
//    (например, module_template._config) без публичных сеттеров;
//  * term.getNext() — очередной токен, разделённый пробелами,
//    term.getRemaining() — остаток строки (trim + снятие кавычек);
//  * каждая подкоманда проверяет и число аргументов, и диапазон;
//  * команды только применяют изменения (память + железо); на FS
//    пишет только save — правило apply/save из корневого AGENTS.md;
//  * helpText лежит в PROGMEM — экономия DRAM ESP8266.
// ============================================================

static const char* const kTemplateCommands[] = {
    "show", "gpio", "blink", "text", "save", nullptr
};

// Строковое представление числового предела для PROGMEM-справки:
// при изменении TEMPLATE_BLINK_MS_MAX справка и usage остаются согласованными.
#define TEMPLATE_STR_(x) #x
#define TEMPLATE_STR(x) TEMPLATE_STR_(x)

static const char kTemplateHelp[] PROGMEM =
    "templ - module template demo (changes are not saved automatically)\r\n"
    "  templ show                      current state and config status\r\n"
    "  templ gpio <1|2> on|off|toggle  apply GPIO state\r\n"
    "  templ blink <0.." TEMPLATE_STR(TEMPLATE_BLINK_MS_MAX) ">          blink period, ms (0 = off)\r\n"
    "  templ text [<text>]             show/set demo text (\"\" clears)\r\n"
    "  templ save                      persist " CONFIG_FILE_TEMPLATE "\r\n"
    "  templ help                      this help\r\n";

static const char* onOffText(bool on) {
    return on ? "on" : "off";
}

static void printGpioUsage() {
    Serial.println("Usage: templ gpio <1|2> on|off|toggle");
}

static void printTextValue(const String& value) {
    Serial.printf("Text: %s\r\n", value.c_str());
}

void CLASS_MODULE_TEMPLATE::terminalRegister() {
    TerminalRegisterModule("module_template", "templ", "module template demo",
                           &CLASS_MODULE_TEMPLATE::s_terminalCmd,
                           kTemplateCommands, kTemplateHelp, true);
}

void CLASS_MODULE_TEMPLATE::s_terminalCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) {
        TerminalPrintText(kTemplateHelp);
        return;
    }

    if (strcmp(sub, "show") == 0)  { s_cmdShow();  return; }
    if (strcmp(sub, "gpio") == 0)  { s_cmdGpio();  return; }
    if (strcmp(sub, "blink") == 0) { s_cmdBlink(); return; }
    if (strcmp(sub, "text") == 0)  { s_cmdText();  return; }
    if (strcmp(sub, "save") == 0)  { s_cmdSave();  return; }

    Serial.printf("Unknown templ subcommand '%s'. Type 'templ help'.\r\n", sub);
}

// ============================================================
// show — ответ на запрос: текущее состояние и статус конфига
// ============================================================

void CLASS_MODULE_TEMPLATE::s_cmdShow() {
    if (term.getNext() != nullptr) {
        Serial.println("Usage: templ show");
        return;
    }

    const strTmplConfig& cfg = module_template._config;
    Serial.printf("GPIO1:  %s\r\n", onOffText(cfg.gpio1State));
    Serial.printf("GPIO2:  %s\r\n", onOffText(cfg.gpio2State));
    Serial.printf("Blink:  %u ms%s\r\n", (unsigned)cfg.blinkInterval,
                  (cfg.blinkInterval == 0) ? " (off)" : "");
    Serial.printf("Text:   %s\r\n", cfg.demoSampleText.c_str());
    Serial.printf("Array:  %s | %s | %s\r\n",
                  cfg.demoArray[0].c_str(), cfg.demoArray[1].c_str(),
                  cfg.demoArray[2].c_str());
    Serial.printf("Config: %s\r\n",
                  module_template._configDirty ? "modified (use 'templ save')" : "saved");
}

// ============================================================
// gpio <1|2> on|off|toggle — два аргумента, enum и инверсия
// ============================================================

void CLASS_MODULE_TEMPLATE::s_cmdGpio() {
    char* idxArg = term.getNext();
    char* stateArg = term.getNext();

    // Третий токен — ошибка: не даём молча игнорировать опечатки в аргументах.
    if (idxArg == nullptr || stateArg == nullptr || term.getNext() != nullptr) {
        printGpioUsage();
        return;
    }

    long idx = 0;
    if (!ns_core_terminal::parseIntStrict(String(idxArg), idx, 1, 2)) {
        printGpioUsage();
        return;
    }

    bool state = false;
    if (strcmp(stateArg, "toggle") == 0) {
        // toggle — инверсия текущего целевого состояния выбранного выхода.
        state = (idx == 1) ? !module_template._config.gpio1State
                           : !module_template._config.gpio2State;
    } else if (!ns_core_terminal::parseOnOff(String(stateArg), state)) {
        printGpioUsage();
        return;
    }

    if (idx == 1) { module_template._config.gpio1State = state; }
    if (idx == 2) { module_template._config.gpio2State = state; }
    module_template._configDirty = true;

    bool blinking = (module_template._config.blinkInterval > 0);
    // Единая политика (см. applyGpioTarget): при выключенном моргании
    // состояние применяется сразу, при включённом — на ближайшем тике.
    module_template.applyGpioTarget();
    Serial.printf("GPIO%ld: %s%s.\r\n", idx, onOffText(state),
                  blinking ? " (applied on next blink; not saved)" : " (not saved)");
}

// ============================================================
// blink <ms> — числовой диапазон и особый случай 0
// ============================================================

void CLASS_MODULE_TEMPLATE::s_cmdBlink() {
    char* arg = term.getNext();
    long ms = 0;

    // Ровно один аргумент: лишние токены означают ошибку ввода.
    if (arg == nullptr || term.getNext() != nullptr ||
        !ns_core_terminal::parseIntStrict(String(arg), ms, 0, TEMPLATE_BLINK_MS_MAX)) {
        Serial.printf("Usage: templ blink <0..%d>\r\n", TEMPLATE_BLINK_MS_MAX);
        return;
    }

    module_template._config.blinkInterval = (uint16_t)ms;
    module_template._configDirty = true;
    module_template.applyBlinkTimer();

    if (ms == 0) {
        Serial.println("Blink: off (not saved).");
    } else {
        Serial.printf("Blink: %ld ms (not saved).\r\n", ms);
    }
}

// ============================================================
// text [<text>] — свободная строка до конца строки
// ============================================================

void CLASS_MODULE_TEMPLATE::s_cmdText() {
    // getRemaining() возвращает nullptr, если аргументов не было вообще,
    // и остаток (возможно, из одних пробелов) — иначе. Пробельный остаток
    // считаем запросом; очистка выполняется только явным `""`.
    char* rest = term.getRemaining();
    if (rest == nullptr) {
        printTextValue(module_template._config.demoSampleText);
        return;
    }

    String raw = ns_core_terminal::trimCopy(String(rest));
    if (raw.length() == 0) {
        printTextValue(module_template._config.demoSampleText);
        return;
    }

    String value = ns_core_terminal::unquote(String(rest));
    module_template._config.demoSampleText = value;
    module_template._configDirty = true;
    Serial.printf("Text: '%s' (not saved).\r\n", value.c_str());
}

// ============================================================
// save — единственная подкоманда, пишущая конфиг на FS
// ============================================================

void CLASS_MODULE_TEMPLATE::s_cmdSave() {
    if (term.getNext() != nullptr) {
        Serial.println("Usage: templ save");
        return;
    }

    // save_config_template() сбрасывает _configDirty только при успешной записи.
    bool ok = module_template.save_config_template();
    Serial.println(ok ? "Saved." : "Save failed.");
}
