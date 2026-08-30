#ifndef _DEVICE_CLOCKMECH_h
#define _DEVICE_CLOCKMECH_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_CLOCKMECH
#define DEBUGCLOCKMECH(...) DBG_MOD("[D_CLOCKMECH] ", __VA_ARGS__)
#else
#define DEBUGCLOCKMECH(...)
#endif

#include <LittleFS.h>
#define CLOCKMECH_DIR       33
#define CLOCKMECH_STEP      12
#define CLOCKMECH_EN        14
#define CLOCKMECH_SENS_LED  27
#define CLOCKMECH_SENS_MIN  26
#define CLOCKMECH_SENS_HOUR 25

#define CLOCKMECH_MIN_STEPS_GAP 5

#define CLOCKMECH_CounterClockWise  HIGH
#define CLOCKMECH_ClockWise         LOW

#define CONFIG_FILE_CLOCKMECH    "/config_clock-mech.json"

typedef enum {
    STATUS_IDLE    = 0,
    STATUS_SET1200,
    STATUS_SETXX00,
    STATUS_SET12XX,
    STATUS_SETHOUR,
    STATUS_SETMIN,
    STATUS_POLL,
    STATUS_COUNTING,
    ERROR_NO_MECH,
    ERROR_NO_MIN,
    ERROR_NO_HOUR,
} mech_status_e;

typedef enum {
    DIR_clockwise = 0,
    DIR_COUNTERclockwise  = 1
} mech_direction_e;

typedef enum {
    MODE_DEBUG = 0,
    MODE_WORK  = 1
} mech_mode_e;

#define     HOURINCIRCLE				12
#define     MININHOUR					60
#define     MINMAX						59
#define     HOURCONTROLDEF              5	
typedef void (*DPDR)(void);
void clockMechTerminalRegister() ;

typedef struct {
    uint8_t  enable_status;
    String   timeSource;
    uint16_t stepsPerRevolution;
    uint16_t pollInterval;
    uint16_t errorLimitSteps;
    bool     sensorLedEnabled;
} strClockMechConfig;

class CLASS_DEVICE_CLOCKMECH {
public:
    CLASS_DEVICE_CLOCKMECH(bool _in);
    void setFs(fs::LittleFSFS* fs);
    void begin();
    void begin(ModContext& ctx);
    void web_Init();
    time_t getCurrentTime();
    
    
    static void cmdStep();
    static void cmdDir();
    static void cmdEn();
    static void cmdSled();
    static void cmdSens();
    static void cmdN();
    static void cmdSet1200();
    static void cmdSetxx00();
    static void cmdSet12xx();
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
    static void cmdSetxx00Web(AsyncWebServerRequest *request);
    static void cmdSet12xxWeb(AsyncWebServerRequest *request);
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

    // Конфиг
    void defaultConfig();
    bool loadConfig();
    bool saveConfig();

    // Логика устройства
    void MechTimeSet (uint8_t _inH, uint8_t _inM);
    //work
    void MechInitGPIOs();
    static void MechMoveStepDown();
    static void MechMoveStepUp();
    static void MechNCmdStep();
    
    //set xx:00
    static void MechSetxx00_Setup();
    static void MechSetxx00_Task();
    static void MechSetxx00_endOk();
    static void MechSetxx00_endFail();


    //set12:xx
    static void MechSet12xx_Setup();
    static void MechSet12xx_Task();
    static void MechSet12xx_endOk();
    static void MechSet12xx_endFail();

    //set12:00
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

protected:
    bool dumb;
    fs::LittleFSFS*     _fs;
    strClockMechConfig _config;
    uint16_t            _mechControlSteps;
    uint8_t             _timeHourReal;
    uint8_t             _timeMinReal;
    uint8_t             _timeMechHour;
    uint16_t            _timeMechMin;
    uint8_t             _Mech_Status;
    uint16_t            _minPrev;
    int                 _sensorLedState;
    int                 _sensorLedStateHOUR;
    int                 _sensorLedStateMIN;
};

extern CLASS_DEVICE_CLOCKMECH device_clock_mech;

#endif
