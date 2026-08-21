#ifndef _MODULE_I2C_MAPPER_h
#define _MODULE_I2C_MAPPER_h

#include "main.h"

#ifdef DEBUG_I2C_MAPPER
#define DEBUGI2CMAPPER(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGI2CMAPPER(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#include <esp_task_wdt.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#if defined(ESP32)
#define I2C_MAPPER_SDA  21
#define I2C_MAPPER_SCL  22
#elif defined(ESP8266)
#define I2C_MAPPER_SDA  4
#define I2C_MAPPER_SCL  5
#endif

class MODULE_CLASS_I2C_MAPPER {
public:
    MODULE_CLASS_I2C_MAPPER(bool _in);
    void begin();
    void webInit();

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleScan(AsyncWebServerRequest *request);

protected:
    bool dumb;
};

extern MODULE_CLASS_I2C_MAPPER ModClassI2cMapper;

#endif // _MODULE_I2C_MAPPER_h
