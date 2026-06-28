#ifndef _DEVICE_CLOCKMECH_h
#define _DEVICE_CLOCKMECH_h

#include "main.h"

#ifdef DEBUG_CLOCKMECH
#define DEBUGCLOCKMECH(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGCLOCKMECH(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#define CONFIG_FILE_CLOCKMECH    "/config_clock-mech.json"

typedef struct {
    bool enabled;
    uint16_t triggerHour;
    uint16_t triggerMinute;
} strClockMechConfig;

class MODULE_CLASS_CLOCKMECH {
public:
    MODULE_CLASS_CLOCKMECH(bool _in);
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void webInit();

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleInfo(AsyncWebServerRequest *request);
    void handleConfig(AsyncWebServerRequest *request);

    void defaultConfigClockMech();
    bool load_config_clockmech();
    bool save_config_clockmech();

protected:
    bool dumb;
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;
#endif

    strClockMechConfig _config;
};

extern MODULE_CLASS_CLOCKMECH ModClassClockMech;

#endif // _DEVICE_CLOCKMECH_h
