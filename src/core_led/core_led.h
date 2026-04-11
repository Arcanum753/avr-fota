#ifndef _COMMON_GPIO_h
#define _COMMON_GPIO_h


#ifdef DEBUG_LED
#define DEBUGLOGLED(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGLED(...)
#endif

#define SLOT_MIN_TIME  100// 0.1 min time to ligth
#define LEDSTRINGLIMIT  128

void ledInit();
void flashLEDTaskOn();
void flashLEDTaskOff();

void espLedOn ();
void espLedOff ();
void flashLEDOnConnected();

void ledMacroTimerTask() ;
bool ledMacroBlinker( ) ;
void ledMacroRst();
void LedMacroSet (String _inStr, int16_t _times);

void ledMacrosWifiScan()		;
void ledMacrosWifiDisconnect()	;
void ledMacrosWifiAP()			;
void ledMacrosWifiConnecting()	;
void ledMacrosWifiError()		;
void ledMacrosMemoryRead()		;
void ledMacrosMemoryWrite()		;
void ledMacrosMemoryError()		;
void ledMacrosMemoryClear()		;
void ledMacrosSuccess()			;
void ledMacrosError()			;
void ledMacrosWaiting()			;
void ledMacrosSystemStart()		;

#endif // _COMMON_GPIO_h
