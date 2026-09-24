#ifndef _MOCK_ARDUINO_H
#define _MOCK_ARDUINO_H

// Минимальный host-мок Arduino API для native-тестов ядра avr-fota.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>

#include "WString.h"
#include "esp_attr.h"
#include "ESP.h"

#define ARDUINO 100
#define ARDUINO_ARCH_ESP8266 1

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PGM_P
#define PGM_P const char*
#endif

#define F(x) (x)

typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int word;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#ifndef NULL
#define NULL 0
#endif

template <typename T> static inline T min(T a, T b) { return a < b ? a : b; }
template <typename T> static inline T max(T a, T b) { return a > b ? a : b; }

// Управляемый счётчик времени — тесты задают его детерминированно.
void mockMillisSet(uint32_t ms);
void mockMillisAdvance(uint32_t ms);
uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int  digitalRead(uint8_t pin);
void analogWrite(uint8_t pin, int value);

// Тестовое наблюдение за GPIO.
int  mockDigitalWriteCount();
int  mockLastDigitalValue();
int  mockLastDigitalPin();
void mockDigitalWriteReset();

void noInterrupts(void);
void interrupts(void);

// Serial (no-op, пишет только при включённом MOCK_SERIAL_VERBOSE).
class MockSerial {
public:
    void begin(unsigned long) {}
    void end() {}
    void flush() {}
    int available() { return (int)(_in.size() - _inPos); }
    int read() {
        if (_inPos >= _in.size()) return -1;
        return (unsigned char)_in[_inPos++];
    }
    void inject(const char* s) { if (s) _in += s; }
    void clearInput() { _in.clear(); _inPos = 0; }
    size_t print(const char* s) { return emit(s); }
    size_t print(const String& s) { return emit(s.c_str()); }
    size_t print(char c) { char b[2] = { c, 0 }; return emit(b); }
    size_t print(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); return emit(b); }
    size_t print(unsigned int v) { char b[32]; snprintf(b, sizeof(b), "%u", v); return emit(b); }
    size_t print(long v) { char b[32]; snprintf(b, sizeof(b), "%ld", v); return emit(b); }
    size_t print(unsigned long v) { char b[32]; snprintf(b, sizeof(b), "%lu", v); return emit(b); }
    size_t println() { return emit("\n"); }
    size_t println(const char* s) { size_t n = emit(s); n += emit("\n"); return n; }
    size_t println(const String& s) { return println(s.c_str()); }
    size_t printf(const char* fmt, ...);
private:
    size_t emit(const char* s);
    std::string _in;
    size_t      _inPos = 0;
};

extern MockSerial Serial;

// WString.h использует String; некоторые скетчи ожидают Stream.
class Stream {
public:
    virtual ~Stream() {}
    virtual int available() { return 0; }
    virtual int read() { return -1; }
};

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) { n += write(*buffer++); }
        return n;
    }
    size_t print(const char* s) { size_t n = 0; while (s && *s) n += write((uint8_t)*s++); return n; }
    size_t println(const char* s) { size_t n = print(s); n += write('\n'); return n; }
};

#endif // _MOCK_ARDUINO_H
