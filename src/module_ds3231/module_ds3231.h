#ifndef _MODULE_DS3231_h
#define _MODULE_DS3231_h

#include "main.h"

#ifdef DEBUG_DS3231
#define DEBUGDS3231(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGDS3231(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#define CONFIG_FILE_DS3231       "/config_ds3231.json"
#define DS3231_SCAN_RETRIES      2
#define DS3231_DEFAULT_ADDR      0x68
#define DS3231_ADDR_NONE         0

typedef struct {
    uint8_t  addr;
    bool     autoPoll;
    uint16_t pollInterval;
} strDs3231Config;

class MODULE_CLASS_DS3231 {
public:
    MODULE_CLASS_DS3231(bool _in);
#if defined(ESP32)
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void webInit();

    // ===== Публичное API для других модулей =====
    time_t getTime();
    bool setTime(time_t t);
    bool setTime(int yr, int mon, int day, int hr, int min, int sec);
    bool getAlarm1(uint8_t &hour, uint8_t &min, uint8_t &sec, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek);
    bool setAlarm1(uint8_t hour, uint8_t min, uint8_t sec, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek);
    bool getAlarm2(uint8_t &hour, uint8_t &min, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek);
    bool setAlarm2(uint8_t hour, uint8_t min, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek);
    bool getTemperature(float &temp);
    bool isConnected();
    uint8_t getAddr();
    int getLastError();

private:
    // Версионные методы
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    // Веб-обработчики
    void handleRead(AsyncWebServerRequest *request);
    void handlePoll(AsyncWebServerRequest *request);
    void handleSetTime(AsyncWebServerRequest *request);
    void handleSetAlarm1(AsyncWebServerRequest *request);
    void handleSetAlarm2(AsyncWebServerRequest *request);
    void handleSetReg(AsyncWebServerRequest *request);
    void handleSaveConfig(AsyncWebServerRequest *request);
    void handleInfo(AsyncWebServerRequest *request);

    // I2C
    bool _detectDS3231(uint8_t addr);
    uint8_t _scanForDS3231();
    uint8_t _readReg(uint8_t reg);
    bool _writeReg(uint8_t reg, uint8_t val);
    bool _readBlock(uint8_t reg, uint8_t *buf, uint8_t len);
    bool _writeBlock(uint8_t reg, uint8_t *buf, uint8_t len);

    // Время
    time_t _readTime();
    bool _writeTime(time_t t);

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

protected:
    bool dumb;
#if defined(ESP32)
    fs::LittleFSFS* _fs;
#elif defined(ESP8266)
    FS* _fs;
#endif
    strDs3231Config _config;
    int _lastError;
    bool _wireStarted;
};

extern MODULE_CLASS_DS3231 ModClassDs3231;

#endif // _MODULE_DS3231_h
