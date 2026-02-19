#define VERSION_APP "1.5.30"
#define VERSION_WEB "1.10.30"
#define SERIAL_NUMBER "0"
#define DEVTYPE_GPIO "gpio"
#define APP_BUILDDATE  __DATE__
#define APP_BUILDTIME  __TIME__

#define NO_RST          0


#define SEC 1000 // 1000 usecs
#define MINUTES 60  // 60 secs



//CONFIGS
#define HIDE_SECRET
#define HIDE_CONFIG

// #define PROGTYPE_ISP
// #define DEBUG_ISP
#define DEBUG_SHOWHEXBUF 0 // ISP

//#define PROGTYPE_SWD
// #define DEBUG_SWD


#define DEBUG_OTA
#define DEBUG_EDITOR
#define DEBUG_NTP 
#define DEBUG_UDP 
#define DEBUG_WIFI
#define DEBUG_JSON
#define DEBUG_GPIO


//#define RELEASE  // Comment to enable debug output

void loop_user();
void ledInit();
