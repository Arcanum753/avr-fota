// 0 disable showing debug // 1 show logic debug // 2 - show all bufs

#ifndef _DEBUGLOG_h
#define _DEBUGLOG_h




//#define CHECKAUTH if (!ESPHTTPServer->checkAuth(request)) {	return request->requestAuthentication(); };


#define DBG_OUTPUT_PORT Serial

#ifndef RELEASE
#define DEBUGLOG(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
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