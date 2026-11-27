#include "Arduino.h"
#include "ArduinoOTA.h"
#include "Update.h"
// Пути относительные: библиотека mocks собирается своим env и не получает -I<repo>/src.
#include "../../../../src/debug.h"
#include "../../../../src/mod_context.h"
#include "core_web/FSWebServerLib.h"

#include <stdarg.h>
#include <stdio.h>

// --- Отладочный префикс: no-op, чтобы вывод был детерминирован ---
bool dbg_line_start = true;
void dbg_printf(const char* prefix, const char* fmt, ...) {
#if defined(MOCK_SERIAL_VERBOSE)
    (void)prefix;
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
#endif
#if !defined(MOCK_SERIAL_VERBOSE)
    (void)prefix; (void)fmt;
#endif
}

// --- Глобальный контекст приложения (в прошивке заполняется веб-сервером) ---
ModContext g_ctx;

// --- Определения, обычно живущие в main.cpp / FSWebServerLib.cpp ---
MockArduinoOTA ArduinoOTA;
MockUpdate Update;
AsyncFSWebServer ESPHTTPServer(80);

void printGitInfo() {}
bool isFsMounted() { return true; }
