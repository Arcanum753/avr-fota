#ifndef _COMMON_GPIO_h
#define _COMMON_GPIO_h


#ifdef DEBUG_LED
#define DEBUGLOGLED(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGLED(...)
#endif

#if defined(ESP8266)
#define LED_ON LOW
#define LED_OFF HIGH
#endif

#if defined(ESP32)
#define LED_ON      HIGH
#define LED_OFF     LOW
#endif


void flashLED(int pin, int times, int delayTime) ;

void espLedOn ();
void espLedOff ();

#endif // _COMMON_GPIO_h
