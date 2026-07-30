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

DPDR GoToTaskAfterStep = Idle_task;

static uint16_t _nStepCount = 0;



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
    //общий сброс
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    GoToTaskAfterStep = Idle_task;
    _mechControlSteps = 0;
    _timeMechMin = 0;
    _timeMechHour = 0;
    _timeMinReal = 0;
    _timeHourReal = 0;
    _Mech_Status = STATUS_IDLE;
    _minPrev = 0;
    _sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    
    defaultConfig(); //конфиги
    if (loadConfig() == false) { saveConfig(); }

    TerminalRegisterModule(clockMechTerminalRegister); // терминал
    MechInitGPIOs(); // инит GPIO

    if (_config.enable_status == MODE_WORK) {SetTask(MechSet1200_Setup); } // если норм режим то работаем
    SetTask(PollTimeTask);
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

    
    ESPHTTPServer.on("/clock-mech/step",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStepWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/dir",    HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdDirWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/en",     HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdEnWeb(request);   else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/sled",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSledWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/sens",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSensWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/n",      HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdNWeb(request);    else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/reset",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdResetWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/count",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdCountWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/status", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStatusWeb(request); else request->requestAuthentication(); });

    ESPHTTPServer.on("/clock-mech/ver", HTTP_GET, [this](AsyncWebServerRequest *request) { this->html_ver_get(request);});
}


void clockMechTerminalRegister() {
    term.addCommand("c-step",   MODULE_CLASS_CLOCKMECH::cmdStep);
    term.addCommand("c-dir",    MODULE_CLASS_CLOCKMECH::cmdDir);
    term.addCommand("c-enc",     MODULE_CLASS_CLOCKMECH::cmdEn);
    term.addCommand("c-sled",   MODULE_CLASS_CLOCKMECH::cmdSled);
    term.addCommand("c-sens",   MODULE_CLASS_CLOCKMECH::cmdSens);
    term.addCommand("c-n",      MODULE_CLASS_CLOCKMECH::cmdN);
    term.addCommand("c-12",     MODULE_CLASS_CLOCKMECH::cmdSet1200);
    term.addCommand("c-cnt",    MODULE_CLASS_CLOCKMECH::cmdCount);
    term.addCommand("c-mode",   MODULE_CLASS_CLOCKMECH::cmdMode);
    term.addCommand("c-pol",    MODULE_CLASS_CLOCKMECH::cmdPoll);
    term.addCommand("c-stat",   MODULE_CLASS_CLOCKMECH::cmdStatus);
    term.addCommand("c-set",   MODULE_CLASS_CLOCKMECH::cmdSetArrows);
    term.addCommand("c-save",   MODULE_CLASS_CLOCKMECH::cmdSave);
}

