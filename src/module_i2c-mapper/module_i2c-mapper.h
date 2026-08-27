#ifndef _MODULE_I2C_MAPPER_h
#define _MODULE_I2C_MAPPER_h

#include "main.h"

#include "mod_context.h"

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

class CLASS_MODULE_I2C_MAPPER {
public:
    CLASS_MODULE_I2C_MAPPER(bool _in);
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleScan(AsyncWebServerRequest *request);

protected:
    bool dumb;
};

extern CLASS_MODULE_I2C_MAPPER module_i2c_mapper;

#endif // _MODULE_I2C_MAPPER_h
