#include "common_module.h"

#include "core_state.h"

namespace ns_core_state {

String formatF32(float value) {
    return String(value, 3);
}

bool nameHasDot(const char* name) {
    if (name == nullptr) { return false; }
    for (const char* p = name; *p != 0; p++) {
        if (*p == '.') { return true; }
    }
    return false;
}

const char* kindName(uint8_t kind) {
    switch (kind) {
        case 1: return "BOOL";
        case 2: return "I32";
        case 3: return "F32";
        case 4: return "STR";
        case 5: return "TIME";
        case 6: return "ENUM";
        default: return "NONE";
    }
}

const char* busErrStr(int code) {
    switch (code) {
        case 0:   return "OK";
        case -1:  return "ERR_NOT_REGISTERED";
        case -2:  return "ERR_NOT_FOUND";
        case -3:  return "ERR_BAD_TYPE";
        case -4:  return "ERR_BAD_ARGC";
        case -5:  return "ERR_BAD_VALUE";
        case -6:  return "ERR_READONLY";
        case -7:  return "ERR_DISABLED";
        case -8:  return "ERR_BUSY";
        case -9:  return "ERR_NOT_READY";
        case -10: return "ERR_TIMEOUT";
        case -11: return "ERR_INTERNAL";
        case -12: return "ERR_NOT_SUPPORTED";
        case -13: return "ERR_DENIED";
        default:  return "ERR_MODULE";
    }
}

} // namespace ns_core_state
