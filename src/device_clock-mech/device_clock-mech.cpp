#include "core_web/FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_clock-mech.h"
#include "common/common.h"
#include "device_clock-mech_version.h"
#include "core_sys/eertos.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"
#include "core_led/core_led.h"

#if defined(ESP8266)
#include <avr/pgmspace.h>
#endif

#if defined(MODULE_DS3231)
#include "module_ds3231/module_ds3231.h"
#endif

#include "common/TimeLib.h"

DPDR GoToTaskAfterStep = Idle_task;

static uint16_t _nStepCount = 0;

// Время последнего шага (для детектора занятости ядра в MechMoveStepDown)
static uint32_t _lastStepDownUs = 0;

// ============================================================
// Паттерн светодиодной индикации ошибки (кассета модуля)
// ============================================================

static const char patDevError[] PROGMEM = LED_PATTERN_DEV_ERROR;

CLASS_DEVICE_CLOCKMECH device_clock_mech(false);
CLASS_DEVICE_CLOCKMECH::CLASS_DEVICE_CLOCKMECH(bool _in) { dumb = _in; }
void CLASS_DEVICE_CLOCKMECH::setFs(fs::LittleFSFS* fs)  {   _fs = fs;   }

// ============================================================
// begin()
// ============================================================
void CLASS_DEVICE_CLOCKMECH::begin() {
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
    ledClearState(LED_PRIO_DEV); // сброс моргания ошибки устройства
    GetSens();
    
    defaultConfig(); //конфиги
    if (loadConfig() == false) { saveConfig(); }

    TerminalRegisterModule(clockMechTerminalRegister); // терминал
    MechInitGPIOs(); // инит GPIO

    if (_config.enable_status == MODE_WORK) {SetTask(MechSet1200_Setup); } // если норм режим то работаем
    SetTask(PollTimeTask);
}

