#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_mech-ring.h"
#include "common.h"
#include "device_mech-ring_version.h"
#include "eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

DPDR GoToTaskAfterStepRing = Idle_task;

MODULE_CLASS_RINGMECH ModClassRingMech(false);
MODULE_CLASS_RINGMECH::MODULE_CLASS_RINGMECH(bool _in) { dumb = _in; }
void MODULE_CLASS_RINGMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }
volatile uint16_t step_time = RINGMECH_SPEED_DEFAULT;

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
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    GoToTaskAfterStepRing = Idle_task;
    _mechControlSteps = 0;
    _ringStatus = RING_STATUS_IDLE;
    _sensorState = digitalRead(RINGMECH_SENS);

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    TerminalRegisterModule(ringMechTerminalRegister);
    MechInitGPIOs();

    if (_config.enable_status == RING_MODE_WORK) { SetTask(MechHomeSetup); }
    SetTask(RingPollTask);
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
        this->handleInfo_ring(request);
    });

    ESPHTTPServer.on("/ring-mech/turn",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdTurnWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/en",     HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdEnWeb(request);   else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/sens",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSensWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/home",   HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdHomeWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/count",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdCountWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/status", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStatusWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/reset",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdResetWeb(request);  else request->requestAuthentication(); });
    ESPHTTPServer.on("/ring-mech/ver",    HTTP_GET, [this](AsyncWebServerRequest *request) { this->html_ver_get(request);});
}

void ringMechTerminalRegister() {
    term.addCommand("r-enc",   MODULE_CLASS_RINGMECH::cmdEn);
    term.addCommand("r-sens",  MODULE_CLASS_RINGMECH::cmdSens);
    term.addCommand("r-home",  MODULE_CLASS_RINGMECH::cmdHome);
    term.addCommand("r-cnt",   MODULE_CLASS_RINGMECH::cmdCount);
    term.addCommand("r-mode",  MODULE_CLASS_RINGMECH::cmdMode);
    term.addCommand("r-stat",  MODULE_CLASS_RINGMECH::cmdStatus);
    term.addCommand("r-save",  MODULE_CLASS_RINGMECH::cmdSave);
    term.addCommand("r-trn",   MODULE_CLASS_RINGMECH::cmdTurn);
    term.addCommand("r-time",  MODULE_CLASS_RINGMECH::cmdTime);
    term.addCommand("r-src",   MODULE_CLASS_RINGMECH::cmdSource);
}

// ============================================================
// Веб-обработчики
// ============================================================
void MODULE_CLASS_RINGMECH::handleInfo_ring(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    doc["enable_status"]        = _config.enable_status;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["ringPauseOne"]         = _config.ringPauseOne;
    doc["ringPauseTwo"]         = _config.ringPauseTwo;
    doc["firstPosition"]        = _config.firstPosition;
    doc["time_begin"]           = _config.time_begin;
    doc["time_end"]             = _config.time_end;
    doc["timeSource"]           = _config.timeSource;
    doc["status"]               = _ringStatus;
    doc["mechControlSteps"]     = _mechControlSteps;
    doc["turnCount"]            = _mechTurnTarget;
    doc["sensor"]               = digitalRead(RINGMECH_SENS);
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
            else if (name == "stepsPerRevolution")  { _config.stepsPerRevolution = (uint16_t)val.toInt(); }
            else if (name == "pollInterval")        { _config.pollInterval = (uint16_t)val.toInt(); if (_config.pollInterval < 1) _config.pollInterval = 1; }
            else if (name == "errorLimitSteps")     { _config.errorLimitSteps = (uint16_t)val.toInt(); }
            else if (name == "ringPauseOne")           { _config.ringPauseOne = (uint16_t)val.toInt(); if (_config.ringPauseOne < 1) _config.ringPauseOne = RINGMECH_RING_FIRST_PAUSE_DEFAULT; }
            else if (name == "ringPauseTwo")           { _config.ringPauseTwo = (uint16_t)val.toInt(); if (_config.ringPauseTwo < 1) _config.ringPauseTwo = RINGMECH_RING_SECON_PAUSE_DEFAULT; }
            else if (name == "firstPosition")       { _config.firstPosition = (uint8_t)val.toInt(); }
            else if (name == "time_begin") {
                uint8_t v = (uint8_t)constrain(val.toInt(), 0, 23);
                if (v == 0 && _config.time_end == 0) { _config.time_begin = 0; }
                else if (v <= _config.time_end) _config.time_begin = v;
            }
            else if (name == "time_end") {
                uint8_t v = (uint8_t)constrain(val.toInt(), 0, 23);
                if (_config.time_begin == 0 && v == 0) { _config.time_end = 0; }
                else if (v >= _config.time_begin) _config.time_end = v;
            }
            else if (name == "timeSource")          { _config.timeSource = val; }
        }
        saveConfig();
        request->send(200, "text/plain", "OK");
    }
}

