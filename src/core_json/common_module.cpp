
#include "core_json/common_module.h"

#include <stdio.h>

namespace ns_core_json {

bool colorParseHex(const String& s, uint32_t& out) {
    if (s.length() != 7 || s.charAt(0) != '#') { return false; }

    uint32_t v = 0;
    for (int i = 1; i < 7; i++) {
        char c = s.charAt(i);
        uint8_t d = 0;
        if (c >= '0' && c <= '9') {
            d = (uint8_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = (uint8_t)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = (uint8_t)(c - 'A' + 10);
        } else {
            return false;
        }
        v = (v << 4) | d;
    }

    out = v & 0xFFFFFF;
    return true;
}

uint32_t colorParseOrWhite(const String& s) {
    uint32_t v = COLOR_WHITE;
    if (colorParseHex(s, v) == false) { return COLOR_WHITE; }
    return v;
}

String colorToHex(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", (unsigned int)(rgb & 0xFFFFFF));
    return String(buf);
}

uint32_t colorFromJson(JsonVariantConst v, uint32_t fallback) {
    if (v.isNull()) { return fallback; }
    const char* s = v.as<const char*>();
    if (s == nullptr) { return COLOR_WHITE; }
    return colorParseOrWhite(String(s));
}

} // namespace ns_core_json
