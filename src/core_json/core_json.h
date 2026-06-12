#ifndef _MODULE_JSON_h
#define _MODULE_JSON_h
#include "main.h"

#ifdef DEBUG_JSON
#define DEBUGJSON(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGJSON(...)
#endif

#include "FSWebServerLib.h"


#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif


class  CORE_CLASS_JSON    {
    
public:    
    CORE_CLASS_JSON (bool _in);
    void webInit(void);
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
protected: 
    #if ESP32
    fs::LittleFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    
public:
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif


    bool save_jsonDoc(const JsonDocument& jsonDoc, const String& file);
    bool load_jsonDoc(const String& file, JsonDocument& jsonDoc);
    

protected: 
    bool  dumb = false;

};

extern CORE_CLASS_JSON ModClassJson;


#endif // _MODULE_JSON_h
