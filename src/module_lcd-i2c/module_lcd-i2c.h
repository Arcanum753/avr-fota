#ifndef _MODULE_LCD_I2C_h
#define _MODULE_LCD_I2C_h

#include "main.h"
#include <LiquidCrystal_I2C.h>

#include "mod_context.h"

#ifdef DEBUG_LCD
#define DEBUGLCD(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLCD(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#if defined(ESP32)
#define LCD_I2C_SDA  21
#define LCD_I2C_SCL  22
#elif defined(ESP8266)
#define LCD_I2C_SDA  4
#define LCD_I2C_SCL  5
#endif

#define CONFIG_FILE_LCD_I2C       "/config_lcd-i2c.json"
#define LCD_I2C_MAX_ROWS          4
#define LCD_I2C_MAX_LINE_LEN      40

typedef struct {
    uint8_t i2cAddr;
    uint8_t cols;
    uint8_t rows;
    bool    backlight;
} strLcdConfig;

class CLASS_MODULE_I2C_LCD {
public:
    CLASS_MODULE_I2C_LCD(bool _in);
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleInfo(AsyncWebServerRequest *request);
    void handleSysInfo(AsyncWebServerRequest *request);
    void handleSaveContent(AsyncWebServerRequest *request);
    void handleSaveConfig(AsyncWebServerRequest *request);

    void defaultConfigLcd();
    bool load_config();
    bool save_config();

    void _initDisplay();
    void _applyLines();
    static void _lcdUpdateTask();

protected:
    bool dumb;
#if ESP32
    fs::LittleFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;
#endif

    strLcdConfig       _config;
    String             _displayLines[LCD_I2C_MAX_ROWS];
    LiquidCrystal_I2C* _lcd;
};

extern CLASS_MODULE_I2C_LCD module_lcd_i2c;

#endif
