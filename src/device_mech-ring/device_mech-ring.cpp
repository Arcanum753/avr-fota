#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_mech-ring.h"
#include "common.h"
#include "device_mech-ring_version.h"
#include "eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#if defined(MODULE_DS3231)
#include "module_ds3231/module_ds3231.h"
#endif

#include <TimeLib.h>

extern DPDR GoToTaskAfterStep;

static uint16_t _nStepCount = 0;



MODULE_CLASS_RINGMECH ModClassRingMech(false);
MODULE_CLASS_RINGMECH::MODULE_CLASS_RINGMECH(bool _in) { dumb = _in; }
void MODULE_CLASS_RINGMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }

// ============================================================
// Время из источника
// ============================================================
time_t MODULE_CLASS_RINGMECH::getCurrentTime() {
#if defined(MODULE_DS3231)
    if (_config.timeSource == "ds3231") {
        time_t t = ModClassDs3231.getTime();
        if (t > 0) { return t; }
        DEBUGRINGMECH("DS3231 error, fallback to NTP\r\n");
    }
#endif
    return now();
}

// ============================================================
// begin()
// ============================================================
void MODULE_CLASS_RINGMECH::begin() {
    //общий сброс
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    GoToTaskAfterStep = Idle_task;
    _mechControlSteps = 0;
    _timeMechMin = 0;
    _timeMechHour = 0;
    _timeMinReal = 0;
    _timeHourReal = 0;
_ringStatus = RING_STATUS_IDLE;
    _minPrev = 0;
    _sensorLedState = digitalRead(RINGMECH_SENS_LED);
    
    defaultConfig(); //конфиги
    if (loadConfig() == false) { saveConfig(); }

    TerminalRegisterModule(ringMechTerminalRegister); // терминал
    MechInitGPIOs(); // инит GPIO

    if (_config.enable_status == RING_MODE_WORK) {SetTask(MechSet1200_Setup); } // если норм режим то работаем
    SetTask(PollTimeTask);
}

// ============================================================
// webInit()
// ============================================================
void MODULE_CLASS_RINGMECH::webInit() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/ring-mech/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    ESPHTTPServer.on("/ring-mech/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/ring-mech/reset", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleReset(request);
    });

    ESPHTTPServer.on("/ring-mech/count", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleCount(request);
    });

    
    ESPHTTPServer.on("/ring-mech/step",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStepWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/dir",    HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdDirWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/en",     HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdEnWeb(request);   else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/sled",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSledWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/sens",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSensWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/n",      HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdNWeb(request);    else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/reset",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdResetWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/count",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdCountWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/status", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStatusWeb(request); else request->requestAuthentication(); });

    ESPHTTPServer.on("/ring-mech/ver", HTTP_GET, [this](AsyncWebServerRequest *request) { this->html_ver_get(request);});
}


void ringMechTerminalRegister() {
    term.addCommand("c-step",   MODULE_CLASS_RINGMECH::cmdStep);
    term.addCommand("c-dir",    MODULE_CLASS_RINGMECH::cmdDir);
    term.addCommand("c-enc",     MODULE_CLASS_RINGMECH::cmdEn);
    term.addCommand("c-sled",   MODULE_CLASS_RINGMECH::cmdSled);
    term.addCommand("c-sens",   MODULE_CLASS_RINGMECH::cmdSens);
    term.addCommand("c-n",      MODULE_CLASS_RINGMECH::cmdN);
    term.addCommand("c-12",     MODULE_CLASS_RINGMECH::cmdSet1200);
    term.addCommand("c-cnt",    MODULE_CLASS_RINGMECH::cmdCount);
    term.addCommand("c-mode",   MODULE_CLASS_RINGMECH::cmdMode);
    term.addCommand("c-pol",    MODULE_CLASS_RINGMECH::cmdPoll);
    term.addCommand("c-stat",   MODULE_CLASS_RINGMECH::cmdStatus);
    term.addCommand("c-set",   MODULE_CLASS_RINGMECH::cmdSetArrows);
    term.addCommand("c-save",   MODULE_CLASS_RINGMECH::cmdSave);
}

