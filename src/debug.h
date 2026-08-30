// 0 disable showing debug // 1 show logic debug // 2 - show all bufs

#ifndef _DEBUGLOG_h
#define _DEBUGLOG_h

#include <stdarg.h>

//#define CHECKAUTH if (!ESPHTTPServer->checkAuth(request)) {	return request->requestAuthentication(); };


#define DBG_OUTPUT_PORT Serial

// Префикс модуля: печатается только в начале новой строки.
extern bool dbg_line_start;
void dbg_printf(const char* prefix, const char* fmt, ...);
#define DBG_MOD(prefix, ...) dbg_printf((prefix), __VA_ARGS__)

#ifndef RELEASE
#define DEBUGLOG(...) DBG_MOD("[C_HTTP] ", __VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif

#define DBG_HADLEFILEWXIST  0

#if (DBG_HADLEFILEWXIST > 0 )
#define DEBUGLOGFH(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOGFH(...)
#endif








#endif // _DEBUGLOG_h