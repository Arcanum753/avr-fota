#include "common_module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ns_core_terminal {

String trimCopy(const String& in) {
    String out = in;
    out.trim();
    return out;
}

String unquote(const String& in) {
    String out = trimCopy(in);
    if (out.length() >= 2) {
        char first = out.charAt(0);
        char last = out.charAt(out.length() - 1);
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            out = out.substring(1, out.length() - 1);
        }
    }
    return out;
}

bool parseOnOff(const String& in, bool& out) {
    String v = trimCopy(in);
    v.toLowerCase();
    if (v == "on" || v == "1" || v == "true" || v == "yes") { out = true; return true; }
    if (v == "off" || v == "0" || v == "false" || v == "no") { out = false; return true; }
    return false;
}

bool parseIntStrict(const String& in, long& out, long min, long max) {
    String v = trimCopy(in);
    if (v.length() == 0) { return false; }
    const char* s = v.c_str();
    char* end = nullptr;
    long value = strtol(s, &end, 10);
    if (end == s || end == nullptr || *end != '\0') { return false; }
    if (value < min || value > max) { return false; }
    out = value;
    return true;
}

String formatUptime(uint32_t seconds) {
    uint32_t days = seconds / 86400UL;
    seconds %= 86400UL;
    uint32_t hours = seconds / 3600UL;
    seconds %= 3600UL;
    uint32_t minutes = seconds / 60UL;
    uint32_t secs = seconds % 60UL;

    char buf[32];
    if (days > 0) {
        snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                 (unsigned long)days, (unsigned long)hours,
                 (unsigned long)minutes, (unsigned long)secs);
    } else {
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)hours, (unsigned long)minutes,
                 (unsigned long)secs);
    }
    return String(buf);
}

} // namespace ns_core_terminal