// ============================================================
// Эндпоинты ручного управления (GET, JSON)
// ============================================================

void MODULE_CLASS_RINGMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS"]  = digitalRead(RINGMECH_SENS);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void MODULE_CLASS_RINGMECH::cmdHomeWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechHomeSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void MODULE_CLASS_RINGMECH::cmdTurnWeb(AsyncWebServerRequest *request) {
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    if (ModClassRingMech._mechTurnTarget != 0) { request->send(429, "application/json", "{\"error\":\"Busy\"}"); return; }
    ModClassRingMech._mechTurnTarget = (uint16_t)request->arg("count").toInt();
    if (ModClassRingMech._mechTurnTarget == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    SetTask(MechTurnNCount);
    request->send(200, "application/json", "{\"ok\":true}");
}

void MODULE_CLASS_RINGMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_ringStatus"]            = ModClassRingMech._ringStatus;
    doc["enable_status"]          = ModClassRingMech._config.enable_status;
    doc["stepsPerRevolution"]     = ModClassRingMech._config.stepsPerRevolution;
    doc["pollInterval"]           = ModClassRingMech._config.pollInterval;
    doc["errorLimitSteps"]        = ModClassRingMech._config.errorLimitSteps;
    doc["_mechControlSteps"]      = ModClassRingMech._mechControlSteps;
    doc["_sensorState"]           = ModClassRingMech._sensorState;
    doc["gpio_SENS"]              = digitalRead(RINGMECH_SENS);
    doc["gpio_STEP"]              = digitalRead(RINGMECH_STEP);
    doc["gpio_EN"]                = digitalRead(RINGMECH_EN);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void MODULE_CLASS_RINGMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._mechTurnTarget = 0;
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================
// Инициализация GPIO
// ============================================================
void MODULE_CLASS_RINGMECH::MechInitGPIOs() {
    pinMode(RINGMECH_STEP, OUTPUT);
    pinMode(RINGMECH_EN,   OUTPUT);

    pinMode(RINGMECH_SENS, INPUT_PULLUP);
    digitalWrite(RINGMECH_STEP, LOW);
    digitalWrite(RINGMECH_EN, HIGH);
}

// ============================================================
// Генератор шага A4988
// ============================================================
void MODULE_CLASS_RINGMECH::MechMoveStepDown() {
    digitalWrite(RINGMECH_STEP, HIGH);
    SetTimerTask(MechMoveStepUp, step_time);
}
void MODULE_CLASS_RINGMECH::MechMoveStepUp() {
    digitalWrite(RINGMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStepRing, step_time);
}

// ============================================================
// Терминальные команды
// ============================================================


void MODULE_CLASS_RINGMECH::cmdEn() {
    bool en = digitalRead(RINGMECH_EN);
    digitalWrite(RINGMECH_EN, en == LOW ? HIGH : LOW);
    Serial.printf("Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}

void MODULE_CLASS_RINGMECH::cmdSens() { Serial.printf("SENS=%d\r\n", digitalRead(RINGMECH_SENS)); }
void MODULE_CLASS_RINGMECH::cmdHome() { SetTask(MechHomeSetup); }
void MODULE_CLASS_RINGMECH::cmdCount() { SetTask(MechCountStepsSetup); }
void MODULE_CLASS_RINGMECH::cmdSave() { ModClassRingMech.saveConfig(); }



void MODULE_CLASS_RINGMECH::cmdTurn() {
    if (ModClassRingMech._mechTurnTarget != 0) { Serial.println("Busy: previous r-turn still running"); return; }
    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) { Serial.println("Blocked: WORK mode"); return; }
    char *arg = term.getNext();
    if (arg == NULL) { Serial.println("Usage: r-turn <N>"); return; }
    ModClassRingMech._mechTurnTarget = (uint16_t)atoi(arg);
    if (ModClassRingMech._mechTurnTarget == 0) return;
    SetTask(MechTurnNCount);
}

void MODULE_CLASS_RINGMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) { ModClassRingMech._config.enable_status = !ModClassRingMech._config.enable_status; } 
    else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { ModClassRingMech._config.enable_status = RING_MODE_DEBUG; }
        else if (s == "work")                 { ModClassRingMech._config.enable_status = RING_MODE_WORK; }
        else { Serial.println("Usage: c-mode [dbg|work]"); return; }
    }
    Serial.printf("Mode: %s\r\n", ModClassRingMech._config.enable_status == RING_MODE_WORK ? "WORK" : "DEBUG");
}

