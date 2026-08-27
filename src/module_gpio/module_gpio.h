#ifndef _MODULE_GPIO_h
#define _MODULE_GPIO_h
#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_GPIO
#define DEBUGGPIO(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGGPIO(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif



class  CLASS_MODULE_GPIO    {
public:    
    CLASS_MODULE_GPIO (bool _in);
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif
    void begin();
    void begin(ModContext& ctx);
    void web_Init();
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
protected: 
bool  dumb = false;
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif


void  gpioGetArgs(AsyncWebServerRequest *request) ;


};

extern CLASS_MODULE_GPIO module_gpio;




#endif // _MODULE_GPIO_h
