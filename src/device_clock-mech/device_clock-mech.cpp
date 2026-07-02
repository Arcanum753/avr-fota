#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_clock-mech.h"
#include "common.h"
#include "device_clock-mech_version.h"
#include "eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#if defined(MODULE_DS3231)
#include "module_ds3231/module_ds3231.h"
#endif

#include <TimeLib.h>

DPDR GoToTaskAfter = Idle_task;

static uint16_t _nStepCount = 0;

static void clockMechTerminalRegister() {
    term.addCommand("c-step",   MODULE_CLASS_CLOCKMECH::cmdStep);
    term.addCommand("c-dir",    MODULE_CLASS_CLOCKMECH::cmdDir);
    term.addCommand("c-en",     MODULE_CLASS_CLOCKMECH::cmdEn);
    term.addCommand("c-sled",   MODULE_CLASS_CLOCKMECH::cmdSled);
    term.addCommand("c-sens",   MODULE_CLASS_CLOCKMECH::cmdSens);
    term.addCommand("c-n",      MODULE_CLASS_CLOCKMECH::cmdN);
    term.addCommand("c-12",     MODULE_CLASS_CLOCKMECH::cmdSet1200);
    term.addCommand("c-cnt",    MODULE_CLASS_CLOCKMECH::cmdCount);
}

MODULE_CLASS_CLOCKMECH ModClassClockMech(false);
MODULE_CLASS_CLOCKMECH::MODULE_CLASS_CLOCKMECH(bool _in) { dumb = _in; }
void MODULE_CLASS_CLOCKMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }

// ============================================================
// Время из источника
// ============================================================
time_t MODULE_CLASS_CLOCKMECH::getCurrentTime() {
#if defined(MODULE_DS3231)
    if (_config.timeSource == "ds3231") {
        time_t t = ModClassDs3231.getTime();
        if (t > 0) { return t; }
        DEBUGCLOCKMECH("DS3231 error, fallback to NTP\r\n");
    }
#endif
    return now();
}

// ============================================================
// begin()
// ============================================================
void MODULE_CLASS_CLOCKMECH::begin() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    _mechControlSteps = 0;
    _mechStepPhase = 0;
    _timeMechMin = 0;
    _timeMechHour = 0;
    _timeMinReal = 0;
    _timeHourReal = 0;
    _status = STATUS_IDLE;
    _needSync = false;
    _sensorLedState = digitalRead(CLOCKMECH_SENS_LED);

    TerminalRegisterModule(clockMechTerminalRegister);

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    MechInitPorts();
    if (_config.enabled) {SetTask(MechSet1200_Setup); }
    // SetTask(MechSet1200_Setup);
}

// ============================================================
// webInit()
// ============================================================
void MODULE_CLASS_CLOCKMECH::webInit() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/clock-mech/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    ESPHTTPServer.on("/clock-mech/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/clock-mech/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleReset(request);
    });

    ESPHTTPServer.on("/clock-mech/count", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleCount(request);
    });

    ESPHTTPServer.on("/clock-mech/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================
void MODULE_CLASS_CLOCKMECH::handleInfo(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "enabled|"              + String(_config.enabled ? "checked" : "")     + "|chk\n";
    values += "timeSource|"           + _config.timeSource                           + "|select\n";
    values += "triggerHour|"          + String(_config.triggerHour)                  + "|input\n";
    values += "triggerMinute|"        + String(_config.triggerMinute)                + "|input\n";
    values += "stepsPerRevolution|"   + String(_config.stepsPerRevolution)           + "|input\n";
    values += "pollInterval|"         + String(_config.pollInterval)                 + "|input\n";
    values += "stepTime|"             + String(_config.stepTime)                     + "|input\n";
    values += "errorLimitSteps|"      + String(_config.errorLimitSteps)              + "|input\n";
    values += "sensorLedEnabled|"     + String(_config.sensorLedEnabled ? "checked" : "") + "|chk\n";
    values += "status|"               + String(_status)                              + "|div\n";
    values += "mechMin|"              + String(_timeMechMin)                         + "|div\n";
    values += "mechHour|"             + String(_timeMechHour)                        + "|div\n";
    values += "mechControlSteps|"     + String(_mechControlSteps)                    + "|div\n";

    String timeStr = "N/A";
    time_t t = getCurrentTime();
    if (t > 0) {
        timeStr = String(hour(t)) + ":" + String(minute(t)) + ":" + String(second(t));
    }
    values += "currentTime|"          + timeStr + "|div\n";
    request->send(200, "text/plain", values);
}

void MODULE_CLASS_CLOCKMECH::handleSave(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            String name = request->argName(i);
            String val  = request->arg(i);

            if (name == "enabled")              { _config.enabled = (val == "true"); }
            else if (name == "timeSource")       { _config.timeSource = val; }
            else if (name == "triggerHour")      { _config.triggerHour = constrain(val.toInt(), 0, 23); }
            else if (name == "triggerMinute")    { _config.triggerMinute = constrain(val.toInt(), 0, 59); }
            else if (name == "stepsPerRevolution") { _config.stepsPerRevolution = (uint16_t)val.toInt(); }
            else if (name == "pollInterval")     { _config.pollInterval = (uint16_t)val.toInt(); if (_config.pollInterval < 1) _config.pollInterval = 1; }
            else if (name == "stepTime")         { _config.stepTime = (uint16_t)val.toInt(); if (_config.stepTime < 1) _config.stepTime = 1; }
            else if (name == "errorLimitSteps")  { _config.errorLimitSteps = (uint16_t)val.toInt(); }
            else if (name == "sensorLedEnabled") { _config.sensorLedEnabled = (val == "true"); }
        }
        saveConfig();
        request->send(200, "text/plain", "OK");
    }
}