// ============================================================
// Веб-обработчики
// ============================================================
void MODULE_CLASS_CLOCKMECH::handleInfo(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    doc["enable_status"]        = _config.enable_status;
    doc["timeSource"]           = _config.timeSource;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["sensorLedEnabled"]     = _config.sensorLedEnabled;
    doc["status"]               = _Mech_Status;
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

void MODULE_CLASS_CLOCKMECH::handleSave(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

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
void MODULE_CLASS_CLOCKMECH::cmdStepWeb(AsyncWebServerRequest *request) {
    if (ModClassClockMech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdStep();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdDirWeb(AsyncWebServerRequest *request) {
    if (ModClassClockMech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdDir();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdSledWeb(AsyncWebServerRequest *request) {
    cmdSled();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS_HOUR"] = digitalRead(CLOCKMECH_SENS_HOUR);
    doc["SENS_MIN"]  = digitalRead(CLOCKMECH_SENS_MIN);
    doc["SENS_LED"]  = digitalRead(CLOCKMECH_SENS_LED);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}
void MODULE_CLASS_CLOCKMECH::cmdNWeb(AsyncWebServerRequest *request) {
    if (ModClassClockMech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    _nStepCount = (uint16_t)request->arg("count").toInt();
    if (_nStepCount == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    digitalWrite(CLOCKMECH_EN, LOW);
    GoToTaskAfterStep = MechNCmdStep;
    MechMoveStepDown();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    if (ModClassClockMech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (ModClassClockMech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_CLOCKMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_Mech_Status"]            = ModClassClockMech._Mech_Status;
    doc["enable_status"]           = ModClassClockMech._config.enable_status;
    doc["timeSource"]              = ModClassClockMech._config.timeSource;
    doc["stepsPerRevolution"]      = ModClassClockMech._config.stepsPerRevolution;
    doc["pollInterval"]            = ModClassClockMech._config.pollInterval;
    doc["errorLimitSteps"]         = ModClassClockMech._config.errorLimitSteps;
    doc["sensorLedEnabled"]        = ModClassClockMech._config.sensorLedEnabled;
    doc["_mechControlSteps"]       = ModClassClockMech._mechControlSteps;
    doc["_timeMechMin"]            = ModClassClockMech._timeMechMin;
    doc["_timeMechHour"]           = ModClassClockMech._timeMechHour;
    doc["_timeMinReal"]            = ModClassClockMech._timeMinReal;
    doc["_timeHourReal"]           = ModClassClockMech._timeHourReal;
    doc["_minPrev"]                = ModClassClockMech._minPrev;
    doc["_sensorLedState"]         = ModClassClockMech._sensorLedState;
    doc["_sensorLedStateHOUR"]     = ModClassClockMech._sensorLedStateHOUR;
    doc["_sensorLedStateMIN"]      = ModClassClockMech._sensorLedStateMIN;
    doc["mchCS"]                   = ModClassClockMech.mchCS;
    doc["gpio_SENS_HOUR"]         = digitalRead(CLOCKMECH_SENS_HOUR);
    doc["gpio_SENS_MIN"]          = digitalRead(CLOCKMECH_SENS_MIN);
    doc["gpio_SENS_LED"]          = digitalRead(CLOCKMECH_SENS_LED);
    doc["gpio_DIR"]               = digitalRead(CLOCKMECH_DIR);
    doc["gpio_STEP"]              = digitalRead(CLOCKMECH_STEP);
    doc["gpio_EN"]                = digitalRead(CLOCKMECH_EN);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}


// ============================================================
// Инициализация GPIO
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechInitGPIOs() {
    pinMode(CLOCKMECH_DIR,  OUTPUT);
    pinMode(CLOCKMECH_STEP, OUTPUT);
    pinMode(CLOCKMECH_EN,   OUTPUT);

    pinMode(CLOCKMECH_SENS_LED, OUTPUT);
    pinMode(CLOCKMECH_SENS_HOUR, INPUT);
    pinMode(CLOCKMECH_SENS_MIN, INPUT);

    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    digitalWrite(CLOCKMECH_STEP, LOW);
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
// MechMoveStepUp: STEP=LOW, затем SetTask(GoToTaskAfterStep)
// ============================================================

void MODULE_CLASS_CLOCKMECH::MechMoveStepDown() {
    digitalWrite(CLOCKMECH_STEP, HIGH);
    SetTimerTask(MechMoveStepUp, 2);
}
void MODULE_CLASS_CLOCKMECH::MechMoveStepUp() {
    digitalWrite(CLOCKMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStep, 2);
}

// ============================================================
// Терминальные команды
// ============================================================

void MODULE_CLASS_CLOCKMECH::cmdStep() {
    GoToTaskAfterStep = Idle_task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::cmdDir() {
    uint8_t dir = digitalRead(CLOCKMECH_DIR);
    if (dir == 1) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);}
    if (dir == 0) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);}
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
        GoToTaskAfterStep = MechNCmdStep; 
        SetTimerTask(MechMoveStepDown, 2);
    } else { GoToTaskAfterStep = Idle_task;  }
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
    GoToTaskAfterStep = MechNCmdStep;
    SetTask(MechMoveStepDown);
}
void MODULE_CLASS_CLOCKMECH::cmdSet1200()   { SetTask(MechSet1200_Setup); }
void MODULE_CLASS_CLOCKMECH::cmdCount()     { SetTask(MechCountStepsSetup); }
void MODULE_CLASS_CLOCKMECH::cmdPoll()      { SetTask(PollTimeTask); }

void MODULE_CLASS_CLOCKMECH::cmdSave() {    ModClassClockMech.saveConfig(); }
void MODULE_CLASS_CLOCKMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) {
        ModClassClockMech._config.enable_status = !ModClassClockMech._config.enable_status;
    } else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { ModClassClockMech._config.enable_status = MODE_DEBUG; }
        else if (s == "work")                 { ModClassClockMech._config.enable_status = MODE_WORK; }
        else { Serial.println("Usage: c-mode [dbg|work]"); return; }
    }
    Serial.printf("Mode: %s\r\n", ModClassClockMech._config.enable_status == MODE_WORK ? "WORK" : "DEBUG");
}

void MODULE_CLASS_CLOCKMECH::cmdSetArrows() {
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
    ModClassClockMech.CheckTime(h, m);
    Serial.printf("Set Arrows to: %02d:%02d\r\n",  ModClassClockMech._timeHourReal, ModClassClockMech._timeMinReal);
    SetTask(MechSetArrows);
}


void MODULE_CLASS_CLOCKMECH::cmdStatus() {

    time_t t = ModClassClockMech.getCurrentTime();
  
    Serial.printf("===== ClockMech Status =====\r\n");
    Serial.printf("_Mech_Status:          %d\r\n", ModClassClockMech._Mech_Status);
    Serial.printf("_config.enable_status: %d\r\n", ModClassClockMech._config.enable_status);
    Serial.printf("_config.timeSource:    %s\r\n", ModClassClockMech._config.timeSource.c_str());
    Serial.printf("_config.stepsPerRevolution: %d\r\n", ModClassClockMech._config.stepsPerRevolution);
    Serial.printf("_config.pollInterval: %d\r\n", ModClassClockMech._config.pollInterval);
    Serial.printf("_config.errorLimitSteps: %d\r\n", ModClassClockMech._config.errorLimitSteps);
    Serial.printf("_config.sensorLedEnabled: %d\r\n", ModClassClockMech._config.sensorLedEnabled);
    Serial.printf("_mechControlSteps: %d\r\n", ModClassClockMech._mechControlSteps);
    Serial.printf("_timeMechHour: %d _timeMechMin: %d\r\n", ModClassClockMech._timeMechHour, ModClassClockMech._timeMechMin);
    Serial.printf("_timeHourReal: %d _timeMinReal: %d\r\n", ModClassClockMech._timeHourReal,  ModClassClockMech._timeMinReal);
    Serial.printf("_minPrev:          %d\r\n", ModClassClockMech._minPrev);
    Serial.printf("_sensorLedState:   %d\r\n", ModClassClockMech._sensorLedState);
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        Serial.printf("time: %s\r\n", buf);
    }
    Serial.printf("mchCS:             %d\r\n", ModClassClockMech.mchCS);
    Serial.printf("SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(CLOCKMECH_SENS_HOUR),
        digitalRead(CLOCKMECH_SENS_MIN),
        digitalRead(CLOCKMECH_SENS_LED));
    Serial.printf("DIR=%d STEP=%d EN=%d\r\n",
        digitalRead(CLOCKMECH_DIR),
        digitalRead(CLOCKMECH_STEP),
        digitalRead(CLOCKMECH_EN));
    Serial.printf("=============================\r\n");
}

// ============================================================
// Сброс механизма в 12:00 
// ============================================================

void MODULE_CLASS_CLOCKMECH::handleReset(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    if (_config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "text/plain", "OK");
}



void MODULE_CLASS_CLOCKMECH::MechSet1200_Setup() {
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    if (SENS_SET) {  MechSet1200_endOk(); return; }
    ModClassClockMech._Mech_Status = STATUS_SET1200;
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);
    ModClassClockMech._mechControlSteps = 0;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}
void MODULE_CLASS_CLOCKMECH::MechSet1200_Task() {
    if (ModClassClockMech._Mech_Status != STATUS_SET1200) {return;}
    
    // превышен лимит шагов
    if (ModClassClockMech._mechControlSteps > ModClassClockMech._config.errorLimitSteps * 12) {
        MechSet1200_endFail();
        return;
    }
    
    // Нашли положение 12:00
    if (SENS_SET) { MechSet1200_endOk(); return; }
    // Двигаемся дальше
    ModClassClockMech._mechControlSteps++;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::MechSet1200_endOk() {
    ModClassClockMech._Mech_Status = STATUS_IDLE;
    GoToTaskAfterStep = Idle_task;
    ModClassClockMech._timeMechMin = 0;
    ModClassClockMech._timeMechHour = 0;
    ModClassClockMech._mechControlSteps = 0;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);

    if (ModClassClockMech._config.enable_status == MODE_WORK) {
        if (ModClassClockMech._config.stepsPerRevolution == 0)  {  SetTask(MechCountStepsSetup); }
        else { SetTask(PollTimeTask); }
    }
    DEBUGCLOCKMECH("MechSet1200_Task: done, position 12:00\r\n");
}

void MODULE_CLASS_CLOCKMECH::MechSet1200_endFail() {
    ModClassClockMech._Mech_Status = ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
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
    if (_config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "text/plain", "OK");
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsSetup() {
    ModClassClockMech._Mech_Status = STATUS_COUNTING;
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GetSens();

    ModClassClockMech._mechControlSteps = 0;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechCountStepsTask);
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsTask() {
    if (ModClassClockMech._Mech_Status != STATUS_COUNTING) {return;}
    if (ModClassClockMech._mechControlSteps >= ModClassClockMech._config.errorLimitSteps) {
        MechCountStepsFail();
        return;
    }
    if ( SENS_MIN_SET && ModClassClockMech._mechControlSteps > CLOCKMECH_MIN_STEPS_GAP) {
        MechCountStepsOk(); 
        return;
    }
    ModClassClockMech._mechControlSteps++;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsOk() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    GetSens(); cmdSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    
    ModClassClockMech._config.stepsPerRevolution = ModClassClockMech._mechControlSteps;
    if (ModClassClockMech._mechControlSteps != 0 ) { ModClassClockMech.saveConfig(); }
    GoToTaskAfterStep = Idle_task;
    ModClassClockMech._mechControlSteps = 0;
    ModClassClockMech._timeMechMin = 0;
    DEBUGCLOCKMECH("MechCountSteps: %d steps\r\n", ModClassClockMech._config.stepsPerRevolution);

    ModClassClockMech._Mech_Status = STATUS_IDLE;
    if (ModClassClockMech._config.enable_status == MODE_WORK) { SetTask(PollTimeTask); } 
}

void MODULE_CLASS_CLOCKMECH::MechCountStepsFail() {
    DEBUGCLOCKMECH("MechCountSteps: ERROR_NO_MECH\r\n");
    ModClassClockMech._Mech_Status = ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    ModClassClockMech._mechControlSteps = 0;

    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
}


// ============================================================
// Периодический опрос времени
// ============================================================

void MODULE_CLASS_CLOCKMECH::PollTimeTask() {
    SetTimerTask(PollTimeTask, ModClassClockMech._config.pollInterval * 1000UL);
    if (ModClassClockMech._config.enable_status != MODE_WORK) { return; }
    if (ModClassClockMech._Mech_Status != STATUS_IDLE) { return; }
    time_t t = ModClassClockMech.getCurrentTime();
    if (t > 0) {
        ModClassClockMech.CheckTime((uint8_t)hour(t), (uint8_t)minute(t));
        SetTask(MechSetArrows);
    }
}

void MODULE_CLASS_CLOCKMECH::CheckTime (uint8_t _inH, uint8_t _inM)	{
	if (_inH >= HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	if (_inM >= MININHOUR)      { _inM = MINMAX; }
	_timeHourReal = _inH;
	_timeMinReal = _inM;
}

// ============================================================
// Постановка стрелок
// ============================================================
void MODULE_CLASS_CLOCKMECH::MechSetArrows() {
    // if ( ModClassClockMech._minPrev == MINMAX ){ ModClassClockMech.mchCS = 0; }
	// else { ModClassClockMech.mchCS = HOURCONTROLDEF; }
    if (ModClassClockMech._Mech_Status != STATUS_IDLE) { return; }

    if (ModClassClockMech._timeMechHour < ModClassClockMech._timeHourReal) {
        SetTask(MechSetArrowHourSetup);
        return;
    }
    if (ModClassClockMech._timeMechHour > ModClassClockMech._timeHourReal) {
        SetTask(MechSet1200_Setup);
        return;
    }
    if (ModClassClockMech._timeMechHour == ModClassClockMech._timeHourReal) {
        if (ModClassClockMech._minPrev != ModClassClockMech._timeMinReal) {
            ModClassClockMech._minPrev = ModClassClockMech._timeMinReal;
            SetTask(MechSetArrowMinSetup);
        }
    }
}

// ============================================================
// Продвинуть часовую на +1 (12 оборотов минутной)
// ============================================================

void MODULE_CLASS_CLOCKMECH::MechSetArrowHourSetup() {
    ModClassClockMech._Mech_Status = STATUS_SETHOUR;
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GetSens();
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechSetArrowHourTask);
}

void MODULE_CLASS_CLOCKMECH::MechSetArrowHourTask() {
    if (ModClassClockMech._Mech_Status != STATUS_SETHOUR) {return;}
    if (ModClassClockMech._mechControlSteps > ModClassClockMech._config.errorLimitSteps) {
        MechSetArrowHourEndFail(); return;
    }
    if (SENS_MIN_SET && (ModClassClockMech._mechControlSteps > CLOCKMECH_MIN_STEPS_GAP)    ) {
        MechSetArrowHourEndOk(); return;
    }
    ModClassClockMech._mechControlSteps++;
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechMoveStepDown);
    
}

void MODULE_CLASS_CLOCKMECH::MechSetArrowHourEndOk() {
    GoToTaskAfterStep = Idle_task;
    ModClassClockMech._Mech_Status = STATUS_IDLE;
    GetSens();
    if (ModClassClockMech._timeMechHour == ModClassClockMech._timeHourReal) { DEBUGCLOCKMECH("tH_M == tH_R \r\n"); }
    ModClassClockMech._timeMechMin = 0;
    ModClassClockMech._mechControlSteps = 0;
    ModClassClockMech._timeMechHour++;
    if (ModClassClockMech._timeMechHour > 12) { ModClassClockMech._timeMechHour -= 12; }
    DEBUGCLOCKMECH("HOUR Arrow: %02d\r\n",  ModClassClockMech._timeMechHour);
    SetTask(MechSetArrows); // set all arows
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);

}

void MODULE_CLASS_CLOCKMECH::MechSetArrowHourEndFail() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    ModClassClockMech._Mech_Status = ERROR_NO_MECH;
    GetSens();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);

}

