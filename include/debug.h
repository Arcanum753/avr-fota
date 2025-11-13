// 0 disable showing debug // 1 show logic debug // 2 - show all bufs
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
