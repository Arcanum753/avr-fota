#ifndef _MOCK_WIFICLIENT_H
#define _MOCK_WIFICLIENT_H

#include "Arduino.h"
#include "IPAddress.h"

class WiFiClient {
public:
    int connect(const char*, uint16_t) { return 0; }
    void stop() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t*, size_t) { return 0; }
    bool connected() { return false; }
};

class WiFiClientSecure : public WiFiClient {};

#endif // _MOCK_WIFICLIENT_H