// ============================================================
// Довернуть минуты до целевого положения
// ============================================================
// void MODULE_CLASS_CLOCKMECH::MechSetArrowMinback() {
//     digitalWrite(CLOCKMECH_SENS_LED, HIGH);
//     digitalWrite(CLOCKMECH_EN, LOW);
//     digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);
//     ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);

//     if (SENS_MIN_SET) {
//         if (ModClassClockMech._mechControlSteps < ModClassClockMech._config.errorLimitSteps) {
//             ModClassClockMech._mechControlSteps++;
//             GoToTaskAfterStep = MechSetArrowMinback;
//             SetTask(MechMoveStepDown);
//         } else {
//             ModClassClockMech._Mech_Status = ERROR_NO_MECH;
//             digitalWrite(CLOCKMECH_EN, HIGH);
//             digitalWrite(CLOCKMECH_SENS_LED, LOW);
//             ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
//         }
//     } else {
//         ModClassClockMech._timeMechMin = 0;
//         ModClassClockMech._mechControlSteps = 0;
//         digitalWrite(CLOCKMECH_SENS_LED, LOW);
//         ModClassClockMech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
//         SetTask(MechSetArrowMin);
//     }
// }

void MODULE_CLASS_CLOCKMECH::MechSetArrowMinSetup() {
    ModClassClockMech._Mech_Status = STATUS_SETMIN;
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GetSens();
    GoToTaskAfterStep = MechSetArrowMinTask;
    SetTask(MechSetArrowMinTask);
}

