#ifndef _MOCK_PGMSPACE_H
#define _MOCK_PGMSPACE_H

// Хост-заглушка AVR pgmspace: PROGMEM-строки доступны как обычные.

#include <string.h>
#include <stdint.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(x) (x)
#endif

static inline size_t strlen_P(const char* p) { return strlen(p); }
static inline char   pgm_read_byte(const void* p) { return *(const char*)p; }
static inline char   pgm_read_byte_near(const void* p) { return *(const char*)p; }
static inline uint16_t pgm_read_word(const void* p) { return *(const uint16_t*)p; }
static inline uint32_t pgm_read_dword(const void* p) { return *(const uint32_t*)p; }

#endif // _MOCK_PGMSPACE_H
