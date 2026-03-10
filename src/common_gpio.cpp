

#include <Arduino.h>
#if defined(ESP32)
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#endif

#include "eertos.h"
#include "common_gpio.h"


#if defined(ESP8266)
void espLedOn (){	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, LOW);} }
void espLedOff (){	if (CONNECTION_LED >= 0) {digitalWrite(CONNECTION_LED, HIGH);} }
#endif

#if defined(ESP32)
void espLedOn (){  if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, HIGH);} }
void espLedOff (){ if (CONNECTION_LED >= 0)	{digitalWrite(CONNECTION_LED, LOW);} }
#endif


uint8_t isBlinking = 0;
uint16_t blinkTimes = 0;
uint32_t delayTime = 0;

uint8_t oldPin = 0;
uint8_t oldState = 0;

void flashLEDinit(){
	if (CONNECTION_LED >= 0) {	pinMode(CONNECTION_LED, OUTPUT);	}
	if (CONNECTION_LED >= 0) {	espLedOff();	}	// Turn LED off
}

void flashLED(uint8_t _oldPin, uint16_t times, uint32_t _delayMS) {
	if (_oldPin < 0) {return;}
	if (isBlinking == 1) {return;}

	oldPin = _oldPin;
	oldState = digitalRead(_oldPin);
	blinkTimes = times;
	delayTime = _delayMS;

	DEBUGLOGLED("---Flash LED during %d ms %d times. Old state = %d\r\n", _delayMS, times, oldState);
	SetTask(flashLEDTaskOn);
	isBlinking = 1;
}


void flashLEDTaskOn(){
	if (blinkTimes == 0) {
		digitalWrite(oldPin, oldState); 
		isBlinking = 0;
		return;
	}
	
	if (delayTime == 0) {return;}
	if (blinkTimes > 0) {
		espLedOn();
		SetTimerTask(flashLEDTaskOff, delayTime);
		blinkTimes--;
	}
		
}
	
	
void flashLEDTaskOff(){
	if (delayTime == 0) {return;}
	espLedOff();
	SetTimerTask(flashLEDTaskOn, delayTime);
}

void flashLEDOnConnected()	{
	if (isBlinking == 1) {return;}
	espLedOn();
}