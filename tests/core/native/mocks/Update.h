#ifndef _MOCK_UPDATE_H
#define _MOCK_UPDATE_H

#include "Arduino.h"

class MockUpdate {
public:
    bool begin(size_t = 0) { _running = true; _written = 0; return true; }
    size_t write(uint8_t) { _written++; return 1; }
    size_t write(const uint8_t*, size_t len) { _written += len; return len; }
    bool end(bool = true) { _running = false; return true; }
    bool hasError() { return false; }
    uint8_t getError() { return 0; }
    size_t size() const { return _written; }
private:
    bool   _running = false;
    size_t _written = 0;
};

extern MockUpdate Update;

#endif // _MOCK_UPDATE_H
