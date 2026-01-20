// 0 disable showing debug // 1 show logic debug // 2 - show all bufs

#ifndef _DEBUGLOG_h
#define _DEBUGLOG_h

#if defined(ARDUINO) && ARDUINO >= 100
    #include <Arduino.h>
#else
    #include "WProgram.h"
#endif


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





#define DEBUG_SHOWHEXBUF 2

#define DBG_ISP_OUTPUT_PORT Serial


// #ifndef RELEAS_AVRISP
#if (DEBUG_SHOWHEXBUF > 1)
#define DEBUGLOGISP(...) DBG_ISP_OUTPUT_PORT.printf(__VA_ARGS__)

#else
#define DEBUGLOGISP(...)
#endif


// #ifndef RELEAS_AVRISP
#if (DEBUG_SHOWHEXBUF > 2)
#define DEBUGLOGISPBUF(...) DBG_ISP_OUTPUT_PORT.printf(__VA_ARGS__)

#else
#define DEBUGLOGISPBUF(...)
#endif



#endif // _DEBUGLOG_h