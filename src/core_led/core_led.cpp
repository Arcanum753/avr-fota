#include <Arduino.h>
#if defined(ESP32)
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif

#include "core_led.h"

// ============================================================
// Инициализация (вызывается вручную из main.cpp)
// ============================================================

void ledInit()    {
	if (CONNECTION_LED >= 0) {	pinMode(CONNECTION_LED, OUTPUT);	}
	if (CONNECTION_LED >= 0) {	espLedOff();	}	// Turn LED off
	ledMacroRst();
	ledMacroTimerTask();
}