// ============================================================
// Веб-обработчики
// ============================================================
void MODULE_CLASS_RINGMECH::handleInfo(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    doc["enable_status"]        = _config.enable_status;
    doc["timeSource"]           = _config.timeSource;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["sensorLedEnabled"]     = _config.sensorLedEnabled;
    doc["status"]               = _ringStatus;
    doc["mechMin"]              = _timeMechMin;
    doc["mechHour"]             = _timeMechHour;
    doc["mechControlSteps"]     = _mechControlSteps;

    time_t t = getCurrentTime();
    if (t > 0) {
        char buf[9];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour(t), minute(t), second(t));
        doc["currentTime"] = buf;
    } else {
        doc["currentTime"] = "N/A";
    }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void MODULE_CLASS_RINGMECH::handleSave(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            String name = request->argName(i);
            String val  = request->arg(i);

            if (name == "enable_status")            { _config.enable_status = (uint8_t)val.toInt(); }
            else if (name == "timeSource")          { _config.timeSource = val; }
            else if (name == "stepsPerRevolution")  { _config.stepsPerRevolution = (uint16_t)val.toInt(); }
            else if (name == "pollInterval")        { _config.pollInterval = (uint16_t)val.toInt(); if (_config.pollInterval < 1) _config.pollInterval = 1; }
            else if (name == "errorLimitSteps")     { _config.errorLimitSteps = (uint16_t)val.toInt(); }
            else if (name == "sensorLedEnabled")    { _config.sensorLedEnabled = (val == "true"); }
        }
        saveConfig();
        request->send(200, "text/plain", "OK");
    }
}

// ============================================================
// Эндпоинты ручного управления (GET, JSON)
// ============================================================
void MODULE_CLASS_RINGMECH::cmdStepWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdStep();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdDirWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdDir();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdSledWeb(AsyncWebServerRequest *request) {
    cmdSled();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS_HOUR"] = digitalRead(RINGMECH_SENS_HOUR);
    doc["SENS_MIN"]  = digitalRead(RINGMECH_SENS_MIN);
    doc["SENS_LED"]  = digitalRead(RINGMECH_SENS_LED);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}
void MODULE_CLASS_RINGMECH::cmdNWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    _nStepCount = (uint16_t)request->arg("count").toInt();
    if (_nStepCount == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    digitalWrite(RINGMECH_EN, LOW);
    GoToTaskAfterStep = MechNCmdStep;
    MechMoveStepDown();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_ringStatus"]            = ModClassRingMech._ringStatus;
    doc["enable_status"]           = ModClassRingMech._config.enable_status;
    doc["timeSource"]              = ModClassRingMech._config.timeSource;
    doc["stepsPerRevolution"]      = ModClassRingMech._config.stepsPerRevolution;
    doc["pollInterval"]            = ModClassRingMech._config.pollInterval;
    doc["errorLimitSteps"]         = ModClassRingMech._config.errorLimitSteps;
    doc["sensorLedEnabled"]        = ModClassRingMech._config.sensorLedEnabled;
    doc["_mechControlSteps"]       = ModClassRingMech._mechControlSteps;
    doc["_timeMechMin"]            = ModClassRingMech._timeMechMin;
    doc["_timeMechHour"]           = ModClassRingMech._timeMechHour;
    doc["_timeMinReal"]            = ModClassRingMech._timeMinReal;
    doc["_timeHourReal"]           = ModClassRingMech._timeHourReal;
    doc["_minPrev"]                = ModClassRingMech._minPrev;
    doc["_sensorLedState"]         = ModClassRingMech._sensorLedState;
    doc["_sensorLedStateHOUR"]     = ModClassRingMech._sensorLedStateHOUR;
    doc["_sensorLedStateMIN"]      = ModClassRingMech._sensorLedStateMIN;
    doc["mchCS"]                   = ModClassRingMech.mchCS;
    doc["gpio_SENS_HOUR"]         = digitalRead(RINGMECH_SENS_HOUR);
    doc["gpio_SENS_MIN"]          = digitalRead(RINGMECH_SENS_MIN);
    doc["gpio_SENS_LED"]          = digitalRead(RINGMECH_SENS_LED);
    doc["gpio_DIR"]               = digitalRead(RINGMECH_DIR);
    doc["gpio_STEP"]              = digitalRead(RINGMECH_STEP);
    doc["gpio_EN"]                = digitalRead(RINGMECH_EN);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}


