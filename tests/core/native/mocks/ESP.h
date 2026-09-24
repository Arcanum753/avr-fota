#ifndef _MOCK_ESP_H
#define _MOCK_ESP_H

#include <stdint.h>

class MockESP {
public:
    void restart();
    void reset() { restart(); }
    uint32_t getSketchSize() { return 0x100000; }
    uint32_t getFreeSketchSpace() { return 0x200000; }
    uint32_t getFreeHeap() { return 100000; }
    uint32_t getChipId() { return 0x12345678; }
};

extern MockESP ESP;

// Тестовый счётчик рестартов (не сбрасывается автоматически).
int  mockEspRestartCount();
void mockEspRestartReset();

#endif // _MOCK_ESP_H
