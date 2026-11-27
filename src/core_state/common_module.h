#ifndef _CORE_STATE_COMMON_MODULE_h
#define _CORE_STATE_COMMON_MODULE_h

#include <stdint.h>
#include <Arduino.h>

namespace ns_core_state {

// Значение F32 для UI/JSON: ровно 3 знака после запятой.
String formatF32(float value);

// Содержит ли имя точку (тогда оно трактуется как полное, без автопрефикса namespace).
bool nameHasDot(const char* name);

// Текстовое имя типа значения.
const char* kindName(uint8_t kind);

// Текст ошибки шины по коду.
const char* busErrStr(int code);

} // namespace ns_core_state

#endif // _CORE_STATE_COMMON_MODULE_h