// ============================================================
// Инициализация GPIO
// ============================================================
void MODULE_CLASS_RINGMECH::MechInitGPIOs() {
    pinMode(RINGMECH_DIR,  OUTPUT);
    pinMode(RINGMECH_STEP, OUTPUT);
    pinMode(RINGMECH_EN,   OUTPUT);

    pinMode(RINGMECH_SENS_LED, OUTPUT);
    pinMode(RINGMECH_SENS_HOUR, INPUT);
    pinMode(RINGMECH_SENS_MIN, INPUT);

    digitalWrite(RINGMECH_DIR, RINGMECH_ClockWise);
    digitalWrite(RINGMECH_STEP, LOW);
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
}

// ============================================================
// Вспомогательные макросы сенсоров
// ============================================================
#define SENS_MIN_SET    (digitalRead(RINGMECH_SENS_MIN) == LOW)
#define SENS_HOUR_SET   (digitalRead(RINGMECH_SENS_HOUR) == LOW)
#define SENS_ANY_SET    (SENS_MIN_SET || SENS_HOUR_SET)
#define SENS_SET        (SENS_MIN_SET && SENS_HOUR_SET)




// ============================================================
// Генератор шага A4988 — эталонный паттерн из Clock_Mechanism.c
// MechMoveStepDown: STEP=HIGH, через 2ms MechMoveStepUp
// MechMoveStepUp: STEP=LOW, затем SetTask(GoToTaskAfterStep)
// ============================================================

void MODULE_CLASS_RINGMECH::MechMoveStepDown() {
    digitalWrite(RINGMECH_STEP, HIGH);
    SetTimerTask(MechMoveStepUp, 2);
}
void MODULE_CLASS_RINGMECH::MechMoveStepUp() {
    digitalWrite(RINGMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStep, 2);
}

// ============================================================
// Терминальные команды
// ============================================================

void MODULE_CLASS_RINGMECH::cmdStep() {
    GoToTaskAfterStep = Idle_task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::cmdDir() {
    uint8_t dir = digitalRead(RINGMECH_DIR);
    if (dir == 1) { digitalWrite(RINGMECH_DIR, RINGMECH_ClockWise);}
    if (dir == 0) { digitalWrite(RINGMECH_DIR, RINGMECH_CounterClockWise);}
    Serial.printf("Direction: %s\r\n", dir == RINGMECH_ClockWise ? "CCW" : "CW");
}

void MODULE_CLASS_RINGMECH::cmdEn() {
    bool en = digitalRead(RINGMECH_EN);
    digitalWrite(RINGMECH_EN, en == LOW ? HIGH : LOW);
    Serial.printf("Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}


void MODULE_CLASS_RINGMECH::cmdSled() {
    bool led = digitalRead(RINGMECH_SENS_LED);
    digitalWrite(RINGMECH_SENS_LED, led == HIGH ? LOW : HIGH);
    Serial.printf("Sensor LED: %s\r\n", led == HIGH ? "OFF" : "ON");
}

void MODULE_CLASS_RINGMECH::cmdSens() {
    
    Serial.printf("SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(RINGMECH_SENS_HOUR),
        digitalRead(RINGMECH_SENS_MIN),
        digitalRead(RINGMECH_SENS_LED));
}

void MODULE_CLASS_RINGMECH::GetSens() {
    ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
    ModClassRingMech._sensorLedStateHOUR = digitalRead(RINGMECH_SENS_HOUR);
    ModClassRingMech._sensorLedStateMIN = digitalRead(RINGMECH_SENS_MIN);
}
void MODULE_CLASS_RINGMECH::MechNCmdStep() {
    if (_nStepCount > 0) {
        _nStepCount--;
        GoToTaskAfterStep = MechNCmdStep; 
        SetTimerTask(MechMoveStepDown, 2);
    } else { GoToTaskAfterStep = Idle_task;  }
}

void MODULE_CLASS_RINGMECH::cmdN() {
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
    digitalWrite(RINGMECH_EN, LOW);
    GoToTaskAfterStep = MechNCmdStep;
    SetTask(MechMoveStepDown);
}
void MODULE_CLASS_RINGMECH::cmdSet1200()   { SetTask(MechSet1200_Setup); }
void MODULE_CLASS_RINGMECH::cmdCount()     { SetTask(MechCountStepsSetup); }
void MODULE_CLASS_RINGMECH::cmdPoll()      { SetTask(PollTimeTask); }

void MODULE_CLASS_RINGMECH::cmdSave() {    ModClassRingMech.saveConfig(); }
void MODULE_CLASS_RINGMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) {
        ModClassRingMech._config.enable_status = !ModClassRingMech._config.enable_status;
    } else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { ModClassRingMech._config.enable_status = RING_MODE_DEBUG; }
        else if (s == "work")                 { ModClassRingMech._config.enable_status = RING_MODE_WORK; }
        else { Serial.println("Usage: c-mode [dbg|work]"); return; }
    }
    Serial.printf("Mode: %s\r\n", ModClassRingMech._config.enable_status == RING_MODE_WORK ? "WORK" : "DEBUG");
}