void MODULE_CLASS_CLOCKMECH::handleReset(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    SetTask(MechSet1200_Setup);
    request->send(200, "text/plain", "OK");
}


// ============================================================
// Инициализация GPIO
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechInitPorts() {
    pinMode(CLOCKMECH_DIR,  OUTPUT);
    pinMode(CLOCKMECH_STEP, OUTPUT);
    pinMode(CLOCKMECH_EN,   OUTPUT);

    pinMode(CLOCKMECH_SENS_LED, OUTPUT);
    pinMode(CLOCKMECH_SENS_HOUR, INPUT);
    pinMode(CLOCKMECH_SENS_MIN, INPUT);

    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    digitalWrite(CLOCKMECH_STEP, HIGH);
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
}

// ============================================================
// Вспомогательные макросы сенсоров
// ============================================================
#define SENS_MIN_SET    (digitalRead(CLOCKMECH_SENS_MIN) == LOW)
#define SENS_HOUR_SET   (digitalRead(CLOCKMECH_SENS_HOUR) == LOW)
#define SENS_ANY_SET    (SENS_MIN_SET || SENS_HOUR_SET)
#define SENS_SET        (SENS_MIN_SET && SENS_HOUR_SET)

// ============================================================
// Генератор шага A4988 — эталонный паттерн из Clock_Mechanism.c
// MechMoveStepDown: STEP=HIGH, через 2ms MechMoveStepUp
// MechMoveStepUp: STEP=LOW, затем SetTask(GoToTaskAfter)
// ============================================================

void MODULE_CLASS_CLOCKMECH::MechMoveStepDown() {
    digitalWrite(CLOCKMECH_STEP, LOW);
    SetTimerTask(MechMoveStepUp, 2);
}
void MODULE_CLASS_CLOCKMECH::MechMoveStepUp() {
    digitalWrite(CLOCKMECH_STEP, HIGH);
    SetTimerTask(GoToTaskAfter, 2);
}

// ============================================================
// Терминальные команды
// ============================================================

void MODULE_CLASS_CLOCKMECH::cmdStep() {
    GoToTaskAfter = Idle_task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::cmdDir() {
    uint8_t dir = digitalRead(CLOCKMECH_DIR);
    if (dir == 0) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);}
    if (dir == 1) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);}
    Serial.printf("Direction: %s\r\n", dir == CLOCKMECH_ClockWise ? "CCW" : "CW");
}

