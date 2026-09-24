#ifndef _MOCK_IPADDRESS_H
#define _MOCK_IPADDRESS_H

#include <stdint.h>
#include <stdio.h>
#include "Arduino.h"

class IPAddress {
public:
    IPAddress() { _a[0] = _a[1] = _a[2] = _a[3] = 0; }
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _a[0] = a; _a[1] = b; _a[2] = c; _a[3] = d; }
    IPAddress(uint32_t v) {
        _a[0] = (uint8_t)(v & 0xFF);
        _a[1] = (uint8_t)((v >> 8) & 0xFF);
        _a[2] = (uint8_t)((v >> 16) & 0xFF);
        _a[3] = (uint8_t)((v >> 24) & 0xFF);
    }

    uint8_t operator[](int i) const { return (i >= 0 && i < 4) ? _a[i] : 0; }
    uint8_t& operator[](int i) { return _a[i]; }

    bool fromString(const char* s) {
        unsigned int a, b, c, d;
        if (!s) return false;
        if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
        _a[0] = (uint8_t)a; _a[1] = (uint8_t)b; _a[2] = (uint8_t)c; _a[3] = (uint8_t)d;
        return true;
    }
    bool fromString(const String& s) { return fromString(s.c_str()); }

    String toString() const {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", _a[0], _a[1], _a[2], _a[3]);
        return String(buf);
    }

    bool operator==(const IPAddress& o) const {
        return _a[0] == o._a[0] && _a[1] == o._a[1] && _a[2] == o._a[2] && _a[3] == o._a[3];
    }
    bool operator!=(const IPAddress& o) const { return !(*this == o); }

private:
    uint8_t _a[4];
};

#endif // _MOCK_IPADDRESS_H