void MODULE_CLASS_RINGMECH::cmdSetArrows() {
    char *arg1 = term.getNext();
    if (arg1 == NULL) {
        Serial.println("Usage: c-set <hour> [min]");
        return;
    }
    uint8_t h = (uint8_t)atoi(arg1);
    if (h > 24) {
        Serial.println("Error: hour must be 0-24");
        return;
    }
    char *arg2 = term.getNext();
    uint8_t m = 0;
    if (arg2 != NULL) {
        m = (uint8_t)atoi(arg2);
        if (m > 60) {
            Serial.println("Error: min must be 0-60");
            return;
        }
    }
    ModClassRingMech.CheckTime(h, m);
    Serial.printf("Set Arrows to: %02d:%02d\r\n",  ModClassRingMech._timeHourReal, ModClassRingMech._timeMinReal);
    SetTask(MechSetArrows);
}


void MODULE_CLASS_RINGMECH::cmdStatus() {

    time_t t = ModClassRingMech.getCurrentTime();
  
    Serial.printf("===== RingMech Status =====\r\n");
    Serial.printf("_ringStatus:          %d\r\n", ModClassRingMech._ringStatus);
    Serial.printf("_config.enable_status: %d\r\n", ModClassRingMech._config.enable_status);
    Serial.printf("_config.timeSource:    %s\r\n", ModClassRingMech._config.timeSource.c_str());
    Serial.printf("_config.stepsPerRevolution: %d\r\n", ModClassRingMech._config.stepsPerRevolution);
    Serial.printf("_config.pollInterval: %d\r\n", ModClassRingMech._config.pollInterval);
    Serial.printf("_config.errorLimitSteps: %d\r\n", ModClassRingMech._config.errorLimitSteps);
    Serial.printf("_config.sensorLedEnabled: %d\r\n", ModClassRingMech._config.sensorLedEnabled);
    Serial.printf("_mechControlSteps: %d\r\n", ModClassRingMech._mechControlSteps);
    Serial.printf("_timeMechHour: %d _timeMechMin: %d\r\n", ModClassRingMech._timeMechHour, ModClassRingMech._timeMechMin);
    Serial.printf("_timeHourReal: %d _timeMinReal: %d\r\n", ModClassRingMech._timeHourReal,  ModClassRingMech._timeMinReal);
    Serial.printf("_minPrev:          %d\r\n", ModClassRingMech._minPrev);
    Serial.printf("_sensorLedState:   %d\r\n", ModClassRingMech._sensorLedState);
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        Serial.printf("time: %s\r\n", buf);
    }
    Serial.printf("mchCS:             %d\r\n", ModClassRingMech.mchCS);
    Serial.printf("SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(RINGMECH_SENS_HOUR),
        digitalRead(RINGMECH_SENS_MIN),
        digitalRead(RINGMECH_SENS_LED));
    Serial.printf("DIR=%d STEP=%d EN=%d\r\n",
        digitalRead(RINGMECH_DIR),
        digitalRead(RINGMECH_STEP),
        digitalRead(RINGMECH_EN));
    Serial.printf("=============================\r\n");
}