void MODULE_CLASS_RINGMECH::cmdStatus() {
    Serial.printf("===== RingMech Status =====\r\n");
    Serial.printf("_ringStatus:           %d\r\n",      ModClassRingMech._ringStatus);
    Serial.printf("_config.enable_status: %d\r\n",      ModClassRingMech._config.enable_status);
    Serial.printf("_config.stepsPerRevolution: %d\r\n", ModClassRingMech._config.stepsPerRevolution);
    Serial.printf("_config.pollInterval:  %d\r\n",      ModClassRingMech._config.pollInterval);
    Serial.printf("_config.errorLimitSteps: %d\r\n",    ModClassRingMech._config.errorLimitSteps);
    Serial.printf("_config.ringPauseOne:     %d\r\n",      ModClassRingMech._config.ringPauseOne);
    Serial.printf("_config.ringPauseTwo:     %d\r\n",      ModClassRingMech._config.ringPauseTwo);
    Serial.printf("_config.firstPosition: %d\r\n",      ModClassRingMech._config.firstPosition);
    Serial.printf("_config.time_begin:    %d\r\n",      ModClassRingMech._config.time_begin);
    Serial.printf("_config.time_end:      %d\r\n",      ModClassRingMech._config.time_end);
    Serial.printf("_config.timeSource:    %s\r\n",      ModClassRingMech._config.timeSource.c_str());
    Serial.printf("_mechControlSteps:     %d\r\n",      ModClassRingMech._mechControlSteps);
    Serial.printf("_mechTurnTarget:       %d\r\n",      ModClassRingMech._mechTurnTarget);
    Serial.printf("_sensorState:          %d\r\n",      ModClassRingMech._sensorState);
    time_t t = ModClassRingMech.getCurrentTime();
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        Serial.printf("time: %s\r\n", buf);
    }
    Serial.printf("SENS=%d STEP=%d EN=%d\r\n",
        digitalRead(RINGMECH_SENS),
        digitalRead(RINGMECH_STEP),
        digitalRead(RINGMECH_EN));
    Serial.printf("=============================\r\n");
}

void MODULE_CLASS_RINGMECH::cmdTime() {
    char *arg1 = term.getNext();
    if (arg1 == NULL) {
        Serial.printf("time_begin=%d time_end=%d\r\n", ModClassRingMech._config.time_begin, ModClassRingMech._config.time_end);
        return;
    }
    uint8_t b = (uint8_t)atoi(arg1);
    if (b > 23) { Serial.println("Error: value must be 0-23"); return; }
    char *arg2 = term.getNext();
    if (arg2 == NULL) { Serial.println("Usage: r-time <begin> <end>"); return; }
    uint8_t e = (uint8_t)atoi(arg2);
    if (e > 23) { Serial.println("Error: value must be 0-23"); return; }
    if (b > e) { Serial.println("Error: begin must be <= end"); return; }
    ModClassRingMech._config.time_begin = b;
    ModClassRingMech._config.time_end   = e;
    Serial.println("OK");
}

void MODULE_CLASS_RINGMECH::cmdSource() {
    char *arg = term.getNext();
    if (arg == NULL) {
        Serial.printf("timeSource=%s\r\n", ModClassRingMech._config.timeSource.c_str());
        return;
    }
    String s(arg);
    if (s == "ds3231" || s == "ntp") {
        ModClassRingMech._config.timeSource = s;
        Serial.println("OK");
    } else {
        Serial.println("Usage: r-src [ds3231|ntp]");
    }
}

