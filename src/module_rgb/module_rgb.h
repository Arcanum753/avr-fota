#ifndef _MODULE_RGB_h
#define _MODULE_RGB_h

#include "main.h"

#include <math.h>

#ifdef DEBUG_RGB
#define DEBUGRGB(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGRGB(...)
#endif

#include <LittleFS.h>
#include <NeoPixelBus.h>

#if defined(ESP32)
typedef NeoPixelBus<NeoGrbFeature, NeoEsp32Rmt0Ws2812xMethod> NeoPixelBusType;
#endif

#if defined(ESP8266)
typedef NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart1Ws2812xMethod> NeoPixelBusType;
#endif

#define CONFIG_FILE_RGB    "/config_rgb.json"
#define RGB_MAX_LEDS       100
#define RGB_DEFAULT_LEDS   10

typedef struct {
    int16_t dataPin;
    uint8_t numLeds;
    uint8_t brightness;
    uint8_t mode;
    uint16_t effectSpeed;
    uint32_t solidColor;
    uint32_t gradStartColor;
    uint32_t gradEndColor;
    uint32_t individualColors[RGB_MAX_LEDS];
    uint8_t eqBands;
    uint8_t eqLedsPerBand;
} strRgbConfig;

class MODULE_CLASS_RGB {
public:
    MODULE_CLASS_RGB();
#if defined(ESP32)
    void setFs(fs::LittleFSFS* fs);
#endif
#if defined(ESP8266)
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
    void handleSave(AsyncWebServerRequest *request);
    void handleSetPixel(AsyncWebServerRequest *request);

    void defaultConfigRgb();
    bool loadConfigRgb();
    bool saveConfigRgb();

    void initStrip();
    void deleteStrip();
    void applyMode();
    void applySolid();
    void applyRainbow();
    void applyGradient();
    void applyIndividual();
    void applyEqualizer();

    static void animationTimerTask();
    static void deferredApplyTask();

protected:
#if defined(ESP32)
    fs::LittleFSFS* _fs;
#endif
#if defined(ESP8266)
    FS* _fs;
#endif

    strRgbConfig _config;
    NeoPixelBusType* _strip;
    uint8_t _hue;
    bool _animationRunning;
    bool _pendingReinit;
    bool _pendingSave;
    bool _pendingApply;
    int16_t _pendingDataPin;
    uint8_t _pendingNumLeds;
};

extern MODULE_CLASS_RGB ModClassRgb;

#endif