// ============================================================
// Сброс механизма в 12:00 
// ============================================================

void MODULE_CLASS_RINGMECH::handleReset(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    if (_config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "text/plain", "OK");
}



void MODULE_CLASS_RINGMECH::MechSet1200_Setup() {
    ModClassRingMech._ringStatus = RING_STATUS_SET1200;
    ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    digitalWrite(RINGMECH_EN, LOW);
    digitalWrite(RINGMECH_DIR, RINGMECH_CounterClockWise);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}
void MODULE_CLASS_RINGMECH::MechSet1200_Task() {
    if (ModClassRingMech._ringStatus != RING_STATUS_SET1200) {return;}
    
    // превышен лимит шагов
    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps * 12) {
        MechSet1200_endFail();
        return;
    }
    
    // Нашли положение 12:00
    if (SENS_SET) { 
        MechSet1200_endOk();
        return;
    }
    // Двигаемся дальше
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::MechSet1200_endOk() {
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    GoToTaskAfterStep = Idle_task;
    ModClassRingMech._timeMechMin = 0;
    ModClassRingMech._timeMechHour = 0;
    ModClassRingMech._mechControlSteps = 0;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);

    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) {
        if (ModClassRingMech._config.stepsPerRevolution == 0)  {  SetTask(MechCountStepsSetup); }
        else { SetTask(PollTimeTask); }
    }
    DEBUGRINGMECH("MechSet1200_Task: done, position 12:00\r\n");
}

void MODULE_CLASS_RINGMECH::MechSet1200_endFail() {
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
    DEBUGRINGMECH("MechSet1200_Task: RING_ERROR_NO_MECH (%d)\r\n", ModClassRingMech._mechControlSteps);
    ModClassRingMech._mechControlSteps = 0;
}
// ============================================================
// Подсчёт шагов на оборот
// ============================================================

void MODULE_CLASS_RINGMECH::handleCount(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    if (_config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "text/plain", "OK");
}

void MODULE_CLASS_RINGMECH::MechCountStepsSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_COUNTING;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    digitalWrite(RINGMECH_EN, LOW);
    digitalWrite(RINGMECH_DIR, RINGMECH_ClockWise);
    GetSens();

    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechCountStepsTask);
}

void MODULE_CLASS_RINGMECH::MechCountStepsTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_COUNTING) {return;}
    if (ModClassRingMech._mechControlSteps >= ModClassRingMech._config.errorLimitSteps) {
        MechCountStepsFail();
        return;
    }
    if ( SENS_MIN_SET && ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP) {
        MechCountStepsOk(); 
        return;
    }
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::MechCountStepsOk() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    GetSens(); cmdSens();
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    
    ModClassRingMech._config.stepsPerRevolution = ModClassRingMech._mechControlSteps;
    if (ModClassRingMech._mechControlSteps != 0 ) { ModClassRingMech.saveConfig(); }
    GoToTaskAfterStep = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._timeMechMin = 0;
    DEBUGRINGMECH("MechCountSteps: %d steps\r\n", ModClassRingMech._config.stepsPerRevolution);

    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { SetTask(PollTimeTask); } 
}

void MODULE_CLASS_RINGMECH::MechCountStepsFail() {
    DEBUGRINGMECH("MechCountSteps: RING_ERROR_NO_MECH\r\n");
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    ModClassRingMech._mechControlSteps = 0;

    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
}


// ============================================================
// Периодический опрос времени
// ============================================================

void MODULE_CLASS_RINGMECH::PollTimeTask() {
    SetTimerTask(PollTimeTask, ModClassRingMech._config.pollInterval * 1000UL);
    if (ModClassRingMech._config.enable_status != RING_MODE_WORK) { return; }
    if (ModClassRingMech._ringStatus != RING_STATUS_IDLE) { return; }
    time_t t = ModClassRingMech.getCurrentTime();
    if (t > 0) {
        ModClassRingMech.CheckTime((uint8_t)hour(t), (uint8_t)minute(t));
        SetTask(MechSetArrows);
    }
}

