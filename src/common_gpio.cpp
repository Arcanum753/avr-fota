#if defined(ESP32)
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#endif

#include "common_gpio.h"




void espLedOn (){   digitalWrite(CONNECTION_LED, LED_ON); }
void espLedOff (){  digitalWrite(CONNECTION_LED, LED_OFF); }

#if defined(ESP8266)
void espLedOn (){   digitalWrite(CONNECTION_LED, LOW);  }
void espLedOff (){  digitalWrite(CONNECTION_LED, HIGH); }
#endif


void flashLED(int pin, int times, int delayTime) {
	int oldState = digitalRead(pin);


	DEBUGLOGLED("---Flash LED during %d ms %d times. Old state = %d\r\n", delayTime, times, oldState);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LED_ON); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, LED_OFF); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
}
