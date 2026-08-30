#ifndef _MODULE_DS3231_h
#define _MODULE_DS3231_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_DS3231
#define DEBUGDS3231(...) DBG_MOD("[M_DS3231] ", __VA_ARGS__)
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

// Режимы чтения вывода SQW/INT# — настраиваются на странице модуля
#define DS3231_SQW_MODE_POLLING     0   // опрос через EERTOS timer
#define DS3231_SQW_MODE_INTERRUPT   1   // прерывание (attachInterrupt)

// Пин для вывода SQW/INT# DS3231 (только ESP32). По умолчанию D13 (DevKit v1)
#if defined(ESP32)
#ifndef DS3231_SQW_PIN
#define DS3231_SQW_PIN 13
#endif
#endif

typedef struct {
    uint8_t  addr;
    bool     autoPoll;
    uint16_t pollInterval;
    // === SQW / GPIO (только ESP32) ===
    bool     sqwEnabled;     // вкл/выкл GPIO мониторинг
    uint8_t  sqwMode;        // DS3231_SQW_MODE_POLLING / _INTERRUPT
    bool     sqwLevelActive; // активный уровень (false=LOW, true=HIGH)
    // === биты Control (0x0E) ===
    bool     ctrlBbsqw;      // BBSQW: 0=INT#, 1=меандр на выходе SQW
    uint8_t  ctrlRs;         // RS[1:0]: 0=1Гц,1=1.024кГц,2=4.096кГц,3=8.192кГц
    bool     ctrlIntcn;      // INTCN: 1=INT по будильникам, 0=выход SQW
    bool     ctrlA1ie;       // A1IE — разрешить прерывание Alarm 1
    bool     ctrlA2ie;       // A2IE — разрешить прерывание Alarm 2
} strDs3231Config;

class CLASS_MODULE_DS3231 {
public:
    CLASS_MODULE_DS3231(bool _in);
#if defined(ESP32)
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

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
    uint8_t getStatusReg();

    // ===== SQW / GPIO (только ESP32) =====
#if defined(ESP32)
    void sqwGpioInit();                // инициализация GPIO SQW
    void sqwGpioReinit();              // переинициализация после смены конфига
    void sqwGpioStop();                // отключение мониторинга
    void sqwEnableInterrupt();         // attachInterrupt
    void sqwDisableInterrupt();        // detachInterrupt
    bool getSqwLevel();                // текущий уровень GPIO SQW
    bool getAlarmFired1();
    bool getAlarmFired2();
    time_t getLastAlarm1Time();   // 0 = не срабатывал с момента старта
    time_t getLastAlarm2Time();   // 0 = не срабатывал с момента старта
#endif

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

    // SQW / GPIO — внутренняя логика (только ESP32)
#if defined(ESP32)
    void sqwPollStep();                // шаг опроса через EERTOS
    void _handleAlarmFired(uint8_t alarmNum, uint8_t mode,
                           uint8_t hour, uint8_t min, uint8_t sec,
                           uint8_t dayOrDate, bool isDayOfWeek); // обработка срабатывания будильника
    void sqwSetIrqFlag();              // вызывается из ISR
    void checkSqw();                   // обработка _sqwIrqFlag (edge детекция)
    void checkAlarmFlags();            // проверка флагов будильников A1F/A2F и вывод отладочного сообщения
    bool _ctrlBitsToReg();             // собрать Control (0x0E) из полей конфига
    void _applyCtrlBits();             // записать биты Control из конфига
    void emitSqwFields(String &values); // поля SQW и статуса будильников в ответ веб
    void emitAlarmState(String &values); // статус сработавших будильников
#endif

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
#if defined(ESP32)
    volatile bool _sqwIrqFlag;      // флаг срабатывания от ISR
    bool          _sqwLastLowEdge;  // состояние уровня для edge-детекции
    bool          _sqwCareActive;   // флаг «активный сигнал замечен»
    bool          _alarm1Fired;     // однократное срабатывание Alarm 1 (фронт)
    bool          _alarm2Fired;     // однократное срабатывание Alarm 2 (фронт)
    time_t        _lastAlarm1At;    // время последнего срабатывания Alarm 1 (0=нет)
    time_t        _lastAlarm2At;    // время последнего срабатывания Alarm 2 (0=нет)
    bool          _sqwInterrupting; // прерывание сейчас подключено
    friend void ds3231SqwIsr();      // ISR обрабатывает флаг через метод sqwSetIrqFlag
    friend void ds3231SqwPollTask(); // EERTOS-задача опроса
    friend void ds3231CmdAlarm();    // терминальная команда ds-alarm
    friend void ds3231CmdSqw();      // терминальная команда ds-sqw
    friend void ds3231CmdSqr();      // терминальная команда ds-sqr
#endif
};

extern CLASS_MODULE_DS3231 module_ds3231;

// SQW / GPIO — внешние функции (только ESP32)
#if defined(ESP32)
void ds3231SqwIsr();                 // ISR для attachInterrupt
void ds3231SqwPollTask();            // периодическая задача опроса
#endif

// Терминальные команды модуля (регистрируются через TerminalRegisterModule)
void ds3231TerminalRegister();

#endif // _MODULE_DS3231_h