void MODULE_CLASS_RINGMECH::CheckTime (uint8_t _inH, uint8_t _inM)	{
	if (_inH >= HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	if (_inM >= MININHOUR)      { _inM = MINMAX; }
	_timeHourReal = _inH;
	_timeMinReal = _inM;
}

// ============================================================
// Постановка стрелок
// ============================================================
void MODULE_CLASS_RINGMECH::MechSetArrows() {
    // if ( ModClassRingMech._minPrev == MINMAX ){ ModClassRingMech.mchCS = 0; }
	// else { ModClassRingMech.mchCS = HOURCONTROLDEF; }
    if (ModClassRingMech._ringStatus != RING_STATUS_IDLE) { return; }

    if (ModClassRingMech._timeMechHour < ModClassRingMech._timeHourReal) {
        SetTask(MechSetArrowHourSetup);
        return;
    }
    if (ModClassRingMech._timeMechHour > ModClassRingMech._timeHourReal) {
        SetTask(MechSet1200_Setup);
        return;
    }
    if (ModClassRingMech._timeMechHour == ModClassRingMech._timeHourReal) {
        if (ModClassRingMech._minPrev != ModClassRingMech._timeMinReal) {
            ModClassRingMech._minPrev = ModClassRingMech._timeMinReal;
            SetTask(MechSetArrowMinSetup);
        }
    }
}

// ============================================================
// Продвинуть часовую на +1 (12 оборотов минутной)
// ============================================================

void MODULE_CLASS_RINGMECH::MechSetArrowHourSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_SETHOUR;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    digitalWrite(RINGMECH_EN, LOW);
    digitalWrite(RINGMECH_DIR, RINGMECH_ClockWise);
    GetSens();
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechSetArrowHourTask);
}

void MODULE_CLASS_RINGMECH::MechSetArrowHourTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_SETHOUR) {return;}
    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps) {
        MechSetArrowHourEndFail(); return;
    }
    if (SENS_MIN_SET && (ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP)    ) {
        MechSetArrowHourEndOk(); return;
    }
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechMoveStepDown);
    
}

void MODULE_CLASS_RINGMECH::MechSetArrowHourEndOk() {
    GoToTaskAfterStep = Idle_task;
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    GetSens();
    if (ModClassRingMech._timeMechHour == ModClassRingMech._timeHourReal) { DEBUGRINGMECH("tH_M == tH_R \r\n"); }
    ModClassRingMech._timeMechMin = 0;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._timeMechHour++;
    if (ModClassRingMech._timeMechHour > 12) { ModClassRingMech._timeMechHour -= 12; }
    DEBUGRINGMECH("HOUR Arrow: %02d\r\n",  ModClassRingMech._timeMechHour);
    SetTask(MechSetArrows); // set all arows
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);

}

void MODULE_CLASS_RINGMECH::MechSetArrowHourEndFail() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GetSens();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);

}

// ============================================================
// Довернуть минуты до целевого положения
// ============================================================
// void MODULE_CLASS_RINGMECH::MechSetArrowMinback() {
//     digitalWrite(RINGMECH_SENS_LED, HIGH);
//     digitalWrite(RINGMECH_EN, LOW);
//     digitalWrite(RINGMECH_DIR, RINGMECH_CounterClockWise);
//     ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);

//     if (SENS_MIN_SET) {
//         if (ModClassRingMech._mechControlSteps < ModClassRingMech._config.errorLimitSteps) {
//             ModClassRingMech._mechControlSteps++;
//             GoToTaskAfterStep = MechSetArrowMinback;
//             SetTask(MechMoveStepDown);
//         } else {
//             ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
//             digitalWrite(RINGMECH_EN, HIGH);
//             digitalWrite(RINGMECH_SENS_LED, LOW);
//             ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
//         }
//     } else {
//         ModClassRingMech._timeMechMin = 0;
//         ModClassRingMech._mechControlSteps = 0;
//         digitalWrite(RINGMECH_SENS_LED, LOW);
//         ModClassRingMech._sensorLedState = digitalRead(RINGMECH_SENS_LED);
//         SetTask(MechSetArrowMin);
//     }
// }

