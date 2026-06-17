#ifndef _MODULE_TEMPLATE_h
#define _MODULE_TEMPLATE_h

#include "main.h"

#ifdef DEBUG_TEMPLATE
#define DEBUGTEMPLATE(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGTEMPLATE(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#define CONFIG_FILE_TEMPLATE    "/config_template.json"
#define HTML_FILE_TEMPLATE      "/template.html"
#define HTML_FILE_TEMPLATE2     "/template2.html"

// Структура конфига шаблона — все поля хранятся в одном config_template.json
typedef struct {
    String textField;
    uint16_t interval;
    uint8_t gpioPin;
    uint32_t baudRate;
    bool enableLogging;
} strTmplConfig;

class MODULE_CLASS_TEMPLATE {
public:
    MODULE_CLASS_TEMPLATE(bool _in);
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

    // Обработчики веб-запросов
    void handleTemplateInfo(AsyncWebServerRequest *request);
    void handleTemplate1Config(AsyncWebServerRequest *request);
    void handleTemplate2Config(AsyncWebServerRequest *request);
    void handleGpio(AsyncWebServerRequest *request);

    // Работа с конфигом
    void defaultConfigTemplate();
    bool load_config_template();
    bool save_config_template();

    String getTimeStr();

protected:
    bool dumb;
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;
#endif

    strTmplConfig _config;
};

extern MODULE_CLASS_TEMPLATE ModClassTemplate;

#endif // _MODULE_TEMPLATE_h
