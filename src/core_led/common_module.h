
#ifndef _CORE_LED_COMMON_MODULE_h
#define _CORE_LED_COMMON_MODULE_h

#include <stdint.h>

// Вспомогательные функции ядра core_led (чистые, без состояния).
namespace ns_core_led {

// Доступ к PROGMEM-строкам паттернов (на ESP8266 — через pgm_read_byte)
uint8_t ledPatLen(const char* p);
char    ledPatAt(const char* p, uint8_t i);

} // namespace ns_core_led

#endif // _CORE_LED_COMMON_MODULE_h
