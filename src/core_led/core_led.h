#ifndef _COMMON_GPIO_h
#define _COMMON_GPIO_h


#ifdef DEBUG_LED
#define DEBUGLOGLED(...) DBG_MOD("[C_LED] ", __VA_ARGS__)
#else
#define DEBUGLOGLED(...)
#endif

#include "core_led_types.h"

void ledInit();
void ledSetState(LedPriority prio, const char* pattern, int16_t times); // times: -1 бесконечно, 0 = снять слот
void ledClearState(LedPriority prio);
void ledSetSteady(bool on); // базовое состояние «горит постоянно» (подключено к Wi-Fi)

void espLedOn ();
void espLedOff ();

void ledMacroTimerTask() ;
bool ledMacroBlinker( ) ;
void ledMacroRst();
void LedMacroSet (const char* _inStr, int16_t _times);

#endif // _COMMON_GPIO_h
