#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_mech-ring.h"
#include "common.h"
#include "device_mech-ring_version.h"
#include "eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

DPDR GoToTaskAfterStepRing = Idle_task;

CLASS_DEVICE_RINGMECH device_mech_ring(false);
CLASS_DEVICE_RINGMECH::CLASS_DEVICE_RINGMECH(bool _in) { dumb = _in; }
void CLASS_DEVICE_RINGMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }
volatile uint16_t step_time = RINGMECH_SPEED_DEFAULT;

// ============================================================
// begin()
// ============================================================
void CLASS_DEVICE_RINGMECH::begin() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    GoToTaskAfterStepRing = Idle_task;
    _mechControlSteps = 0;
    _ringStatus = RING_STATUS_IDLE;
    _sensorState = digitalRead(RINGMECH_SENS);
    _timeHourPrev = 0xFF;

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    TerminalRegisterModule(ringMechTerminalRegister);
    MechInitGPIOs();

    if (_config.enable_status == RING_MODE_WORK) { SetTask(MechHomeSetup); }
    SetTask(RingPollTask);
}

void CLASS_DEVICE_RINGMECH::begin(ModContext& ctx) {
#if defined(ESP32)
    _fs = ctx.fs;
#endif
    begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_DEVICE_RINGMECH::web_Init() {
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

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_DEVICE_RINGMECH::handleInfo_ring(AsyncWebServerRequest *request) {
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

void CLASS_DEVICE_RINGMECH::handleSave(AsyncWebServerRequest *request) {
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

// Эндпоинты ручного управления (GET, JSON)

void CLASS_DEVICE_RINGMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS"]  = digitalRead(RINGMECH_SENS);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_RINGMECH::cmdHomeWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechHomeSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_RINGMECH::cmdTurnWeb(AsyncWebServerRequest *request) {
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    if (device_mech_ring._mechTurnTarget != 0) { request->send(429, "application/json", "{\"error\":\"Busy\"}"); return; }
    device_mech_ring._mechTurnTarget = (uint16_t)request->arg("count").toInt();
    if (device_mech_ring._mechTurnTarget == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    SetTask(MechTurnNCount);
    request->send(200, "application/json", "{\"ok\":true}");
}

void CLASS_DEVICE_RINGMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_ringStatus"]            = device_mech_ring._ringStatus;
    doc["enable_status"]          = device_mech_ring._config.enable_status;
    doc["stepsPerRevolution"]     = device_mech_ring._config.stepsPerRevolution;
    doc["pollInterval"]           = device_mech_ring._config.pollInterval;
    doc["errorLimitSteps"]        = device_mech_ring._config.errorLimitSteps;
    doc["_mechControlSteps"]      = device_mech_ring._mechControlSteps;
    doc["_sensorState"]           = device_mech_ring._sensorState;
    doc["gpio_SENS"]              = digitalRead(RINGMECH_SENS);
    doc["gpio_STEP"]              = digitalRead(RINGMECH_STEP);
    doc["gpio_EN"]                = digitalRead(RINGMECH_EN);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_RINGMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._mechTurnTarget = 0;
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    request->send(200, "application/json", "{\"ok\":true}");
}

// ============================================================
// Конфиг
// ============================================================
void CLASS_DEVICE_RINGMECH::defaultConfig() {
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

bool CLASS_DEVICE_RINGMECH::loadConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc) == false) { return false; }

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

bool CLASS_DEVICE_RINGMECH::saveConfig() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_RINGMECH, doc);
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
    return core_json.jsonFileSaveDoc(CONFIG_FILE_RINGMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_DEVICE_RINGMECH::getVersionStr() { return String(DEVICE_MECH_RING_VERSION);  }
String CLASS_DEVICE_RINGMECH::getGeneratedTime() { return String(DEVICE_MECH_RING_GENERATED_TIME);    }
String CLASS_DEVICE_RINGMECH::getCommitDateStr() { return String(DEVICE_MECH_RING_COMMIT_DATE_STR);   }

void CLASS_DEVICE_RINGMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ringmechversion|" + getVersionStr()    + "|div\n";
    values += "ringmechgentime|" + getGeneratedTime() + "|div\n";
    values += "ringmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// ============================================================
// Регистрация терминальных команд
// ============================================================
void ringMechTerminalRegister() {
    term.addCommand("r-enc",   CLASS_DEVICE_RINGMECH::cmdEn);
    term.addCommand("r-sens",  CLASS_DEVICE_RINGMECH::cmdSens);
    term.addCommand("r-home",  CLASS_DEVICE_RINGMECH::cmdHome);
    term.addCommand("r-cnt",   CLASS_DEVICE_RINGMECH::cmdCount);
    term.addCommand("r-mode",  CLASS_DEVICE_RINGMECH::cmdMode);
    term.addCommand("r-stat",  CLASS_DEVICE_RINGMECH::cmdStatus);
    term.addCommand("r-save",  CLASS_DEVICE_RINGMECH::cmdSave);
    term.addCommand("r-trn",   CLASS_DEVICE_RINGMECH::cmdTurn);
    term.addCommand("r-time",  CLASS_DEVICE_RINGMECH::cmdTime);
    term.addCommand("r-src",   CLASS_DEVICE_RINGMECH::cmdSource);
}

// ============================================================
// Время из источника
// ============================================================
time_t CLASS_DEVICE_RINGMECH::getCurrentTime() {
#if defined(MODULE_DS3231)
    if (_config.timeSource == "ds3231") {
        time_t t = module_ds3231.getTime();
        if (t > 0) { return t; }
        DEBUGRINGMECH("DS3231 error, fallback to NTP\r\n");
    }
#endif
    return now();
}

// ============================================================
// Инициализация GPIO
// ============================================================
void CLASS_DEVICE_RINGMECH::MechInitGPIOs() {
    pinMode(RINGMECH_STEP, OUTPUT);
    pinMode(RINGMECH_EN,   OUTPUT);

    pinMode(RINGMECH_SENS, INPUT_PULLUP);
    digitalWrite(RINGMECH_STEP, LOW);
    digitalWrite(RINGMECH_EN, HIGH);
}

// ============================================================
// Генератор шага A4988
// ============================================================
void CLASS_DEVICE_RINGMECH::MechMoveStepDown() {
    digitalWrite(RINGMECH_STEP, HIGH);
    SetTimerTask(MechMoveStepUp, step_time);
}
void CLASS_DEVICE_RINGMECH::MechMoveStepUp() {
    digitalWrite(RINGMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStepRing, step_time);
}

// ============================================================
// Хоминг — поиск метки сенсора
// ============================================================

void CLASS_DEVICE_RINGMECH::MechHomeSetup() {
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    device_mech_ring._ringStatus = RING_STATUS_HOMING;
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void CLASS_DEVICE_RINGMECH::MechHomeTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_HOMING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (device_mech_ring._mechControlSteps > device_mech_ring._config.errorLimitSteps) { MechHomeEndFail(); return; }
    if (SENS_TRIGGERED) { MechHomeEndOk(); return; }
    device_mech_ring._mechControlSteps++;
    GoToTaskAfterStepRing = MechHomeTask;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_RINGMECH::MechHomeEndOk() {
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);

    if (device_mech_ring._config.enable_status == RING_MODE_WORK) {
        if (device_mech_ring._config.stepsPerRevolution == 0) { SetTask(MechCountStepsSetup); }
    }
    DEBUGRINGMECH("MechHome: done, sensor found\r\n");
}

void CLASS_DEVICE_RINGMECH::MechHomeEndFail() {
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStepRing = Idle_task;
    digitalWrite(RINGMECH_EN, HIGH);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    DEBUGRINGMECH("MechHome: RING_ERROR_NO_MECH (%d steps)\r\n", device_mech_ring._mechControlSteps);
    device_mech_ring._mechControlSteps = 0;
}

// ============================================================
// Подсчёт шагов на оборот (калибровка)
// ============================================================

void CLASS_DEVICE_RINGMECH::MechCountStepsSetup() {
    device_mech_ring._ringStatus = RING_STATUS_COUNTING;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}

void CLASS_DEVICE_RINGMECH::MechCountStepsTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_COUNTING) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (device_mech_ring._mechControlSteps >= device_mech_ring._config.errorLimitSteps) { MechCountStepsFail(); return; }
    if (SENS_TRIGGERED && device_mech_ring._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechCountStepsOk(); return; }
    device_mech_ring._mechControlSteps++; 
    GoToTaskAfterStepRing = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_RINGMECH::MechCountStepsOk() {
    DEBUGRINGMECH("%s\r\n", __FUNCTION__);
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    device_mech_ring._config.stepsPerRevolution = device_mech_ring._mechControlSteps;
    if (device_mech_ring._mechControlSteps != 0) { device_mech_ring.saveConfig(); }
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    DEBUGRINGMECH("MechCountSteps: %d steps\r\n", device_mech_ring._config.stepsPerRevolution);
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    
}

void CLASS_DEVICE_RINGMECH::MechCountStepsFail() {
    DEBUGRINGMECH("MechCountSteps: RING_ERROR_NO_MECH\r\n");
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
}

// ============================================================
// Вращение на N оборотов (r-turn)
// ============================================================

void CLASS_DEVICE_RINGMECH::MechTurnNCount() {
    if (device_mech_ring._ringStatus != RING_STATUS_IDLE ) { return; }
    if (device_mech_ring._mechTurnTarget == 0) { return; }
    DEBUGRINGMECH("MechTurn: %d \r\n", device_mech_ring._mechTurnTarget);
    SetTask(MechTurnNSetup);
    device_mech_ring._mechTurnTarget --;
}

void CLASS_DEVICE_RINGMECH::MechTurnNSetup() {
    device_mech_ring._ringStatus = RING_STATUS_TURN;
    digitalWrite(RINGMECH_SENS_LED, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_EN, LOW);
    device_mech_ring._mechControlSteps = 0;
    GoToTaskAfterStepRing = MechTurnNTask;
    SetTask(MechMoveStepDown);
    step_time = RINGMECH_SPEED_DEFAULT;
}


void CLASS_DEVICE_RINGMECH::MechTurnNTask() {
    if (device_mech_ring._ringStatus != RING_STATUS_TURN) { return; }

    // Светодиод сенсоров общий с часовым механизмом (GPIO 27).
    // Включаем на каждой итерации, чтобы он не был сброшен логикой часов.
    digitalWrite(RINGMECH_SENS_LED, HIGH);

    if (SENS_TRIGGERED && device_mech_ring._mechControlSteps > RINGMECH_MIN_STEPS_GAP) { MechTurnNEndOk(); return; }
    if (device_mech_ring._mechControlSteps > device_mech_ring._config.errorLimitSteps) { MechTurnNEndFail(); return; }
    
    device_mech_ring._mechControlSteps++;
    GoToTaskAfterStepRing = MechTurnNTask;

    if ( device_mech_ring._mechControlSteps == device_mech_ring._config.firstPosition  ) { 
        SetTimerTask(MechMoveStepDown, device_mech_ring._config.ringPauseOne); 
    }
    else { SetTask(MechMoveStepDown); }
}

void CLASS_DEVICE_RINGMECH::MechTurnNEndOk() {
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._ringStatus = RING_STATUS_IDLE;
    SetTimerTask(MechTurnNCount, device_mech_ring._config.ringPauseTwo);
}

void CLASS_DEVICE_RINGMECH::MechTurnNEndFail() {
    DEBUGRINGMECH("MechTurn: fail at step %d, turn %d\r\n", device_mech_ring._mechControlSteps, device_mech_ring._mechTurnTarget);
    digitalWrite(RINGMECH_EN, HIGH);
    device_mech_ring._sensorState = digitalRead(RINGMECH_SENS);
    digitalWrite(RINGMECH_SENS_LED, LOW);
    GoToTaskAfterStepRing = Idle_task;
    device_mech_ring._mechControlSteps = 0;
    device_mech_ring._ringStatus = RING_ERROR_NO_MECH;
}

// ============================================================
// Периодический опрос (Wheel rotation in WORK mode)
// ============================================================
void CLASS_DEVICE_RINGMECH::RingPollTask() {
    SetTimerTask(RingPollTask, device_mech_ring._config.pollInterval * 1000UL);
    if (device_mech_ring._config.enable_status != RING_MODE_WORK) { return; }
    if (device_mech_ring._ringStatus != RING_STATUS_IDLE) { return; }

    time_t t = device_mech_ring.getCurrentTime();
    
    if (t == 0) { return; }
    uint8_t hourBegin = device_mech_ring._config.time_begin;
    uint8_t hourEnd = device_mech_ring._config.time_end;
    uint8_t timeNowHour = (uint8_t)hour(t) ;
    uint8_t timeNowMin = (uint8_t)minute(t) ;

    if (timeNowHour < hourBegin || timeNowHour > hourEnd) { return; } // мы НЕ в рабочем диапазоне
    if (device_mech_ring._timeHourPrev == timeNowHour){ return; } // час новый.
    if ( timeNowMin == 0  ){ // в начале часа, ноль минут.
       
            device_mech_ring._timeHourPrev = timeNowHour;
            device_mech_ring.CheckTime(timeNowHour);
            SetTask(MechTurnNCount);
        
    }
}

void CLASS_DEVICE_RINGMECH::CheckTime (uint8_t _inH)	{
	if (_inH >= HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	_mechTurnTarget = _inH;
}

// ============================================================
// Терминальные команды
// ============================================================

void CLASS_DEVICE_RINGMECH::cmdEn() {
    bool en = digitalRead(RINGMECH_EN);
    digitalWrite(RINGMECH_EN, en == LOW ? HIGH : LOW);
    DBG_MOD("[D_CLOCKRING] ", "Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}

void CLASS_DEVICE_RINGMECH::cmdSens() { DBG_MOD("[D_CLOCKRING] ", "SENS=%d\r\n", digitalRead(RINGMECH_SENS)); }
void CLASS_DEVICE_RINGMECH::cmdHome() { SetTask(MechHomeSetup); }
void CLASS_DEVICE_RINGMECH::cmdCount() { SetTask(MechCountStepsSetup); }
void CLASS_DEVICE_RINGMECH::cmdSave() { device_mech_ring.saveConfig(); }

void CLASS_DEVICE_RINGMECH::cmdTurn() {
    if (device_mech_ring._mechTurnTarget != 0) { DBG_MOD("[D_CLOCKRING] ", "Busy: previous r-turn still running\r\n"); return; }
    if (device_mech_ring._config.enable_status == RING_MODE_WORK) { DBG_MOD("[D_CLOCKRING] ", "Blocked: WORK mode\r\n"); return; }
    char *arg = term.getNext();
    if (arg == NULL) { DBG_MOD("[D_CLOCKRING] ", "Usage: r-turn <N>\r\n"); return; }
    device_mech_ring._mechTurnTarget = (uint16_t)atoi(arg);
    if (device_mech_ring._mechTurnTarget == 0) return;
    SetTask(MechTurnNCount);
}

void CLASS_DEVICE_RINGMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) { device_mech_ring._config.enable_status = !device_mech_ring._config.enable_status; } 
    else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { device_mech_ring._config.enable_status = RING_MODE_DEBUG; }
        else if (s == "work")                 { device_mech_ring._config.enable_status = RING_MODE_WORK; }
        else { DBG_MOD("[D_CLOCKRING] ", "Usage: c-mode [dbg|work]\r\n"); return; }
    }
    DBG_MOD("[D_CLOCKRING] ", "Mode: %s\r\n", device_mech_ring._config.enable_status == RING_MODE_WORK ? "WORK" : "DEBUG");
}

void CLASS_DEVICE_RINGMECH::cmdStatus() {
    DBG_MOD("[D_CLOCKRING] ", "===== RingMech Status =====\r\n");
    DBG_MOD("[D_CLOCKRING] ", "_ringStatus:           %d\r\n",      device_mech_ring._ringStatus);
    DBG_MOD("[D_CLOCKRING] ", "_config.enable_status: %d\r\n",      device_mech_ring._config.enable_status);
    DBG_MOD("[D_CLOCKRING] ", "_config.stepsPerRevolution: %d\r\n", device_mech_ring._config.stepsPerRevolution);
    DBG_MOD("[D_CLOCKRING] ", "_config.pollInterval:  %d\r\n",      device_mech_ring._config.pollInterval);
    DBG_MOD("[D_CLOCKRING] ", "_config.errorLimitSteps: %d\r\n",    device_mech_ring._config.errorLimitSteps);
    DBG_MOD("[D_CLOCKRING] ", "_config.ringPauseOne:     %d\r\n",      device_mech_ring._config.ringPauseOne);
    DBG_MOD("[D_CLOCKRING] ", "_config.ringPauseTwo:     %d\r\n",      device_mech_ring._config.ringPauseTwo);
    DBG_MOD("[D_CLOCKRING] ", "_config.firstPosition: %d\r\n",      device_mech_ring._config.firstPosition);
    DBG_MOD("[D_CLOCKRING] ", "_config.time_begin:    %d\r\n",      device_mech_ring._config.time_begin);
    DBG_MOD("[D_CLOCKRING] ", "_config.time_end:      %d\r\n",      device_mech_ring._config.time_end);
    DBG_MOD("[D_CLOCKRING] ", "_config.timeSource:    %s\r\n",      device_mech_ring._config.timeSource.c_str());
    DBG_MOD("[D_CLOCKRING] ", "_mechControlSteps:     %d\r\n",      device_mech_ring._mechControlSteps);
    DBG_MOD("[D_CLOCKRING] ", "_mechTurnTarget:       %d\r\n",      device_mech_ring._mechTurnTarget);
    DBG_MOD("[D_CLOCKRING] ", "_sensorState:          %d\r\n",      device_mech_ring._sensorState);
    time_t t = device_mech_ring.getCurrentTime();
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        DBG_MOD("[D_CLOCKRING] ", "time: %s\r\n", buf);
    }
    DBG_MOD("[D_CLOCKRING] ", "SENS=%d STEP=%d EN=%d\r\n",
        digitalRead(RINGMECH_SENS),
        digitalRead(RINGMECH_STEP),
        digitalRead(RINGMECH_EN));
    DBG_MOD("[D_CLOCKRING] ", "=============================\r\n");
}

