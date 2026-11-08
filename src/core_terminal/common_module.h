#ifndef _CORE_TERMINAL_COMMON_MODULE_h
#define _CORE_TERMINAL_COMMON_MODULE_h

// ============================================================
// common_module.h — чистые хелперы core_terminal (без состояния).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

namespace ns_core_terminal {

String trimCopy(const String& in);
// Снять парные кавычки ('...' или "...").
String unquote(const String& in);
// on/off, 1/0, true/false, yes/no (без учёта регистра).
bool parseOnOff(const String& in, bool& out);
// Строгий разбор целого: вся строка должна быть числом, диапазон [min,max].
bool parseIntStrict(const String& in, long& out, long min, long max);
// "1d 02:03:04" (дни опускаются, если 0).
String formatUptime(uint32_t seconds);

} // namespace ns_core_terminal

#endif // _CORE_TERMINAL_COMMON_MODULE_h
