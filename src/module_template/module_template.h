#ifndef _MODULE_TEMPLATE_h
#define _MODULE_TEMPLATE_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_TEMPLATE
#define DEBUGTEMPLATE(...) DBG_MOD("[M_TEMPLATE] ", __VA_ARGS__)
#else
#define DEBUGTEMPLATE(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#include "module_template_types.h"

class CLASS_MODULE_TEMPLATE {
public:
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void begin(ModContext& ctx);
    void web_Init();
    void terminalRegister();

    // Демо-кассета светодиодной индикации (пример: как модуль носит свою кассету)
    void ledMacrosTemplateDemo();

private:
    // Терминальные команды: статические методы-члены, поэтому имеют
    // прямой доступ к приватному состоянию модуля (см. _terminal.cpp).
    static void s_terminalCmd();
    static void s_cmdShow();
    static void s_cmdGpio();
    static void s_cmdBlink();
    static void s_cmdText();
    static void s_cmdSave();

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
    void applyGpioTarget();
    void applyBlinkTimer();
    static void blinkTimerTask();

protected:
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;
#endif

    strTmplConfig _config;
    bool _blinkState;
    // true — в памяти есть изменения, которых нет на FS (правило apply/save).
    bool _configDirty;
};

extern CLASS_MODULE_TEMPLATE module_template;

#endif // _MODULE_TEMPLATE_h