void CLASS_DEVICE_RINGMECH::cmdTime() {
    char *arg1 = term.getNext();
    if (arg1 == NULL) {
        DBG_MOD("[D_CLOCKRING] ", "time_begin=%d time_end=%d\r\n", device_mech_ring._config.time_begin, device_mech_ring._config.time_end);
        return;
    }
    uint8_t b = (uint8_t)atoi(arg1);
    if (b > 23) { DBG_MOD("[D_CLOCKRING] ", "Error: value must be 0-23\r\n"); return; }
    char *arg2 = term.getNext();
    if (arg2 == NULL) { DBG_MOD("[D_CLOCKRING] ", "Usage: r-time <begin> <end>\r\n"); return; }
    uint8_t e = (uint8_t)atoi(arg2);
    if (e > 23) { DBG_MOD("[D_CLOCKRING] ", "Error: value must be 0-23\r\n"); return; }
    if (b > e) { DBG_MOD("[D_CLOCKRING] ", "Error: begin must be <= end\r\n"); return; }
    device_mech_ring._config.time_begin = b;
    device_mech_ring._config.time_end   = e;
    DBG_MOD("[D_CLOCKRING] ", "OK\r\n");
}

void CLASS_DEVICE_RINGMECH::cmdSource() {
    char *arg = term.getNext();
    if (arg == NULL) {
        DBG_MOD("[D_CLOCKRING] ", "timeSource=%s\r\n", device_mech_ring._config.timeSource.c_str());
        return;
    }
    String s(arg);
    if (s == "ds3231" || s == "ntp") {
        device_mech_ring._config.timeSource = s;
        DBG_MOD("[D_CLOCKRING] ", "OK\r\n");
    } else {
        DBG_MOD("[D_CLOCKRING] ", "Usage: r-src [ds3231|ntp]\r\n");
    }
}