void MODULE_CLASS_CLOCKMECH::cmdEn() {
    bool en = digitalRead(CLOCKMECH_EN);
    digitalWrite(CLOCKMECH_EN, en == LOW ? HIGH : LOW);
    Serial.printf("Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}

void MODULE_CLASS_CLOCKMECH::cmdSled() {
    bool led = digitalRead(CLOCKMECH_SENS_LED);
    digitalWrite(CLOCKMECH_SENS_LED, led == HIGH ? LOW : HIGH);
    Serial.printf("Sensor LED: %s\r\n", led == HIGH ? "OFF" : "ON");
}

void MODULE_CLASS_CLOCKMECH::cmdSens() {
    
    Serial.printf("SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(CLOCKMECH_SENS_HOUR),
        digitalRead(CLOCKMECH_SENS_MIN),
        digitalRead(CLOCKMECH_SENS_LED));
}

void MODULE_CLASS_CLOCKMECH::GetSens() {
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    ModClassClockMech._sensorLedStateHOUR = digitalRead(CLOCKMECH_SENS_HOUR);
    ModClassClockMech._sensorLedStateMIN = digitalRead(CLOCKMECH_SENS_MIN);
}
void MODULE_CLASS_CLOCKMECH::MechNCmdStep() {
    if (_nStepCount > 0) {
        _nStepCount--;
        GoToTaskAfter = MechNCmdStep; 
        SetTimerTask(MechMoveStepDown, 2);
    } else {
        GoToTaskAfter = Idle_task; 
    }
}

void MODULE_CLASS_CLOCKMECH::cmdN() {

    if (_nStepCount > 0) {
        Serial.println("Busy: previous clock-n still running");
        return;
    }
    char *arg = term.getNext();
    if (arg == NULL) {
        Serial.println("Usage: clock-n <N>");
        return;
    }
    _nStepCount = atoi(arg);
    if (_nStepCount == 0) return;
    digitalWrite(CLOCKMECH_EN, LOW);
    GoToTaskAfter = MechNCmdStep;
    MechMoveStepDown();
}

// ============================================================
// Сброс механизма в 12:00 
// ============================================================
void MODULE_CLASS_CLOCKMECH::cmdSet1200() { MechSet1200_Setup(); }

void MODULE_CLASS_CLOCKMECH::MechSet1200_Setup() {
    ModClassClockMech._status = STATUS_SET1200;
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);
    ModClassClockMech._mechControlSteps = 0;
    GoToTaskAfter = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}
void MODULE_CLASS_CLOCKMECH::MechSet1200_Task() {
    if (ModClassClockMech._status != STATUS_SET1200) {return;}
    
    // превышен лимит шагов
    if (ModClassClockMech._mechControlSteps > ModClassClockMech._config.errorLimitSteps * 12) {
        MechSet1200_endFail();
        return;
    }
    
    // Нашли положение 12:00
    if (SENS_SET) { 
        MechSet1200_endOk();
        return;
    }
    // Двигаемся дальше
    ModClassClockMech._mechControlSteps++;
    GoToTaskAfter = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::MechSet1200_endOk() {
    ModClassClockMech._timeMechMin = 0;
    ModClassClockMech._timeMechHour = 0;
    ModClassClockMech._mechControlSteps = 0;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    GoToTaskAfter = Idle_task;

    if (ModClassClockMech._config.enabled == true) {
        ModClassClockMech._status = STATUS_WORKING;
        SetTask(PollTimeTask);
    } else {
        ModClassClockMech._status = STATUS_IDLE;
    }
    DEBUGCLOCKMECH("MechSet1200_Task: done, position 12:00\r\n");
}

void MODULE_CLASS_CLOCKMECH::MechSet1200_endFail() {
    GoToTaskAfter = Idle_task;
    ModClassClockMech._status = ERROR_NO_MECH;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    DEBUGCLOCKMECH("MechSet1200_Task: ERROR_NO_MECH (%d)\r\n", ModClassClockMech._mechControlSteps);
    ModClassClockMech._mechControlSteps = 0;
}
// ============================================================
// Подсчёт шагов на оборот
// ============================================================

void MODULE_CLASS_CLOCKMECH::handleCount(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    SetTask(MechCountStepsSetup);
    request->send(200, "text/plain", "OK");
}
void MODULE_CLASS_CLOCKMECH::cmdCount() {   MechCountStepsSetup(); }

void MODULE_CLASS_CLOCKMECH::MechCountStepsSetup() {
    ModClassClockMech._status = STATUS_COUNTING;
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GetSens();

    ModClassClockMech._mechControlSteps = 0;
    GoToTaskAfter = MechCountStepsTask;
    SetTask(MechCountStepsTask);
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsTask() {
    if (ModClassClockMech._status != STATUS_COUNTING) {return;}
    if (ModClassClockMech._mechControlSteps >= ModClassClockMech._config.errorLimitSteps) {
        MechCountStepsFail();
        return;
    }
    if ( SENS_MIN_SET && ModClassClockMech._mechControlSteps > CLOCKMECH_MIN_STEPS_GAP) {
        MechCountStepsOk(); 
        return;
    }
    ModClassClockMech._mechControlSteps++;
    GoToTaskAfter = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsOk() {
    GetSens();
    cmdSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    
    ModClassClockMech._config.stepsPerRevolution = ModClassClockMech._mechControlSteps;
    if (ModClassClockMech._mechControlSteps != 0 ) { ModClassClockMech.saveConfig(); }
    
    ModClassClockMech._mechControlSteps = 0;
    DEBUGCLOCKMECH("MechCountSteps: %d steps\r\n", ModClassClockMech._config.stepsPerRevolution);

    if (ModClassClockMech._config.enabled == true) {
        ModClassClockMech._status = STATUS_WORKING;
        SetTask(PollTimeTask);
    } else {
        ModClassClockMech._status = STATUS_IDLE;
    }
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsFail() {
    GetSens();
    cmdSens();
    ModClassClockMech._mechControlSteps = 0;
    ModClassClockMech._status = ERROR_NO_MECH;

    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    DEBUGCLOCKMECH("MechCountSteps: ERROR_NO_MECH\r\n");
}


// ============================================================
// Периодический опрос времени
// ============================================================
void MODULE_CLASS_CLOCKMECH::PollTimeTask() {
    if (ModClassClockMech._status != STATUS_WORKING) { return; }

    time_t t = ModClassClockMech.getCurrentTime();
    if (t > 0) {
        ModClassClockMech._timeHourReal = hour(t);
        ModClassClockMech._timeMinReal  = minute(t);
        ModClassClockMech.CheckAndSync();
    }

    if (!ModClassClockMech._needSync && ModClassClockMech._status != STATUS_SET1200 && ModClassClockMech._status != STATUS_COUNTING) {
        SetTimerTask(PollTimeTask, ModClassClockMech._config.pollInterval * 1000UL);
    }
}

void MODULE_CLASS_CLOCKMECH::CheckAndSync() {
    if (_timeMechHour != _timeHourReal || abs((int)_timeMechMin - (int)_timeMinReal) > 1) {
        _needSync = true;
        SetTask(MechSetArrows);
    }
}

// ============================================================
// Постановка стрелок
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechSetArrows() {
    if (ModClassClockMech._timeMechHour != ModClassClockMech._timeHourReal) {
        if (ModClassClockMech._timeMechHour > ModClassClockMech._timeHourReal) { SetTask(MechSet1200_Setup); }
         else { SetTask(MechSetArrowHour); }
    } else { SetTask(MechSetArrowMin); }
}

// ============================================================
// Продвинуть часовую на +1 (12 оборотов минутной)
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechSetArrowHour() {
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);

    if (SENS_MIN_SET || ModClassClockMech._mechControlSteps <= 5) {
        if (ModClassClockMech._mechControlSteps < ModClassClockMech._config.errorLimitSteps) {
            ModClassClockMech._mechControlSteps++;
            GoToTaskAfter = MechSetArrowHour;
            SetTimerTask(MechMoveStepDown, 2);
        } else {
            ModClassClockMech._status = ERROR_NO_MECH;
            digitalWrite(CLOCKMECH_EN, HIGH);
            digitalWrite(CLOCKMECH_SENS_LED, LOW);
            ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
        }
    } else {
        ModClassClockMech._timeMechHour++;
        if (ModClassClockMech._timeMechHour >= 12) { ModClassClockMech._timeMechHour = 0; }
        ModClassClockMech._timeMechMin = 0;
        ModClassClockMech._mechControlSteps = 0;
        digitalWrite(CLOCKMECH_SENS_LED, LOW);
        ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
        SetTask(MechSetArrows);
    }
}

// ============================================================
// Довернуть минуты до целевого положения
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechSetArrowMin() {
    int stepsNeeded = (ModClassClockMech._timeMinReal * ModClassClockMech._config.stepsPerRevolution / 60)
                    - (ModClassClockMech._timeMechMin  * ModClassClockMech._config.stepsPerRevolution / 60);

    if (stepsNeeded > 0) {
        digitalWrite(CLOCKMECH_EN, LOW);
        digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
        ModClassClockMech._timeMechMin++;
        if (ModClassClockMech._timeMechMin >= 60) {
            ModClassClockMech._timeMechMin = 0;
            ModClassClockMech._timeMechHour++;
            if (ModClassClockMech._timeMechHour >= 12) { ModClassClockMech._timeMechHour = 0; }
        }
        GoToTaskAfter = MechSetArrowMin;
        SetTimerTask(MechMoveStepDown, 2);
    } else {
        ModClassClockMech._needSync = false;
        digitalWrite(CLOCKMECH_EN, HIGH);
        SetTimerTask(PollTimeTask, ModClassClockMech._config.pollInterval * 1000UL);
    }
}

// ============================================================
// Конфиг
// ============================================================
void MODULE_CLASS_CLOCKMECH::defaultConfig() {
    _config.enabled            = false;
    _config.timeSource         = "ds3231";
    _config.triggerHour        = 12;
    _config.triggerMinute      = 0;
    _config.stepsPerRevolution = 400;
    _config.pollInterval       = 5;
    _config.stepTime           = 5;
    _config.errorLimitSteps    = 1500;
    _config.sensorLedEnabled   = true;
}

bool MODULE_CLASS_CLOCKMECH::loadConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc) == false) { return false; }

    _config.enabled            = doc["enabled"].as<bool>();
    _config.timeSource         = doc["timeSource"].as<String>();
    _config.triggerHour        = doc["triggerHour"].as<uint16_t>();
    _config.triggerMinute      = doc["triggerMinute"].as<uint16_t>();
    _config.stepsPerRevolution = doc["stepsPerRevolution"].as<uint16_t>();
    _config.pollInterval       = doc["pollInterval"].as<uint16_t>();
    _config.stepTime           = doc["stepTime"].as<uint16_t>();
    _config.errorLimitSteps    = doc["errorLimitSteps"].as<uint16_t>();
    _config.sensorLedEnabled   = doc["sensorLedEnabled"].as<bool>();

    if (_config.timeSource != "ds3231" && _config.timeSource != "ntp") { _config.timeSource = "ds3231"; }
    if (_config.pollInterval < 1)  { _config.pollInterval = 5; }
    if (_config.stepTime < 1)      { _config.stepTime = 5; }
    if (_config.stepsPerRevolution < 1) { _config.stepsPerRevolution = 400; }

    return true;
}

bool MODULE_CLASS_CLOCKMECH::saveConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc);
    doc["enabled"]            = _config.enabled;
    doc["timeSource"]         = _config.timeSource;
    doc["triggerHour"]        = _config.triggerHour;
    doc["triggerMinute"]      = _config.triggerMinute;
    doc["stepsPerRevolution"] = _config.stepsPerRevolution;
    doc["pollInterval"]       = _config.pollInterval;
    doc["stepTime"]           = _config.stepTime;
    doc["errorLimitSteps"]    = _config.errorLimitSteps;
    doc["sensorLedEnabled"]   = _config.sensorLedEnabled;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_CLOCKMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String MODULE_CLASS_CLOCKMECH::getVersionStr() {
    return String(DEVICE_CLOCK_MECH_VERSION);
}

String MODULE_CLASS_CLOCKMECH::getGeneratedTime() {
    return String(DEVICE_CLOCK_MECH_GENERATED_TIME);
}

String MODULE_CLASS_CLOCKMECH::getCommitDateStr() {
    return String(DEVICE_CLOCK_MECH_COMMIT_DATE_STR);
}

void MODULE_CLASS_CLOCKMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "clockmechversion|" + getVersionStr()    + "|div\n";
    values += "clockmechgentime|" + getGeneratedTime() + "|div\n";
    values += "clockmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
