// ============================================================
// Отладочный вывод с префиксом модуля
// ============================================================
// Префикс печатается только в начале новой строки (после '\n').
// Общее состояние dbg_line_start позволяет не плодить префикс
// при паттернах вида DEBUGLOG(__FUNCTION__); DEBUGLOG("\r\n");
// и корректно префиксовать каждую строку многострочных сообщений.

#include <Arduino.h>
#include <stdarg.h>

#include "debug.h"

bool dbg_line_start = true;   // true = следующий вывод начинается с новой строки

void dbg_printf(const char* prefix, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    for (int i = 0; i < (int)sizeof(buf) && buf[i] != '\0'; i++) {
        if (dbg_line_start) {
            Serial.print(prefix);
            dbg_line_start = false;
        }
        Serial.write(buf[i]);
        if (buf[i] == '\n') {
            dbg_line_start = true;
        }
    }
}
