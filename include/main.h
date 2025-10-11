#define VERSION_APP "1.5.13"
#define VERSION_WEB "1.10.15"
#define SERIAL_NUMBER "0"
#define DEVTYPE_GPIO "gpio"
#define DEVTYPE_AVR "avr"
#define DEVTYPE_SWD "stm32"
#define APP_BUILDDATE  __DATE__
#define APP_BUILDTIME  __TIME__

#define USE_RESERV_WIFI 1
#define NO_RST          0

#define NTP_TIMEOUT 3000 // milliseconds

#define SEC 1000 // 1000 usecs
#define MIN 60  // 60 secs


#define LOCALHOST '127.0.0.1'
#define UDP_PORT  40001


#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#endif

void loop_user();  
void ledInit();
    