void MODULE_CLASS_CLOCKMECH::MechSetArrowMinTask() {
    if (ModClassClockMech._Mech_Status != STATUS_SETMIN) {return;}
    if (ModClassClockMech._mechControlSteps >= ModClassClockMech._config.errorLimitSteps) {
        MechSetArrowMinFail(); return;
    }

    uint32_t MinPosSteps = MININHOUR * ModClassClockMech._mechControlSteps;
    uint32_t MinTimeSteps = ModClassClockMech._timeMinReal * ModClassClockMech._config.stepsPerRevolution;

    if ( MinPosSteps < MinTimeSteps ) {
        ModClassClockMech._mechControlSteps++;
        GoToTaskAfterStep = MechSetArrowMinTask;
        SetTask(MechMoveStepDown);
    }     
    if ( MinPosSteps >= MinTimeSteps ) {    SetTask(MechSetArrowMinOk); }
}

void MODULE_CLASS_CLOCKMECH::MechSetArrowMinOk() {
    ModClassClockMech._Mech_Status = STATUS_IDLE;
    GoToTaskAfterStep = Idle_task;
    GetSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    SetTask(MechSetArrows); // set all arows
    DEBUGCLOCKMECH("MIN MinPosSteps: %02d, MinTimeSteps: %d steps\r\n", MININHOUR * ModClassClockMech._mechControlSteps, ModClassClockMech._timeMinReal * ModClassClockMech._config.stepsPerRevolution);
}

