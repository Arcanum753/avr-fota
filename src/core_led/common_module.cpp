
#include "core_led/common_module.h"

#include <Arduino.h>
#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif
#include <string.h>

// ============================================================
// Вспомогательные функции ядра core_led
// ============================================================

namespace ns_core_led {

#if defined(ESP8266)
uint8_t ledPatLen(const char* p)  { return (uint8_t)strlen_P(p); }
char    ledPatAt(const char* p, uint8_t i) { return (char)pgm_read_byte(p + i); }
#endif
#if defined(ESP32)
uint8_t ledPatLen(const char* p)  { return (uint8_t)strlen(p); }
char    ledPatAt(const char* p, uint8_t i) { return p[i]; }
#endif

} // namespace ns_core_led
