


#include <Arduino.h>
#if defined(ESP32)
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif

#include <string.h>

#include "core_sys/eertos.h"
#include "core_led.h"
#include "common_module.h"

// ============================================================
// Глобальные объекты и переменные
// ============================================================

typedef struct {
    const char* pattern;  // указатель на PROGMEM-строку
    uint8_t     length;   // число слотов
    uint8_t     position; // текущая позиция
    int16_t     times;    // число повторов, -1 = бесконечно
    bool        active;
} LedSlot;

static LedSlot ledSlots[LED_PRIO_COUNT];
static bool ledSteadyOn = false;                 // базовое состояние «горит постоянно»
static char ledManualPattern[LEDSTRINGLIMIT + 1];// буфер терминальной команды blink (без heap)

#if defined(ESP8266)
void espLedOn ()    {	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, LOW);} }
void espLedOff ()    {	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, HIGH);} }
#endif

#if defined(ESP32)
void espLedOn ()    {  if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, HIGH);} }
void espLedOff ()    { if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, LOW);} }
#endif

// ============================================================
// Верхний активный слот и применение выхода
// ============================================================

static LedSlot* ledTopSlot() {
	for (int i = LED_PRIO_COUNT - 1; i >= 0; i--) {
		if (ledSlots[i].active) { return &ledSlots[i]; }
	}
	return NULL;
}

static void ledApplyOutput() {
	LedSlot* s = ledTopSlot();
	if (s) {
		if (ns_core_led::ledPatAt(s->pattern, s->position) == '*') { espLedOn(); }
		else { espLedOff(); }
	} else {
		if (ledSteadyOn) { espLedOn(); }
		else { espLedOff(); }
	}
}

// ============================================================
// Управление слотами
// ============================================================

void ledSetState(LedPriority prio, const char* pattern, int16_t times) {
	if (prio >= LED_PRIO_COUNT) { return; }
	if (pattern == NULL) { ledClearState(prio); return; }
	if (times == 0) { ledClearState(prio); return; }	// 0 = снять слот
	LedSlot& s = ledSlots[prio];
	if (prio != LED_PRIO_MANUAL) {
		// тот же паттерн уже активен — не перезапускаем (идемпотентно)
		if (s.active && s.pattern == pattern) { return; }
		// конечный паттерн в слоте не прерывается другим паттерном
		if (s.active && s.times != -1) { return; }
	}
	s.pattern = pattern;
	s.length = ns_core_led::ledPatLen(pattern);
	if (s.length == 0) { ledClearState(prio); return; }
	s.position = 0;
	s.times = times;
	s.active = true;
	ledApplyOutput();
}

void ledClearState(LedPriority prio) {
	if (prio >= LED_PRIO_COUNT) { return; }
	ledSlots[prio].active = false;
	ledApplyOutput();
}

void ledSetSteady(bool on) {
	ledSteadyOn = on;
	ledApplyOutput();
}

void ledMacroRst()    {
	for (uint8_t i = 0; i < LED_PRIO_COUNT; i++) { ledSlots[i].active = false; }
	ledSteadyOn = false;
	espLedOff();
}

void ledInit()    {
	if (CONNECTION_LED >= 0) {	pinMode(CONNECTION_LED, OUTPUT);	}
	if (CONNECTION_LED >= 0) {	espLedOff();	}	// Turn LED off
	ledMacroRst();
	ledMacroTimerTask();
}

// ============================================================
// Продвижение паттерна (1 шаг)
// ============================================================

bool ledMacroBlinker( ) {
	LedSlot* s = ledTopSlot();
	if (!s) {
		ledApplyOutput();
		return false;
	}
	if (s->position	>= s->length-1) { 
		s->position = 0; 
		if (s->times > 0) { s->times--; }
		if (s->times == 0) { s->active = false; }
	} 
	else { s->position++; }
	ledApplyOutput();
	return true; // repeat? true/false
}

void ledMacroTimerTask() {
	SetTimerTask(ledMacroTimerTask, SLOT_MIN_TIME);
	ledMacroBlinker();
}

// ============================================================
// Ручной тест из терминала (команда blink)
// ============================================================

void LedMacroSet (const char* _inStr, int16_t _times){ 
	if (_inStr == NULL) { return; }
	if (_times < -1) {_times = -1;}
	strncpy(ledManualPattern, _inStr, LEDSTRINGLIMIT);
	ledManualPattern[LEDSTRINGLIMIT] = '\0';
	ledSetState(LED_PRIO_MANUAL, ledManualPattern, _times);
}