void MODULE_CLASS_CLOCKMECH::MechSetArrowMinFail() {
    ModClassClockMech._Mech_Status = ERROR_NO_MECH;
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    DEBUGCLOCKMECH("MechSetArrowMinFail\r\n");
}


// ============================================================
// Конфиг
// ============================================================
void MODULE_CLASS_CLOCKMECH::defaultConfig() {
    _config.enable_status      = MODE_DEBUG;
    _config.timeSource         = "ds3231";
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.sensorLedEnabled   = true;
}

bool MODULE_CLASS_CLOCKMECH::loadConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc) == false) { return false; }

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

bool MODULE_CLASS_CLOCKMECH::saveConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc);
    doc["enable_status"]        = _config.enable_status;
    doc["timeSource"]           = _config.timeSource;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["sensorLedEnabled"]     = _config.sensorLedEnabled;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_CLOCKMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String MODULE_CLASS_CLOCKMECH::getVersionStr() { return String(DEVICE_CLOCK_MECH_VERSION);  }
String MODULE_CLASS_CLOCKMECH::getGeneratedTime() { return String(DEVICE_CLOCK_MECH_GENERATED_TIME);    }
String MODULE_CLASS_CLOCKMECH::getCommitDateStr() { return String(DEVICE_CLOCK_MECH_COMMIT_DATE_STR);   }

void MODULE_CLASS_CLOCKMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "clockmechversion|" + getVersionStr()    + "|div\n";
    values += "clockmechgentime|" + getGeneratedTime() + "|div\n";
    values += "clockmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
