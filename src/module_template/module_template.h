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

// Пины для двух GPIO, управляемых с веб-страницы
#if defined(ESP32)
#define TEMPLATE_GPIO1  32   // D32
#define TEMPLATE_GPIO2  33   // D33
#elif defined(ESP8266)
#define TEMPLATE_GPIO1  16   // D0
#define TEMPLATE_GPIO2  14   // D5
#endif

#define CONFIG_FILE_TEMPLATE    "/config_template.json"

// Структура конфига — сохраняется в config_template.json
typedef struct {
    bool gpio1State;
    bool gpio2State;
    uint16_t blinkInterval;   // период моргания в мс, 0 = не моргать
    String demoSampleText;    // демо-поле, ни на что не влияет
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
    void handleInfo(AsyncWebServerRequest *request);
    void handleTime(AsyncWebServerRequest *request);
    void handleConfigGpio(AsyncWebServerRequest *request);
    void handleConfigDemo(AsyncWebServerRequest *request);

    // Работа с конфигом
    void defaultConfigTemplate();
    bool load_config_template();
    bool save_config_template();

    // Логика моргания
    void applyGpioState();
    static void blinkTimerTask();

protected:
    bool dumb;
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;
#endif

    strTmplConfig _config;
    bool _blinkState;
};

extern MODULE_CLASS_TEMPLATE ModClassTemplate;

#endif // _MODULE_TEMPLATE_h
