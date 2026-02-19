#ifndef _MODULE_GPIO_h
#define _MODULE_GPIO_h
#include "main.h"

#ifdef DEBUG_GPIO
#define DEBUGGPIO(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGGPIO(...)
#endif

#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif



class  MODULE_CLASS_GPIO    {
public:    
    MODULE_CLASS_GPIO (bool _in);
#if ESP32
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif
    void begin();
    void webInit();
    
protected: 
bool  dumb = false;
#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif


void  gpioGetArgs(AsyncWebServerRequest *request) ;


};

extern MODULE_CLASS_GPIO ModClassGpio;




#endif // _MODULE_GPIO_h
