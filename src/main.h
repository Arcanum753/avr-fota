// #define VERSION_APP "1.5.30"
#define DEVMODULE_GPIO "gpio"
#define APP_BUILDDATE  __DATE__
#define APP_BUILDTIME  __TIME__


#define SEC 1000 // 1000 usecs
#define MINUTES 60  // 60 secs


#define NO_RST          0

//CONFIGS
#define HIDE_SECRET
// #define HIDE_CONFIG

#define DEBUG_SHOWHEXBUF 0 // ISP

#ifndef  CONNECTION_LED
#define CONNECTION_LED -1
#endif

#include "debug.h"

//#define RELEASE  // Comment to enable debug output

void printGitInfo() ;

bool isFsMounted() ;