void MODULE_CLASS_RINGMECH::MechSetArrowMinSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_SETMIN;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    digitalWrite(RINGMECH_EN, LOW);
    digitalWrite(RINGMECH_DIR, RINGMECH_ClockWise);
    GetSens();
    GoToTaskAfterStep = MechSetArrowMinTask;
    SetTask(MechSetArrowMinTask);
}

void MODULE_CLASS_RINGMECH::MechSetArrowMinTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_SETMIN) {return;}
    if (ModClassRingMech._mechControlSteps >= ModClassRingMech._config.errorLimitSteps) {
        MechSetArrowMinFail(); return;
    }

    uint32_t MinPosSteps = MININHOUR * ModClassRingMech._mechControlSteps;
    uint32_t MinTimeSteps = ModClassRingMech._timeMinReal * ModClassRingMech._config.stepsPerRevolution;

    if ( MinPosSteps < MinTimeSteps ) {
        ModClassRingMech._mechControlSteps++;
        GoToTaskAfterStep = MechSetArrowMinTask;
        SetTask(MechMoveStepDown);
    }     
    if ( MinPosSteps >= MinTimeSteps ) {    SetTask(MechSetArrowMinOk); }
}

void MODULE_CLASS_RINGMECH::MechSetArrowMinOk() {
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    GoToTaskAfterStep = Idle_task;
    GetSens();
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    SetTask(MechSetArrows); // set all arows
    DEBUGRINGMECH("MIN MinPosSteps: %02d, MinTimeSteps: %d steps\r\n", MININHOUR * ModClassRingMech._mechControlSteps, ModClassRingMech._timeMinReal * ModClassRingMech._config.stepsPerRevolution);
}

void MODULE_CLASS_RINGMECH::MechSetArrowMinFail() {
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    DEBUGRINGMECH("MechSetArrowMinFail\r\n");
}


// ============================================================
// Конфиг
// ============================================================
void MODULE_CLASS_RINGMECH::defaultConfig() {
    _config.enable_status      = RING_MODE_DEBUG;
    _config.timeSource         = "ds3231";
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.sensorLedEnabled   = true;
}

bool MODULE_CLASS_RINGMECH::loadConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc) == false) { return false; }

    _config.enable_status       = doc["enable_status"].as<uint8_t>();
    _config.timeSource          = doc["timeSource"].as<String>();
    _config.stepsPerRevolution  = doc["stepsPerRevolution"].as<uint16_t>();
    _config.pollInterval        = doc["pollInterval"].as<uint16_t>();
    _config.errorLimitSteps     = doc["errorLimitSteps"].as<uint16_t>();
    _config.sensorLedEnabled    = doc["sensorLedEnabled"].as<bool>();

    if (_config.timeSource != "ds3231" && _config.timeSource != "ntp") { _config.timeSource = "ds3231"; }
    if (_config.pollInterval < 1)   { _config.pollInterval = 5; }
    if (_config.stepsPerRevolution < 1) { _config.stepsPerRevolution = 400; }

    return true;
}

bool MODULE_CLASS_RINGMECH::saveConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc);
    doc["enable_status"]        = _config.enable_status;
    doc["timeSource"]           = _config.timeSource;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["sensorLedEnabled"]     = _config.sensorLedEnabled;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_RINGMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String MODULE_CLASS_RINGMECH::getVersionStr() { return String(DEVICE_MECH_RING_VERSION);  }
String MODULE_CLASS_RINGMECH::getGeneratedTime() { return String(DEVICE_MECH_RING_GENERATED_TIME);    }
String MODULE_CLASS_RINGMECH::getCommitDateStr() { return String(DEVICE_MECH_RING_COMMIT_DATE_STR);   }

void MODULE_CLASS_RINGMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ringmechversion|" + getVersionStr()    + "|div\n";
    values += "ringmechgentime|" + getGeneratedTime() + "|div\n";
    values += "ringmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
