#ifndef _DEVICE_RINGMECH_h
#define _DEVICE_RINGMECH_h

#include "main.h"

#ifdef DEBUG_RINGMECH
#define DEBUGRINGMECH(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGRINGMECH(...)
#endif

#include <LittleFS.h>
#define RINGMECH_STEP      17
#define RINGMECH_EN        16
#define RINGMECH_SENS      32

#define RINGMECH_MIN_STEPS_GAP 40

#define CONFIG_FILE_RINGMECH    "/config_ring-mech.json"

#define SENS_TRIGGERED  (digitalRead(RINGMECH_SENS) == LOW)

extern DPDR GoToTaskAfterStepRing ;
typedef enum {
    RING_STATUS_IDLE    = 0,
    RING_STATUS_HOMING,
    RING_STATUS_ROTATION,
    RING_STATUS_COUNTING,
    RING_STATUS_TURN,
    RING_ERROR_NO_MECH
} ring_status_e;

typedef enum {
    RING_MODE_DEBUG = 0,
    RING_MODE_WORK  = 1
} ring_mode_e;

typedef void (*DPDR)(void);

void ringMechTerminalRegister();

typedef struct {
    uint8_t  enable_status;
    uint16_t stepsPerRevolution;
    uint16_t pollInterval;
    uint16_t errorLimitSteps;
} strRingMechConfig;

class MODULE_CLASS_RINGMECH {
public:
    MODULE_CLASS_RINGMECH(bool _in);
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void webInit();

    static void cmdEn();
    static void cmdSens();
    static void cmdHome();
    static void cmdCount();
    static void cmdTurn();
    static void cmdSave();
    static void cmdMode();
    static void cmdStatus();
    static void cmdEnWeb(AsyncWebServerRequest *request);
    static void cmdSensWeb(AsyncWebServerRequest *request);
    static void cmdNWeb(AsyncWebServerRequest *request);
    static void cmdHomeWeb(AsyncWebServerRequest *request);
    static void cmdCountWeb(AsyncWebServerRequest *request);
    static void cmdTurnWeb(AsyncWebServerRequest *request);
    static void cmdStatusWeb(AsyncWebServerRequest *request);
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

    void handleInfo_ring(AsyncWebServerRequest *request);
    void handleSave(AsyncWebServerRequest *request);
    void handleHome(AsyncWebServerRequest *request);
    void handleCount(AsyncWebServerRequest *request);
    

    void MechInitGPIOs();
    static void MechMoveStepDown();
    static void MechMoveStepUp();
    static void MechNCmdStep();

    static void MechHomeSetup();
    static void MechHomeTask();
    static void MechHomeEndOk();
    static void MechHomeEndFail();

    static void MechRotationSetup();
    static void MechRotationTask();
    static void MechRotationOk();
    static void MechRotationFail();

    static void MechCountStepsSetup();
    static void MechCountStepsTask();
    static void MechCountStepsOk();
    static void MechCountStepsFail();

    static void MechTurnNCount();
    static void MechTurnNSetup();
    static void MechTurnNTask();
    static void MechTurnNEndOk();
    static void MechTurnNEndFail();

    static void RingPollTask();

    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

protected:
    bool dumb;
    fs::LittleFSFS*     _fs;
    strRingMechConfig   _config;
    uint16_t            _mechControlSteps;
    uint16_t            _mechTurnTarget;
    uint8_t             _ringStatus;
    int                 _sensorState;
};

extern MODULE_CLASS_RINGMECH ModClassRingMech;

#endif
