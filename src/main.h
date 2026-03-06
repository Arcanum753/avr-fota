// #define VERSION_APP "1.5.30"
#define VERSION_WEB "1.10.30" // TODO do version control
#define SERIAL_NUMBER "0"
#define DEVMODULE_GPIO "gpio"
#define APP_BUILDDATE  __DATE__
#define APP_BUILDTIME  __TIME__


#define SEC 1000 // 1000 usecs
#define MINUTES 60  // 60 secs


#define NO_RST          0

//CONFIGS
#define HIDE_SECRET
#define HIDE_CONFIG

// #define PROGTYPE_ISP
// #define DEBUG_ISP
#define DEBUG_SHOWHEXBUF 0 // ISP

//#define PROGTYPE_SWD
// #define DEBUG_SWD



//#define RELEASE  // Comment to enable debug output

void printGitInfo() ;

void loop_user();
