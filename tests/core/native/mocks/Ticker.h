#ifndef _MOCK_TICKER_H
#define _MOCK_TICKER_H

#include <stdint.h>

class Ticker {
public:
    void attach_ms(uint32_t, void (*)(void)) {}
    void attach(uint32_t, void (*)(void)) {}
    void attach_ms(uint32_t, void (*)(void*), void*) {}
    void detach() {}
    void stop() {}
};

#endif // _MOCK_TICKER_H
