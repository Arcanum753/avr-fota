#include "Arduino.h"

#include <stdarg.h>

static uint32_t g_millis = 0;

MockSerial Serial;

void mockMillisSet(uint32_t ms) { g_millis = ms; }
void mockMillisAdvance(uint32_t ms) { g_millis += ms; }
uint32_t millis(void) { return g_millis; }
uint32_t micros(void) { return g_millis * 1000u; }
void delay(uint32_t) {}
void delayMicroseconds(uint32_t) {}
void yield(void) {}

void pinMode(uint8_t, uint8_t) {}
static int g_dwCount = 0;
static int g_lastDwValue = -1;
static int g_lastDwPin = -1;
void digitalWrite(uint8_t pin, uint8_t value) {
    g_dwCount++;
    g_lastDwPin = pin;
    g_lastDwValue = value;
}
int mockDigitalWriteCount() { return g_dwCount; }
int mockLastDigitalValue() { return g_lastDwValue; }
int mockLastDigitalPin() { return g_lastDwPin; }
void mockDigitalWriteReset() { g_dwCount = 0; g_lastDwValue = -1; g_lastDwPin = -1; }
int  digitalRead(uint8_t) { return 0; }
void analogWrite(uint8_t, int) {}

void noInterrupts(void) {}
void interrupts(void) {}

size_t MockSerial::emit(const char* s) {
#ifdef MOCK_SERIAL_VERBOSE
    if (s) { fputs(s, stdout); fflush(stdout); }
#endif
    return s ? strlen(s) : 0;
}

size_t MockSerial::printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    buf[sizeof(buf) - 1] = 0;
    return emit(buf);
}