// ============================================================
// Хоминг — поиск метки сенсора
// ============================================================


void MODULE_CLASS_RINGMECH::MechHomeSetup() {
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    ModClassRingMech._ringStatus = RING_STATUS_HOMING;
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void MODULE_CLASS_RINGMECH::MechHomeTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_HOMING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps) { MechHomeEndFail(); return; }
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::MechHomeEndOk() {
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);

    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) {
        if (ModClassRingMech._config.stepsPerRevolution == 0) { SetTask(MechCountStepsSetup); }
    }
    DEBUGRINGMECH("MechHome: done, sensor found\r\n");
}

void MODULE_CLASS_RINGMECH::MechHomeEndFail() {
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStepRing = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    DEBUGRINGMECH("MechHome: RING_ERROR_NO_MECH (%d steps)\r\n", ModClassRingMech._mechControlSteps);
    ModClassRingMech._mechControlSteps = 0;
}


// ============================================================
// Подсчёт шагов на оборот (калибровка)
// ============================================================


void MODULE_CLASS_RINGMECH::MechCountStepsSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_COUNTING;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void MODULE_CLASS_RINGMECH::MechCountStepsTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_COUNTING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (ModClassRingMech._mechControlSteps >= ModClassRingMech._config.errorLimitSteps) { MechCountStepsFail(); return; }
    if (SENS_TRIGGERED && ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechCountStepsOk(); return; }
    ModClassRingMech._mechControlSteps++; 
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::MechCountStepsOk() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    ModClassRingMech._config.stepsPerRevolution = ModClassRingMech._mechControlSteps;
    if (ModClassRingMech._mechControlSteps != 0) { ModClassRingMech.saveConfig(); }
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    DEBUGRINGMECH("MechCountSteps: %d steps\r\n", ModClassRingMech._config.stepsPerRevolution);
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    
}

void MODULE_CLASS_RINGMECH::MechCountStepsFail() {
    DEBUGRINGMECH("MechCountSteps: RING_ERROR_NO_MECH\r\n");
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
}

// ============================================================
// Вращение на N оборотов (r-turn)
// ============================================================

void MODULE_CLASS_RINGMECH::MechTurnNCount() {
    if (ModClassRingMech._ringStatus != RING_STATUS_IDLE ) { return; }
    if (ModClassRingMech._mechTurnTarget == 0) { return; }
    DEBUGRINGMECH("MechTurn: %d \r\n", ModClassRingMech._mechTurnTarget);
    SetTask(MechTurnNSetup);
    ModClassRingMech._mechTurnTarget --;
}

void MODULE_CLASS_RINGMECH::MechTurnNSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_TURN;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechTurnNTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}


void MODULE_CLASS_RINGMECH::MechTurnNTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_TURN) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (SENS_TRIGGERED && ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechTurnNEndOk(); return; }
    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps) { MechTurnNEndFail(); return; }
    
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStepRing = MechTurnNTask;

    if ( ModClassRingMech._mechControlSteps == ModClassRingMech._config.firstPosition  ) { 
        SetTimerTask(MechMoveStepDown, ModClassRingMech._config.ringPauseOne); 
    }
    else { SetTask(MechMoveStepDown); }
}

void MODULE_CLASS_RINGMECH::MechTurnNEndOk() {
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    SetTimerTask(MechTurnNCount, ModClassRingMech._config.ringPauseTwo);
}

void MODULE_CLASS_RINGMECH::MechTurnNEndFail() {
    DEBUGRINGMECH("MechTurn: fail at step %d, turn %d\r\n", ModClassRingMech._mechControlSteps, ModClassRingMech._mechTurnTarget);
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
}