void CLASS_DEVICE_CLOCKMECH::begin(ModContext& ctx) {
#if defined(ESP32)
    _fs = ctx.fs;
#endif
    begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_DEVICE_CLOCKMECH::web_Init() {
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
    ESPHTTPServer.on("/clock-mech/set-xx00", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSetxx00Web(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/set-12xx", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdSet12xxWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/count",  HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdCountWeb(request); else request->requestAuthentication(); });
    ESPHTTPServer.on("/clock-mech/status", HTTP_GET, [](AsyncWebServerRequest *request) { if (ESPHTTPServer.checkAuth(request)) cmdStatusWeb(request); else request->requestAuthentication(); });

    ESPHTTPServer.on("/clock-mech/ver", HTTP_GET, [this](AsyncWebServerRequest *request) { this->html_ver_get(request);});
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_DEVICE_CLOCKMECH::handleInfo(AsyncWebServerRequest *request) {
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
    } else { doc["currentTime"] = "N/A"; }
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void CLASS_DEVICE_CLOCKMECH::handleSave(AsyncWebServerRequest *request) {
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

// Эндпоинты ручного управления (GET, JSON)
void CLASS_DEVICE_CLOCKMECH::cmdStepWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdStep();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdDirWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    cmdDir();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdEnWeb(AsyncWebServerRequest *request) {
    cmdEn();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdSledWeb(AsyncWebServerRequest *request) {
    cmdSled();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdSensWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["SENS_HOUR"] = digitalRead(CLOCKMECH_SENS_HOUR);
    doc["SENS_MIN"]  = digitalRead(CLOCKMECH_SENS_MIN);
    doc["SENS_LED"]  = digitalRead(CLOCKMECH_SENS_LED);
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}
void CLASS_DEVICE_CLOCKMECH::cmdNWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    if (!request->hasArg("count")) { request->send(400, "application/json", "{\"error\":\"Missing count\"}"); return; }
    _nStepCount = (uint16_t)request->arg("count").toInt();
    if (_nStepCount == 0) { request->send(200, "application/json", "{\"ok\":true}"); return; }
    digitalWrite(CLOCKMECH_EN, LOW);
    GoToTaskAfterStep = MechNCmdStep;
    MechMoveStepDown();
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdResetWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdSetxx00Web(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSetxx00_Setup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdSet12xxWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet12xx_Setup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdCountWeb(AsyncWebServerRequest *request) {
    if (device_clock_mech._config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "application/json", "{\"ok\":true}");
}
void CLASS_DEVICE_CLOCKMECH::cmdStatusWeb(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["_Mech_Status"]            = device_clock_mech._Mech_Status;
    doc["enable_status"]           = device_clock_mech._config.enable_status;
    doc["timeSource"]              = device_clock_mech._config.timeSource;
    doc["stepsPerRevolution"]      = device_clock_mech._config.stepsPerRevolution;
    doc["pollInterval"]            = device_clock_mech._config.pollInterval;
    doc["errorLimitSteps"]         = device_clock_mech._config.errorLimitSteps;
    doc["sensorLedEnabled"]        = device_clock_mech._config.sensorLedEnabled;
    doc["_mechControlSteps"]       = device_clock_mech._mechControlSteps;
    doc["_timeMechMin"]            = device_clock_mech._timeMechMin;
    doc["_timeMechHour"]           = device_clock_mech._timeMechHour;
    doc["_timeMinReal"]            = device_clock_mech._timeMinReal;
    doc["_timeHourReal"]           = device_clock_mech._timeHourReal;
    doc["_minPrev"]                = device_clock_mech._minPrev;
    doc["_sensorLedState"]         = device_clock_mech._sensorLedState;
    doc["_sensorLedStateHOUR"]     = device_clock_mech._sensorLedStateHOUR;
    doc["_sensorLedStateMIN"]      = device_clock_mech._sensorLedStateMIN;
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

void CLASS_DEVICE_CLOCKMECH::handleReset(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    if (_config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechSet1200_Setup);
    request->send(200, "text/plain", "OK");
}

void CLASS_DEVICE_CLOCKMECH::handleCount(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    if (_config.enable_status == MODE_WORK) { request->send(403, "application/json", "{\"error\":\"Blocked: WORK mode\"}"); return; }
    SetTask(MechCountStepsSetup);
    request->send(200, "text/plain", "OK");
}

// ============================================================
// Конфиг
// ============================================================
void CLASS_DEVICE_CLOCKMECH::defaultConfig() {
    _config.enable_status      = MODE_DEBUG;
    _config.timeSource         = "ds3231";
    _config.stepsPerRevolution = 0;
    _config.pollInterval       = 5;
    _config.errorLimitSteps    = 500;
    _config.sensorLedEnabled   = true;
}

bool CLASS_DEVICE_CLOCKMECH::loadConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc) == false) { return false; }

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

bool CLASS_DEVICE_CLOCKMECH::saveConfig() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc);
    doc["enable_status"]        = _config.enable_status;
    doc["timeSource"]           = _config.timeSource;
    doc["stepsPerRevolution"]   = _config.stepsPerRevolution;
    doc["pollInterval"]         = _config.pollInterval;
    doc["errorLimitSteps"]      = _config.errorLimitSteps;
    doc["sensorLedEnabled"]     = _config.sensorLedEnabled;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_CLOCKMECH, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_DEVICE_CLOCKMECH::getVersionStr() { return String(DEVICE_CLOCK_MECH_VERSION);  }
String CLASS_DEVICE_CLOCKMECH::getGeneratedTime() { return String(DEVICE_CLOCK_MECH_GENERATED_TIME);    }
String CLASS_DEVICE_CLOCKMECH::getCommitDateStr() { return String(DEVICE_CLOCK_MECH_COMMIT_DATE_STR);   }

void CLASS_DEVICE_CLOCKMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "clockmechversion|" + getVersionStr()    + "|div\n";
    values += "clockmechgentime|" + getGeneratedTime() + "|div\n";
    values += "clockmechgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// ============================================================
// Светодиодная индикация ошибки устройства
// ============================================================
void CLASS_DEVICE_CLOCKMECH::ledMacrosClockMechError() {
    ledSetState(LED_PRIO_DEV, patDevError, -1);
}
// ============================================================
// Регистрация терминальных команд
// ============================================================
void clockMechTerminalRegister() {
    term.addCommand("c-step",   CLASS_DEVICE_CLOCKMECH::cmdStep);
    term.addCommand("c-dir",    CLASS_DEVICE_CLOCKMECH::cmdDir);
    term.addCommand("c-enc",     CLASS_DEVICE_CLOCKMECH::cmdEn);
    term.addCommand("c-sled",   CLASS_DEVICE_CLOCKMECH::cmdSled);
    term.addCommand("c-sens",   CLASS_DEVICE_CLOCKMECH::cmdSens);
    term.addCommand("c-n",      CLASS_DEVICE_CLOCKMECH::cmdN);
    term.addCommand("c-12",     CLASS_DEVICE_CLOCKMECH::cmdSet1200);
    term.addCommand("c-cnt",    CLASS_DEVICE_CLOCKMECH::cmdCount);
    term.addCommand("c-mode",   CLASS_DEVICE_CLOCKMECH::cmdMode);
    term.addCommand("c-pol",    CLASS_DEVICE_CLOCKMECH::cmdPoll);
    term.addCommand("c-stat",   CLASS_DEVICE_CLOCKMECH::cmdStatus);
    term.addCommand("c-set",   CLASS_DEVICE_CLOCKMECH::cmdSetArrows);
    term.addCommand("c-save",   CLASS_DEVICE_CLOCKMECH::cmdSave);
    term.addCommand("c-m00",   CLASS_DEVICE_CLOCKMECH::cmdSetxx00);
    term.addCommand("c-h12",   CLASS_DEVICE_CLOCKMECH::cmdSet12xx);
}

// ============================================================
// Время из источника
// ============================================================
time_t CLASS_DEVICE_CLOCKMECH::getCurrentTime() {
#if defined(MODULE_DS3231)
    if (_config.timeSource == "ds3231") {
        time_t t = module_ds3231.getTime();
        if (t > 0) { return t; }
        DEBUGCLOCKMECH("DS3231 error, fallback to NTP\r\n");
    }
#endif
    return now();
}

// ============================================================
// Вспомогательные макросы сенсоров
// ============================================================
#define SENS_MIN_SET    (digitalRead(CLOCKMECH_SENS_MIN) == LOW)
#define SENS_HOUR_SET   (digitalRead(CLOCKMECH_SENS_HOUR) == LOW)
#define SENS_ANY_SET    (SENS_MIN_SET || SENS_HOUR_SET)
#define SENS_SET        (SENS_MIN_SET && SENS_HOUR_SET)

// ============================================================
// Инициализация GPIO
// ============================================================
void CLASS_DEVICE_CLOCKMECH::MechInitGPIOs() {
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
// Генератор шага A4988 — эталонный паттерн из Clock_Mechanism.c
// MechMoveStepDown: STEP=HIGH, через 2ms MechMoveStepUp
// MechMoveStepUp: STEP=LOW, затем SetTask(GoToTaskAfterStep)
// ============================================================

void CLASS_DEVICE_CLOCKMECH::MechMoveStepDown() {
    digitalWrite(CLOCKMECH_STEP, HIGH);

    // Проверка занятости ядра: если предыдущий цикл шага (HIGH->LOW->задача)
    // занял заметно больше штатных ~4 мс — планировщик/таймеры задерживались
    // (сеть, веб, чтение FS). Тогда следующую фазу откладываем, механизм
    // «встаёт» на паузу; логика при этом не меняется.
    bool coreBusy = false;
    if (_lastStepDownUs != 0) {
        uint32_t gapMs = (micros() - _lastStepDownUs) / 1000UL;
        if ((gapMs > CLOCKMECH_STEP_GAP_BUSY_MS) && (gapMs < CLOCKMECH_STEP_GAP_RESET_MS)) {
            coreBusy = true;
        }
    }
    uint32_t stepDelayMs = CLOCKMECH_STEP_DELAY_MS;
    if (coreBusy) {
        // Пауза из-за занятости ядра. Сбрасываем базу отсчёта: собственная
        // пауза не должна восприниматься как новая занятость на следующем шаге.
        stepDelayMs = CLOCKMECH_STEP_BUSY_MS;
        _lastStepDownUs = 0;
    } else {
        _lastStepDownUs = micros();
    }
    SetTimerTask(MechMoveStepUp, stepDelayMs);
}
void CLASS_DEVICE_CLOCKMECH::MechMoveStepUp() {
    digitalWrite(CLOCKMECH_STEP, LOW);
    SetTimerTask(GoToTaskAfterStep, 2);
}

void CLASS_DEVICE_CLOCKMECH::GetSens() {
    device_clock_mech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
    device_clock_mech._sensorLedStateHOUR = digitalRead(CLOCKMECH_SENS_HOUR);
    device_clock_mech._sensorLedStateMIN = digitalRead(CLOCKMECH_SENS_MIN);
}

// ============================================================
// Периодический опрос времени. Основной рабочий цикл.
// ============================================================

void CLASS_DEVICE_CLOCKMECH::PollTimeTask() {
    SetTimerTask(PollTimeTask, device_clock_mech._config.pollInterval * 1000UL);
    if (device_clock_mech._config.enable_status != MODE_WORK) { return; }
    if (device_clock_mech._Mech_Status != STATUS_IDLE) { return; }
    time_t t = device_clock_mech.getCurrentTime();
    if (t > 0) {
        device_clock_mech.MechTimeSet((uint8_t)hour(t), (uint8_t)minute(t));
        SetTask(MechSetArrows);
    }
}

void CLASS_DEVICE_CLOCKMECH::MechTimeSet (uint8_t _inH, uint8_t _inM)	{
	if (_inH >= HOURINCIRCLE)   { _inH -= HOURINCIRCLE; }
	if (_inH >= HOURINCIRCLE)   { _inH = 0; }
	if (_inM >= MININHOUR)      { _inM = MINMAX; }
	if (_inM >= MININHOUR)      { _inM = 0; }
	_timeHourReal = _inH;
	_timeMinReal = _inM;
}

// ============================================================
// Постановка стрелок в нормальном режиме работы.
// ============================================================
void CLASS_DEVICE_CLOCKMECH::MechSetArrows() {
    if (device_clock_mech._Mech_Status != STATUS_IDLE) { return; }
    if (device_clock_mech._timeMechHour > device_clock_mech._timeHourReal) { SetTask(MechSet1200_Setup); return; }
    if (device_clock_mech._timeMechHour < device_clock_mech._timeHourReal) { SetTask(MechSetArrowHourSetup); return; }
    // if (device_clock_mech._timeMechHour == device_clock_mech._timeHourReal) {
        if (device_clock_mech._minPrev != device_clock_mech._timeMinReal) {
            device_clock_mech._minPrev = device_clock_mech._timeMinReal;
            SetTask(MechSetArrowMinSetup);
        }
    // }
}

// ============================================================
// Сброс механизма в хх:00 (выставляем ТОЛЬКО МИНУТНУЮ стрелку)
// ============================================================
void CLASS_DEVICE_CLOCKMECH::MechSetxx00_Setup() {
    GetSens();
    if (SENS_MIN_SET) {  MechSetxx00_endOk(); return; }
    device_clock_mech._Mech_Status = STATUS_SETXX00;
    digitalWrite(CLOCKMECH_EN, LOW);
    device_clock_mech._mechControlSteps = 0;
    GoToTaskAfterStep = MechSetxx00_Task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechSetxx00_Task() {
    if (device_clock_mech._Mech_Status != STATUS_SETXX00) {return;}
    // Нашли положение xx:00
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    if (SENS_MIN_SET) { MechSetxx00_endOk(); return; }
    // превышен лимит шагов
    device_clock_mech._mechControlSteps++;
    if (device_clock_mech._mechControlSteps > device_clock_mech._config.errorLimitSteps)  { MechSetxx00_endFail(); return; }
    // Двигаемся дальше
    GoToTaskAfterStep = MechSetxx00_Task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechSetxx00_endOk() {
    device_clock_mech._Mech_Status = STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStep = Idle_task;
    device_clock_mech._timeMechMin = 0;
    device_clock_mech._timeMechHour = 0;
    device_clock_mech._mechControlSteps = 0;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();

    DEBUGCLOCKMECH("MechSetXX00_Task: done, position XX:00\r\n");
}

void CLASS_DEVICE_CLOCKMECH::MechSetxx00_endFail() {
    device_clock_mech._Mech_Status = ERROR_NO_MIN;
    ledMacrosClockMechError();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();
    DEBUGCLOCKMECH("MechSet1200: ERROR_NO_MIN (%d)\r\n", device_clock_mech._mechControlSteps);
    device_clock_mech._mechControlSteps = 0;
}

// ============================================================
// Сброс механизма в 12:хх
// ============================================================
void CLASS_DEVICE_CLOCKMECH::MechSet12xx_Setup() {
    GetSens();
    if (SENS_HOUR_SET) { MechSet12xx_endOk(); return; }
    device_clock_mech._Mech_Status = STATUS_SET12XX;
    digitalWrite(CLOCKMECH_EN, LOW);
    device_clock_mech._mechControlSteps = 0;
    GoToTaskAfterStep = MechSet12xx_Task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechSet12xx_Task() {
    if (device_clock_mech._Mech_Status != STATUS_SET12XX) {return;}
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    // Нашли положение xx:00
    if (SENS_HOUR_SET) { MechSet12xx_endOk(); return; }
    device_clock_mech._mechControlSteps++;
    // превышен лимит шагов
    if (device_clock_mech._mechControlSteps > device_clock_mech._config.errorLimitSteps * 12 ) { MechSet12xx_endFail(); return; } // умножение на 12 важно только здесь.
    // Двигаемся дальше
    GoToTaskAfterStep = MechSet12xx_Task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechSet12xx_endOk() {
    device_clock_mech._Mech_Status = STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStep = Idle_task;
    device_clock_mech._timeMechMin = 0;
    device_clock_mech._timeMechHour = 0;
    device_clock_mech._mechControlSteps = 0;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();
    DEBUGCLOCKMECH("MechSet12xx_Task: done, position 12:xx\r\n");
}

void CLASS_DEVICE_CLOCKMECH::MechSet12xx_endFail() {
    device_clock_mech._Mech_Status = ERROR_NO_HOUR;
    ledMacrosClockMechError();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();
    DEBUGCLOCKMECH("MechSet12xx_Task: ERROR_NO_HOUR (%d)\r\n", device_clock_mech._mechControlSteps);
    device_clock_mech._mechControlSteps = 0;
}

// ============================================================
// Сброс механизма в 12:00  (work)
// ============================================================
void CLASS_DEVICE_CLOCKMECH::MechSet1200_Setup() {
    if (SENS_SET) {  MechSet1200_endOk(); return; }
    GetSens();
    device_clock_mech._Mech_Status = STATUS_SET1200;
    digitalWrite(CLOCKMECH_EN, LOW);
    // digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);
    device_clock_mech._mechControlSteps = 0;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}
void CLASS_DEVICE_CLOCKMECH::MechSet1200_Task() {
    if (device_clock_mech._Mech_Status != STATUS_SET1200) {return;}
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    // Нашли положение 12:00
    if (SENS_SET) { MechSet1200_endOk(); return; }
    // превышен лимит шагов на 12 оборотов минутной стрелки
    if (device_clock_mech._mechControlSteps > device_clock_mech._config.errorLimitSteps * 12) { MechSet1200_endFail(); return; }
    // Двигаемся дальше
    device_clock_mech._mechControlSteps++;
    GoToTaskAfterStep = MechSet1200_Task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechSet1200_endOk() {
    device_clock_mech._Mech_Status = STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStep = Idle_task;
    device_clock_mech._timeMechMin = 0;
    device_clock_mech._timeMechHour = 0;
    device_clock_mech._mechControlSteps = 0;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();

    if (device_clock_mech._config.enable_status == MODE_WORK) {
        if (device_clock_mech._config.stepsPerRevolution == 0)  {  SetTask(MechCountStepsSetup); }
        else { SetTask(PollTimeTask); }
    }
    DEBUGCLOCKMECH("MechSet1200_Task: done, position 12:00\r\n");
}

void CLASS_DEVICE_CLOCKMECH::MechSet1200_endFail() {
    device_clock_mech._Mech_Status = ERROR_NO_MECH;
    ledMacrosClockMechError();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();
    DEBUGCLOCKMECH("MechSet1200_Task: ERROR_NO_MECH (%d)\r\n", device_clock_mech._mechControlSteps);
    device_clock_mech._mechControlSteps = 0;
}

// ============================================================
// Подсчёт шагов на оборот
// ============================================================

void CLASS_DEVICE_CLOCKMECH::MechCountStepsSetup() {
    device_clock_mech._Mech_Status = STATUS_COUNTING;
    GetSens();
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);

    device_clock_mech._mechControlSteps = 0;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechCountStepsTask);
}

void CLASS_DEVICE_CLOCKMECH::MechCountStepsTask() {
    if (device_clock_mech._Mech_Status != STATUS_COUNTING) {return;}
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    if (device_clock_mech._mechControlSteps >= device_clock_mech._config.errorLimitSteps) { MechCountStepsFail(); return;   }
    if ( SENS_MIN_SET && device_clock_mech._mechControlSteps > CLOCKMECH_MIN_STEPS_GAP) { MechCountStepsOk();  return; }
    device_clock_mech._mechControlSteps++;
    GoToTaskAfterStep = MechCountStepsTask;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::MechCountStepsOk() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    ledClearState(LED_PRIO_DEV);
    GetSens(); cmdSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    
    device_clock_mech._config.stepsPerRevolution = device_clock_mech._mechControlSteps;
    if (device_clock_mech._mechControlSteps != 0 ) { device_clock_mech.saveConfig(); }
    GoToTaskAfterStep = Idle_task;
    device_clock_mech._mechControlSteps = 0;
    device_clock_mech._timeMechMin = 0;
    DEBUGCLOCKMECH("MechCountSteps: %d steps\r\n", device_clock_mech._config.stepsPerRevolution);

    device_clock_mech._Mech_Status = STATUS_IDLE;
    if (device_clock_mech._config.enable_status == MODE_WORK) { SetTask(PollTimeTask); } 
}

void CLASS_DEVICE_CLOCKMECH::MechCountStepsFail() {
    DEBUGCLOCKMECH("MechCountSteps: ERROR_NO_MECH\r\n");
    device_clock_mech._Mech_Status = ERROR_NO_MECH;
    ledMacrosClockMechError();
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    device_clock_mech._mechControlSteps = 0;

    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    device_clock_mech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
}

// ============================================================
// Продвинуть часовую на +1 (12 оборотов минутной)
// ============================================================

void CLASS_DEVICE_CLOCKMECH::MechSetArrowHourSetup() {
    device_clock_mech._Mech_Status = STATUS_SETHOUR;
    
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechSetArrowHourTask);
}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowHourTask() {
    if (device_clock_mech._Mech_Status != STATUS_SETHOUR) {return;}
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    GetSens();
    if (SENS_MIN_SET == true && (device_clock_mech._mechControlSteps > CLOCKMECH_MIN_STEPS_GAP) ) { MechSetArrowHourEndOk(); return; }
    if (device_clock_mech._mechControlSteps > device_clock_mech._config.errorLimitSteps) { MechSetArrowHourEndFail(); return; }
    device_clock_mech._mechControlSteps++;
    GoToTaskAfterStep = MechSetArrowHourTask;
    SetTask(MechMoveStepDown);
    
}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowHourEndOk() {
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStep = Idle_task;
    GetSens();
    if (device_clock_mech._timeMechHour == device_clock_mech._timeHourReal) { DEBUGCLOCKMECH("tH_M == tH_R \r\n"); }
    device_clock_mech._timeMechMin = 0;
    device_clock_mech._mechControlSteps = 0;
    device_clock_mech._timeMechHour++;
    if (device_clock_mech._timeMechHour > 12) { device_clock_mech._timeMechHour -= 12; }
    DEBUGCLOCKMECH("\t\t HOUR Arrow: %02d\r\n",  device_clock_mech._timeMechHour);
    SetTask(MechSetArrows); // set all arows
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    device_clock_mech._Mech_Status = STATUS_IDLE;

}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowHourEndFail() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    device_clock_mech._Mech_Status = ERROR_NO_MECH;
    ledMacrosClockMechError();
    GetSens();
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);

}

// ============================================================
// Довернуть минуты до целевого положения
// ============================================================
// void CLASS_DEVICE_CLOCKMECH::MechSetArrowMinback() {
//     digitalWrite(CLOCKMECH_SENS_LED, HIGH);
//     digitalWrite(CLOCKMECH_EN, LOW);
//     digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);
//     device_clock_mech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);

//     if (SENS_MIN_SET) {
//         if (device_clock_mech._mechControlSteps < device_clock_mech._config.errorLimitSteps) {
//             device_clock_mech._mechControlSteps++;
//             GoToTaskAfterStep = MechSetArrowMinback;
//             SetTask(MechMoveStepDown);
//         } else {
//             device_clock_mech._Mech_Status = ERROR_NO_MECH;
//             digitalWrite(CLOCKMECH_EN, HIGH);
//             digitalWrite(CLOCKMECH_SENS_LED, LOW);
//             device_clock_mech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
//         }
//     } else {
//         device_clock_mech._timeMechMin = 0;
//         device_clock_mech._mechControlSteps = 0;
//         digitalWrite(CLOCKMECH_SENS_LED, LOW);
//         device_clock_mech._sensorLedState = digitalRead(CLOCKMECH_SENS_LED);
//         SetTask(MechSetArrowMin);
//     }
// }

void CLASS_DEVICE_CLOCKMECH::MechSetArrowMinSetup() {
    device_clock_mech._Mech_Status = STATUS_SETMIN;
    
    digitalWrite(CLOCKMECH_EN, LOW);
    digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);
    GoToTaskAfterStep = MechSetArrowMinTask;
    SetTask(MechSetArrowMinTask);
}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowMinTask() {
    if (device_clock_mech._Mech_Status != STATUS_SETMIN) {return;}
    if (device_clock_mech._mechControlSteps >= device_clock_mech._config.errorLimitSteps) { MechSetArrowMinFail(); return; }
    digitalWrite(CLOCKMECH_SENS_LED, HIGH);
    GetSens();
    uint32_t MinPosSteps = MININHOUR * device_clock_mech._mechControlSteps;
    uint32_t MinTimeSteps = device_clock_mech._timeMinReal * device_clock_mech._config.stepsPerRevolution;

    if ( MinPosSteps < MinTimeSteps ) {
        device_clock_mech._mechControlSteps++;
        GoToTaskAfterStep = MechSetArrowMinTask;
        SetTask(MechMoveStepDown);
    }     
    if ( MinPosSteps >= MinTimeSteps ) { SetTask(MechSetArrowMinOk); return; }
}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowMinOk() {
    device_clock_mech._Mech_Status = STATUS_IDLE;
    ledClearState(LED_PRIO_DEV);
    GoToTaskAfterStep = Idle_task;
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    GetSens();
    SetTask(MechSetArrows); // set all arows
    DEBUGCLOCKMECH("ctrlSteps: %02d, MinReal: %d, stepsPerHour %d\r\n", device_clock_mech._mechControlSteps, device_clock_mech._timeMinReal , device_clock_mech._config.stepsPerRevolution);
}

void CLASS_DEVICE_CLOCKMECH::MechSetArrowMinFail() {
    device_clock_mech._Mech_Status = ERROR_NO_MECH;
    ledMacrosClockMechError();
    GoToTaskAfterStep = Idle_task;
    GetSens(); cmdSens();
    digitalWrite(CLOCKMECH_EN, HIGH);
    digitalWrite(CLOCKMECH_SENS_LED, LOW);
    DEBUGCLOCKMECH("MechSetArrowMinFail\r\n");
}

// ============================================================
// Терминальные команды
// ============================================================

void CLASS_DEVICE_CLOCKMECH::cmdStep() {
    GoToTaskAfterStep = Idle_task;
    SetTask(MechMoveStepDown);
}

void CLASS_DEVICE_CLOCKMECH::cmdDir() {
    uint8_t dir = digitalRead(CLOCKMECH_DIR);
    if (dir == 1) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_ClockWise);}
    if (dir == 0) { digitalWrite(CLOCKMECH_DIR, CLOCKMECH_CounterClockWise);}
    DBG_MOD("[D_CLOCKMECH] ", "Direction: %s\r\n", dir == CLOCKMECH_ClockWise ? "CCW" : "CW");
}

void CLASS_DEVICE_CLOCKMECH::cmdEn() {
    bool en = digitalRead(CLOCKMECH_EN);
    digitalWrite(CLOCKMECH_EN, en == LOW ? HIGH : LOW);
    DBG_MOD("[D_CLOCKMECH] ", "Driver: %s\r\n", en == LOW ? "OFF" : "ON");
}

void CLASS_DEVICE_CLOCKMECH::cmdSled() {
    bool led = digitalRead(CLOCKMECH_SENS_LED);
    digitalWrite(CLOCKMECH_SENS_LED, led == HIGH ? LOW : HIGH);
    DBG_MOD("[D_CLOCKMECH] ", "Sensor LED: %s\r\n", led == HIGH ? "OFF" : "ON");
}

void CLASS_DEVICE_CLOCKMECH::cmdSens() {
    
    DBG_MOD("[D_CLOCKMECH] ", "SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(CLOCKMECH_SENS_HOUR),
        digitalRead(CLOCKMECH_SENS_MIN),
        digitalRead(CLOCKMECH_SENS_LED));
}

void CLASS_DEVICE_CLOCKMECH::MechNCmdStep() {
    if (_nStepCount > 0) {
        _nStepCount--;
        GoToTaskAfterStep = MechNCmdStep; 
        SetTimerTask(MechMoveStepDown, 2);
    } else { GoToTaskAfterStep = Idle_task;  }
}

void CLASS_DEVICE_CLOCKMECH::cmdN() {
    if (_nStepCount > 0) {
        DBG_MOD("[D_CLOCKMECH] ", "Busy: previous clock-n still running\r\n");
        return;
    }
    char *arg = term.getNext();
    if (arg == NULL) {
        DBG_MOD("[D_CLOCKMECH] ", "Usage: clock-n <N>\r\n");
        return;
    }
    _nStepCount = atoi(arg);
    if (_nStepCount == 0) return;
    digitalWrite(CLOCKMECH_EN, LOW);
    GoToTaskAfterStep = MechNCmdStep;
    SetTask(MechMoveStepDown);
}
void CLASS_DEVICE_CLOCKMECH::cmdSet1200()   { SetTask(MechSet1200_Setup); }
void CLASS_DEVICE_CLOCKMECH::cmdSetxx00()   { SetTask(MechSetxx00_Setup); }
void CLASS_DEVICE_CLOCKMECH::cmdSet12xx()   { SetTask(MechSet12xx_Setup); }
void CLASS_DEVICE_CLOCKMECH::cmdCount()     { SetTask(MechCountStepsSetup); }
void CLASS_DEVICE_CLOCKMECH::cmdPoll()      { SetTask(PollTimeTask); }

void CLASS_DEVICE_CLOCKMECH::cmdSave() {    device_clock_mech.saveConfig(); }
void CLASS_DEVICE_CLOCKMECH::cmdMode() {
    char *arg = term.getNext();
    if (arg == NULL) {
        device_clock_mech._config.enable_status = !device_clock_mech._config.enable_status;
    } else {
        String s(arg);
        if (s == "dbg" || s == "debug")       { device_clock_mech._config.enable_status = MODE_DEBUG; }
        else if (s == "work")                 { device_clock_mech._config.enable_status = MODE_WORK; }
        else { DBG_MOD("[D_CLOCKMECH] ", "Usage: c-mode [dbg|work]\r\n"); return; }
    }
    DBG_MOD("[D_CLOCKMECH] ", "Mode: %s\r\n", device_clock_mech._config.enable_status == MODE_WORK ? "WORK" : "DEBUG");
}

void CLASS_DEVICE_CLOCKMECH::cmdSetArrows() {
    char *arg1 = term.getNext();
    if (arg1 == NULL) {
        DBG_MOD("[D_CLOCKMECH] ", "Usage: c-set <hour> [min]\r\n");
        return;
    }
    uint8_t h = (uint8_t)atoi(arg1);
    if (h > 24) {
        DBG_MOD("[D_CLOCKMECH] ", "Error: hour must be 0-24\r\n");
        return;
    }
    char *arg2 = term.getNext();
    uint8_t m = 0;
    if (arg2 != NULL) {
        m = (uint8_t)atoi(arg2);
        if (m > 60) {
            DBG_MOD("[D_CLOCKMECH] ", "Error: min must be 0-60\r\n");
            return;
        }
    }
    device_clock_mech.MechTimeSet(h, m);
    DBG_MOD("[D_CLOCKMECH] ", "Set Arrows to: %02d:%02d\r\n",  device_clock_mech._timeHourReal, device_clock_mech._timeMinReal);
    SetTask(MechSetArrows);
}

void CLASS_DEVICE_CLOCKMECH::cmdStatus() {

    time_t t = device_clock_mech.getCurrentTime();
  
    DBG_MOD("[D_CLOCKMECH] ", "===== ClockMech Status =====\r\n");
    DBG_MOD("[D_CLOCKMECH] ", "_Mech_Status:          %d\r\n", device_clock_mech._Mech_Status);
    DBG_MOD("[D_CLOCKMECH] ", "_config.enable_status: %d\r\n", device_clock_mech._config.enable_status);
    DBG_MOD("[D_CLOCKMECH] ", "_config.timeSource:    %s\r\n", device_clock_mech._config.timeSource.c_str());
    DBG_MOD("[D_CLOCKMECH] ", "_config.stepsPerRevolution: %d\r\n", device_clock_mech._config.stepsPerRevolution);
    DBG_MOD("[D_CLOCKMECH] ", "_config.pollInterval: %d\r\n", device_clock_mech._config.pollInterval);
    DBG_MOD("[D_CLOCKMECH] ", "_config.errorLimitSteps: %d\r\n", device_clock_mech._config.errorLimitSteps);
    DBG_MOD("[D_CLOCKMECH] ", "_config.sensorLedEnabled: %d\r\n", device_clock_mech._config.sensorLedEnabled);
    DBG_MOD("[D_CLOCKMECH] ", "_mechControlSteps: %d\r\n", device_clock_mech._mechControlSteps);
    DBG_MOD("[D_CLOCKMECH] ", "_timeMechHour: %d _timeMechMin: %d\r\n", device_clock_mech._timeMechHour, device_clock_mech._timeMechMin);
    DBG_MOD("[D_CLOCKMECH] ", "_timeHourReal: %d _timeMinReal: %d\r\n", device_clock_mech._timeHourReal,  device_clock_mech._timeMinReal);
    DBG_MOD("[D_CLOCKMECH] ", "_minPrev:          %d\r\n", device_clock_mech._minPrev);
    DBG_MOD("[D_CLOCKMECH] ", "_sensorLedState:   %d\r\n", device_clock_mech._sensorLedState);
    if (t > 0) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%02d:%02d", hour(t), minute(t));
        DBG_MOD("[D_CLOCKMECH] ", "time: %s\r\n", buf);
    }
    DBG_MOD("[D_CLOCKMECH] ", "SENS_HOUR=%d SENS_MIN=%d SENS_LED=%d\r\n",
        digitalRead(CLOCKMECH_SENS_HOUR),
        digitalRead(CLOCKMECH_SENS_MIN),
        digitalRead(CLOCKMECH_SENS_LED));
    DBG_MOD("[D_CLOCKMECH] ", "DIR=%d STEP=%d EN=%d\r\n",
        digitalRead(CLOCKMECH_DIR),
        digitalRead(CLOCKMECH_STEP),
        digitalRead(CLOCKMECH_EN));
    DBG_MOD("[D_CLOCKMECH] ", "=============================\r\n");
}
