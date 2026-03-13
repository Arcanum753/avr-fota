#ifndef _MODULE_EDITOR_h
#define _MODULE_EDITOR_h
#include "main.h"


#ifdef DEBUG_EDITOR
#define DEBUGEDIT(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGEDIT(...)
#endif

#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif




class  CORE_CLASS_EDITOR    {
public:    
    CORE_CLASS_EDITOR (bool _in);
#if ESP32
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif
    void begin();
    void webInit();
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    

protected: 
#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif

bool  dumb = false;

public:    
    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
};

extern CORE_CLASS_EDITOR ModClassEdit;




#endif // _MODULE_EDITOR_h
