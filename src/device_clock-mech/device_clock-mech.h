#ifndef _DEVICE_CLOCKMECH_h
#define _DEVICE_CLOCKMECH_h

#include "main.h"

#ifdef DEBUG_CLOCKMECH
#define DEBUGCLOCKMECH(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGCLOCKMECH(...)
#endif

#include <LittleFS.h>
#define CLOCKMECH_DIR       33
#define CLOCKMECH_STEP      12
#define CLOCKMECH_EN        14
#define CLOCKMECH_SENS_LED  27
#define CLOCKMECH_SENS_HOUR 25
#define CLOCKMECH_SENS_MIN  26

#define CLOCKMECH_MIN_STEPS_GAP 30

#define CLOCKMECH_CounterClockWise  LOW
#define CLOCKMECH_ClockWise         HIGH

#define CONFIG_FILE_CLOCKMECH    "/config_clock-mech.json"

#define STATUS_IDLE      0
#define STATUS_SET1200   1
#define STATUS_WORKING   2
#define STATUS_COUNTING  3
#define ERROR_NO_MECH    4

typedef void (*DPDR)(void);
extern DPDR GoToTaskAfter;

typedef struct {
    bool     enabled;
    String   timeSource;
    uint16_t triggerHour;
    uint16_t triggerMinute;
    uint16_t stepsPerRevolution;
    uint16_t pollInterval;
    uint16_t stepTime;
    uint16_t errorLimitSteps;
    bool     sensorLedEnabled;
} strClockMechConfig;

class MODULE_CLASS_CLOCKMECH {
public:
    MODULE_CLASS_CLOCKMECH(bool _in);
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void webInit();
    time_t getCurrentTime();

    static void cmdStep();
    static void cmdDir();
    static void cmdEn();
    static void cmdSled();
    static void cmdSens();
    static void cmdN();
    static void cmdSet1200();
    static void cmdCount();
    
    static void GetSens();
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleInfo(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleReset(AsyncWebServerRequest *request);
    void handleCount(AsyncWebServerRequest *request);

    void MechInitPorts();
    static void MechMoveStepDown();
    static void MechMoveStepUp();
    static void MechNCmdStep();


    static void MechSet1200_Setup();
    static void MechSet1200_Task();
    static void MechSet1200_endOk();
    static void MechSet1200_endFail();

    
    static void MechCountStepsSetup();
    static void MechCountStepsTask();
    static void MechCountStepsOk();
    static void MechCountStepsFail();

    static void MechSetArrows();
    static void MechSetArrowHour();
    static void MechSetArrowMin();
    void CheckAndSync();
    static void PollTimeTask();

    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

protected:
    bool dumb;
    fs::LittleFSFS*       _fs;
    strClockMechConfig _config;
    uint16_t _mechControlSteps;
    uint8_t  _mechStepPhase;
    uint16_t _timeMechMin;
    uint8_t  _timeMechHour;
    uint16_t _timeMinReal;
    uint8_t  _timeHourReal;
    uint8_t  _status;
    bool     _needSync;
    int     _sensorLedState;
    int     _sensorLedStateHOUR;
    int     _sensorLedStateMIN;
};

extern MODULE_CLASS_CLOCKMECH ModClassClockMech;

#endif
