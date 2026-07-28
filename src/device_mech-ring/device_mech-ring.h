#ifndef _DEVICE_RINGMECH_h
#define _DEVICE_RINGMECH_h

#include "main.h"

#ifdef DEBUG_RINGMECH
#define DEBUGRINGMECH(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGRINGMECH(...)
#endif

#include <LittleFS.h>
#define RINGMECH_DIR       33
#define RINGMECH_STEP      12
#define RINGMECH_EN        14
#define RINGMECH_SENS_LED  27
#define RINGMECH_SENS_MIN  26
#define RINGMECH_SENS_HOUR 25

#define RINGMECH_MIN_STEPS_GAP 5

#define RINGMECH_CounterClockWise  HIGH
#define RINGMECH_ClockWise         LOW

#define CONFIG_FILE_RINGMECH    "/config_ring-mech.json"

typedef enum {
    RING_STATUS_IDLE    = 0,
    RING_STATUS_SET1200,
    RING_STATUS_SETHOUR,
    RING_STATUS_SETMIN,
    RING_STATUS_POLL,
    RING_STATUS_COUNTING,
    RING_ERROR_NO_MECH
} ring_status_e;

typedef enum {
    RING_MODE_DEBUG = 0,
    RING_MODE_WORK  = 1
} ring_mode_e;

#define HOURINCIRCLE  12
#define MININHOUR     60
#define MINMAX        59
#define HOURCONTROLDEF 5
typedef void (*DPDR)(void);

void ringMechTerminalRegister() ;

typedef struct {
    uint8_t  enable_status;
    String   timeSource;
    uint16_t stepsPerRevolution;
    uint16_t pollInterval;
    uint16_t errorLimitSteps;
    bool     sensorLedEnabled;
} strRingMechConfig;

class MODULE_CLASS_RINGMECH {
public:
    MODULE_CLASS_RINGMECH(bool _in);
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
    static void cmdSave();
    static void cmdMode();
    static void cmdPoll();
    static void GetSens();
    static void PollTimeTask();
    static void cmdStatus();
    static void cmdSetArrows();
    static void cmdStepWeb(AsyncWebServerRequest *request);
    static void cmdDirWeb(AsyncWebServerRequest *request);
    static void cmdEnWeb(AsyncWebServerRequest *request);
    static void cmdSledWeb(AsyncWebServerRequest *request);
    static void cmdSensWeb(AsyncWebServerRequest *request);
    static void cmdNWeb(AsyncWebServerRequest *request);
    static void cmdResetWeb(AsyncWebServerRequest *request);
    static void cmdCountWeb(AsyncWebServerRequest *request);
    static void cmdStatusWeb(AsyncWebServerRequest *request);
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleInfo(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleReset(AsyncWebServerRequest *request);
    void handleCount(AsyncWebServerRequest *request);

    void CheckTime (uint8_t _inH, uint8_t _inM);
    //work
    void MechInitGPIOs();
    static void MechMoveStepDown();
    static void MechMoveStepUp();
    static void MechNCmdStep();

    //work
    static void MechSet1200_Setup();
    static void MechSet1200_Task();
    static void MechSet1200_endOk();
    static void MechSet1200_endFail();

    //work
    static void MechCountStepsSetup();
    static void MechCountStepsTask();
    static void MechCountStepsOk();
    static void MechCountStepsFail();

    static void MechSetArrows();

    static void MechSetArrowHourSetup();
    static void MechSetArrowHourTask();
    static void MechSetArrowHourEndOk();
    static void MechSetArrowHourEndFail();

    // static void MechSetArrowMinback();
    
    static void MechSetArrowMinSetup();
    static void MechSetArrowMinTask();
    static void MechSetArrowMinOk();
    static void MechSetArrowMinFail();
    

    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

protected:
    bool dumb;
    fs::LittleFSFS*     _fs;
    strRingMechConfig _config;
    uint16_t            _mechControlSteps;
    uint8_t             _timeHourReal;
    uint8_t             _timeMinReal;
    uint8_t             _timeMechHour;
    uint16_t            _timeMechMin;
    uint8_t             _ringStatus;
    uint16_t            _minPrev;
    int                 _sensorLedState;
    int                 _sensorLedStateHOUR;
    int                 _sensorLedStateMIN;
    uint8_t             mchCS = 0;
};

extern MODULE_CLASS_RINGMECH ModClassRingMech;

#endif
