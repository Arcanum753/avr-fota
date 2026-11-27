
#ifndef _CORE_JSON_COMMON_MODULE_h
#define _CORE_JSON_COMMON_MODULE_h

#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdint.h>

// Вспомогательные функции модуля core_json (чистые, без состояния).
namespace ns_core_json {

// Цвет по умолчанию при ошибке чтения/разбора строки.
constexpr uint32_t COLOR_WHITE = 0xFFFFFF;

// Строгий разбор "#RRGGBB" (6 hex-цифр, регистр любой). true — успех.
bool colorParseHex(const String& s, uint32_t& out);

// Разбор с fallback: при ошибке возвращает COLOR_WHITE.
uint32_t colorParseOrWhite(const String& s);

// Формат "#RRGGBB" (верхний регистр).
String colorToHex(uint32_t rgb);

// Чтение цвета из JSON: отсутствие значения -> fallback,
// не строка или невалидная строка -> COLOR_WHITE.
uint32_t colorFromJson(JsonVariantConst v, uint32_t fallback);

} // namespace ns_core_json

#endif // _CORE_JSON_COMMON_MODULE_h
