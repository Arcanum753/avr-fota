/*!
 * \file ErriezSerialTerminal.cpp
 * \brief Serial terminal library for Arduino.
 * \details
 *      Source:         https://github.com/Erriez/ErriezSerialTerminal
 *      Documentation:  https://erriez.github.io/ErriezSerialTerminal
 */
#include <Arduino.h>

#include "main.h"

#include <ESPAsyncWebServer.h>
#include "core_web/FSWebServerLib.h"
#include "ErriezSerialTerminal.h"

#include "core_terminal/core_terminal.h"


// Newline character '\r' or '\n'
char newlineChar = '\r';
// Separator character between commands and arguments
char delimiterChar = ' ';

SerialTerminal term(newlineChar, delimiterChar);

static TerminalModuleInit _moduleSlots[TERMINAL_MODULE_SLOTS];
static uint8_t _moduleSlotCount = 0;

void TerminalRegisterModule(TerminalModuleInit initFn) {
    if (_moduleSlotCount < TERMINAL_MODULE_SLOTS) {
        _moduleSlots[_moduleSlotCount++] = initFn;
    }
}

static bool _slotsApplied = false;

void TerminalInit(){
    term.addCommand("help",  TerminalHelp );    // показать все команды
    term.addCommand("reset", EspReset );        // ресет мк
    term.addCommand("echo",  TerminalEcho );    // выкл/вкл "эха" терминала.
    term.addCommand("?",     InfoShow );        // общая информация и текущее состояние подключения вай-фай
    term.addCommand("id",    TermIdent );       // показать/установить имя и серийник устройства

    term.addCommand("1",    test ); // тест терминала

    term.addCommand("udpp",    udpp ); 
    term.addCommand("udpc",    udpc ); 
    term.addCommand("udps",    udps ); 
    term.addCommand("led", BlinkCmd);

// dead monks
// all about dbg of AVR
    // term.addCommand("flash", flash1 );
    // term.addCommand("flash2", flash2 );

    // term.addCommand("stm32",    termStm32 );
    // term.addCommand("swdf", termSwdFlash1 );
    // term.addCommand("avr",  avr );

    Serial.println("\n\r Serial terminal inited.");
    term.setSerialEcho(true);
    term.helpShow();
}

// main Loop func for Terminal
// Слоты модулей применяются лениво при первом вызове loop(): к этому моменту
// все begin() ядер/модулей/устройств уже выполнены и команды зарегистрированы.
void TerminalLoop() {
    if (!_slotsApplied) {
        _slotsApplied = true;
        for (uint8_t i = 0; i < _moduleSlotCount; i++) {
            _moduleSlots[i]();
        }
    }
    term.readSerial();
}
