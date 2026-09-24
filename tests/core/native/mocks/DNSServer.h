#ifndef _MOCK_DNSSERVER_H
#define _MOCK_DNSSERVER_H

#include "Arduino.h"
#include "IPAddress.h"

class DNSServer {
public:
    bool start(uint16_t, const char*, const IPAddress&) { _started = true; return true; }
    void stop() { _started = false; }
    void processNextRequest() {}
    bool started() const { return _started; }
private:
    bool _started = false;
};

#endif // _MOCK_DNSSERVER_H
