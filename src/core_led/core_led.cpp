

#include <Arduino.h>
#if defined(ESP32)
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#endif

#include "eertos.h"
#include "core_led.h"

uint32_t ledMacroPosition = 0;
String ledBlinkMacros = "...";

volatile uint8_t isBlinking = 0;
int16_t ledMacroTimes = 0;
static bool patternActive = false;


#if defined(ESP8266)
void espLedOn ()    {	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, LOW);} }
void espLedOff ()    {	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, HIGH);} }
#endif

#if defined(ESP32)
void espLedOn ()    {  if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, HIGH);} }
void espLedOff ()    { if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, LOW);} }
#endif


void ledInit()    {
	if (CONNECTION_LED >= 0) {	pinMode(CONNECTION_LED, OUTPUT);	}
	if (CONNECTION_LED >= 0) {	espLedOff();	}	// Turn LED off
	ledMacroRst();
	ledMacroTimerTask();
}

void flashLEDOnConnected()	{
	if (isBlinking == 1) {return;}
	espLedOn();
}

void ledMacroTimerTask() {
	SetTimerTask(ledMacroTimerTask, SLOT_MIN_TIME);
	if (patternActive == true && ledMacroTimes  != 0 ) {	
		ledMacroBlinker(); 
		isBlinking = 1;
	} else { 
		isBlinking = 0; 
		if (ledMacroTimes == 0) { patternActive = false; }
	}
}


bool ledMacroBlinker( ) {
	if (ledBlinkMacros.length() !=0 ) {
		if (ledMacroPosition	>= ledBlinkMacros.length()-1) { 
			ledMacroPosition = 0; 
			if (ledMacroTimes > 0) { ledMacroTimes--; }
		} 
		else { ledMacroPosition++; }
		if (ledBlinkMacros[ledMacroPosition] == '*') { espLedOn();}
		if (ledBlinkMacros[ledMacroPosition] == '.') { espLedOff();}
	} else {
		espLedOff(); 
	}
	return true; // repeat? true/false
}

void LedMacroSet (String _inStr, int16_t _times){ 
	if (patternActive == true) { return;   }
	if (_inStr.length() == 0) {
        espLedOff();
        return;
    }
	if (_times < -1) {_times = -1;}
	ledMacroTimes = _times;
	ledMacroRst();
	if (LEDSTRINGLIMIT 		<= _inStr.length()-1) {  _inStr.remove(LEDSTRINGLIMIT);  } 
	ledBlinkMacros = _inStr; 
	patternActive = true;
}

void ledMacroRst()    {
	espLedOff();
	ledMacroPosition = 0;
}

// ==================== Wi-Fi состояния ====================

void ledMacrosWifiScan()			{	LedMacroSet("*.*.*.*.", 10);  }
void ledMacrosWifiDisconnect()		{	LedMacroSet("*.........", 3);  }
void ledMacrosWifiAP()				{	LedMacroSet("*.*.*......", -1); }
void ledMacrosWifiConnecting()		{	LedMacroSet("*.*..", 2); }
void ledMacrosWifiError()			{	LedMacroSet("*.*.*", 5); }
// ==================== Память ====================
void ledMacrosMemoryRead()			{	LedMacroSet("*", 1); }
void ledMacrosMemoryWrite()			{	LedMacroSet("*.*", 1); }
void ledMacrosMemoryError()			{	LedMacroSet("*.*.*.*.*", 3); }
void ledMacrosMemoryClear()			{	LedMacroSet("***...***...", 2); }

// ==================== Дополнительные ====================
void ledMacrosSuccess()				{	LedMacroSet("*", 1); }
void ledMacrosError()				{	LedMacroSet("*.*.*", 2); }
void ledMacrosWaiting()				{	LedMacroSet("*...*...", -1); }
void ledMacrosSystemStart()			{	LedMacroSet("***", 2); }