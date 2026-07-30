#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_clock-mech/device_clock-mech.h"
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
    doc["status"]               = _ringStatus;
    doc["mechControlSteps"]     = _mechControlSteps;
    doc["turnCount"]            = _mechTurnTarget;
    doc["sensor"]               = digitalRead(RINGMECH_SENS);
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
    Serial.printf("_mechControlSteps:     %d\r\n",      ModClassRingMech._mechControlSteps);
    Serial.printf("_mechTurnTarget:       %d\r\n",      ModClassRingMech._mechTurnTarget);
    Serial.printf("_sensorState:          %d\r\n",      ModClassRingMech._sensorState);
    Serial.printf("SENS=%d STEP=%d EN=%d\r\n",
        digitalRead(RINGMECH_SENS),
        digitalRead(RINGMECH_STEP),
        digitalRead(RINGMECH_EN));
    Serial.printf("=============================\r\n");
}

// ============================================================
// Хоминг — поиск метки сенсора
// ============================================================


void MODULE_CLASS_RINGMECH::MechHomeSetup() {
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
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

    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps) {
        MechHomeEndFail();
        return;
    }
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
    digitalWrite(CLOCKMECH_SENS_LED, LOW);

    if (ModClassRingMech._config.enable_status == RING_MODE_WORK) {
        if (ModClassRingMech._config.stepsPerRevolution == 0) { SetTask(MechCountStepsSetup); }
        
    }
    DEBUGRINGMECH("MechHome: done, sensor found\r\n");
}

void MODULE_CLASS_RINGMECH::MechHomeEndFail() {
    ModClassRingMech._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStepRing = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    DEBUGRINGMECH("MechHome: RING_ERROR_NO_MECH (%d steps)\r\n", ModClassRingMech._mechControlSteps);
    ModClassRingMech._mechControlSteps = 0;
}


// ============================================================
// Подсчёт шагов на оборот (калибровка)
// ============================================================


void MODULE_CLASS_RINGMECH::MechCountStepsSetup() {
    ModClassRingMech._ringStatus = RING_STATUS_COUNTING;
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void MODULE_CLASS_RINGMECH::MechCountStepsTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_COUNTING) { return; }
    if (ModClassRingMech._mechControlSteps >= ModClassRingMech._config.errorLimitSteps) {
        MechCountStepsFail();
        return;
    }
    if (SENS_TRIGGERED && ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP) {
        MechCountStepsOk();
        return;
    }
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void MODULE_CLASS_RINGMECH::MechCountStepsOk() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
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
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
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
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    ModClassRingMech._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechTurnNTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}


void MODULE_CLASS_RINGMECH::MechTurnNTask() {
    if (ModClassRingMech._ringStatus != RING_STATUS_TURN) { return; }
    if (SENS_TRIGGERED && ModClassRingMech._mechControlSteps > RINGMECH_MIN_STEPS_GAP) {
        MechTurnNEndOk(); return;
    }
    if (ModClassRingMech._mechControlSteps > ModClassRingMech._config.errorLimitSteps) {
        MechTurnNEndFail(); return;
    }
    ModClassRingMech._mechControlSteps++;
    GoToTaskAfterStepRing = MechTurnNTask;

    if ( ModClassRingMech._mechControlSteps == ModClassRingMech._config.firstPosition  ) {
        SetTimerTask(MechMoveStepDown, ModClassRingMech._config.ringPauseOne);
    } else { SetTask(MechMoveStepDown); }
}

void MODULE_CLASS_RINGMECH::MechTurnNEndOk() {
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    ModClassRingMech._mechControlSteps = 0;
    ModClassRingMech._ringStatus = RING_STATUS_IDLE;
    
    SetTimerTask(MechTurnNCount, ModClassRingMech._config.ringPauseTwo);
    // SetTask(MechTurnNCount);
}

void MODULE_CLASS_RINGMECH::MechTurnNEndFail() {
    DEBUGRINGMECH("MechTurn: fail at step %d, turn %d\r\n", ModClassRingMech._mechControlSteps, ModClassRingMech._mechTurnTarget);
    digitalWrite(RINGMECH_EN, HIGH);
    ModClassRingMech._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
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
    // SetTask(MechRotationSetup);
}

// ============================================================
// Конфиг
// ============================================================
void MODULE_CLASS_RINGMECH::defaultConfig() {
    _config.enable_status      = RING_MODE_DEBUG;
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.ringPauseOne          = RINGMECH_RING_FIRST_PAUSE_DEFAULT ;
    _config.ringPauseTwo          = RINGMECH_RING_SECON_PAUSE_DEFAULT;
    _config.firstPosition      = RINGMECH_FIRST_POSITION_DEFAULT;
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