// ============================================================
// Периодический опрос (Wheel rotation in WORK mode)
// ============================================================
void MODULE_CLASS_RINGMECH::RingPollTask() {
    SetTimerTask(RingPollTask, ModClassRingMech._config.pollInterval * 1000UL);
    if (ModClassRingMech._config.enable_status != RING_MODE_WORK) { return; }
    if (ModClassRingMech._ringStatus != RING_STATUS_IDLE) { return; }

    time_t t = ModClassRingMech.getCurrentTime();
    
    if (t == 0) { return; }
    uint8_t hourBegin = ModClassRingMech._config.time_begin;
    uint8_t hourEnd = ModClassRingMech._config.time_end;
    uint8_t timeNowHour = (uint8_t)hour(t) ;
    uint8_t timeNowMin = (uint8_t)minute(t) ;
    uint8_t timeNowSec = (uint8_t)second(t) ;

    // if ((hourBegin > timeNowHour) && (timeNowHour > hourEnd  )  ){ return; } 
    if (timeNowHour < hourBegin || timeNowHour > hourEnd) { return; } // мы НЕ в рабочем диапазоне
    
    if ( timeNowMin == 0 && timeNowSec <= 6 ){ // в начале часа
            ModClassRingMech.CheckTime(timeNowHour);
            SetTask(MechTurnNCount);
    }
}

void MODULE_CLASS_RINGMECH::CheckTime (uint8_t _inH)	{
	if (_inH >= HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	_mechTurnTarget = _inH;
}


// ============================================================
// Конфиг
// ============================================================
void MODULE_CLASS_RINGMECH::defaultConfig() {
    _config.enable_status      = RING_MODE_DEBUG;
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.ringPauseOne        = RINGMECH_RING_FIRST_PAUSE_DEFAULT ;
    _config.ringPauseTwo        = RINGMECH_RING_SECON_PAUSE_DEFAULT;
    _config.firstPosition       = RINGMECH_FIRST_POSITION_DEFAULT;
    _config.time_begin          = RINGMECH_TIME_BEGIN_DEFAULT;
    _config.time_end            = RINGMECH_TIME_END_DEFAULT;
    _config.timeSource          = "ds3231";
}

bool MODULE_CLASS_RINGMECH::loadConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc) == false) { return false; }

    _config.enable_status       = doc["enable_status"].as<uint8_t>();
    _config.stepsPerRevolution  = doc["stepsPerRevolution"].as<uint16_t>();
    _config.pollInterval        = doc["pollInterval"].as<uint16_t>();
    _config.errorLimitSteps     = doc["errorLimitSteps"].as<uint16_t>();
    _config.ringPauseOne           = doc["ringPauseOne"].as<uint16_t>();
    _config.ringPauseTwo           = doc["ringPauseTwo"].as<uint16_t>();
    _config.firstPosition       = doc["firstPosition"].as<uint8_t>();
    _config.time_begin           = doc["time_begin"].as<uint8_t>();
    _config.time_end             = doc["time_end"].as<uint8_t>();
    _config.timeSource           = doc["timeSource"].as<String>();

    if (_config.timeSource != "ds3231" && _config.timeSource != "ntp") { _config.timeSource = "ds3231"; }
    if (_config.time_begin > 23) _config.time_begin = RINGMECH_TIME_BEGIN_DEFAULT;
    if (_config.time_end   > 23) _config.time_end   = RINGMECH_TIME_END_DEFAULT;
    if (_config.time_begin > _config.time_end && !(_config.time_begin == 0 && _config.time_end == 0)) { _config.time_begin = RINGMECH_TIME_BEGIN_DEFAULT; _config.time_end = RINGMECH_TIME_END_DEFAULT; }

    if (_config.pollInterval < 1)   { _config.pollInterval = 5; }
    if (_config.stepsPerRevolution < 1) { _config.stepsPerRevolution = 400; }
    if (_config.ringPauseOne < 1)      { _config.ringPauseOne = RINGMECH_RING_FIRST_PAUSE_DEFAULT; }
    if (_config.ringPauseTwo < 1)      { _config.ringPauseTwo = RINGMECH_RING_SECON_PAUSE_DEFAULT; }

    return true;
}

bool MODULE_CLASS_RINGMECH::saveConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc);
    doc["enable_status"]        = _config.enable_status;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["ringPauseOne"]         = _config.ringPauseOne;
    doc["ringPauseTwo"]         = _config.ringPauseTwo;
    doc["firstPosition"]        = _config.firstPosition;
    doc["time_begin"]           = _config.time_begin;
    doc["time_end"]             = _config.time_end;
    doc["timeSource"]           = _config.timeSource;
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
