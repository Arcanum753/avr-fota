#ifndef _COMMON_GPIO_h
#define _COMMON_GPIO_h


#ifdef DEBUG_LED
#define DEBUGLOGLED(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGLED(...)
#endif



void flashLEDinit();
void flashLED(uint8_t pin, uint16_t times, uint32_t delayTime) ;
void flashLEDTaskOn();
void flashLEDTaskOff();

void espLedOn ();
void espLedOff ();
void flashLEDOnConnected();

#endif // _COMMON_GPIO_h
