#include "FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"

#include "module_template.h"
#include "common.h"
#include "module_template_version.h"
#include "eertos.h"

CLASS_MODULE_TEMPLATE ModClassTemplate(false);
CLASS_MODULE_TEMPLATE::CLASS_MODULE_TEMPLATE(bool _in) { dumb = _in; }

#if defined(ESP32)
void CLASS_MODULE_TEMPLATE::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void CLASS_MODULE_TEMPLATE::setFs(FS* fs)
#endif
{
    _fs = fs;
}

void CLASS_MODULE_TEMPLATE::begin() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    // Инициализация пинов
    pinMode(TEMPLATE_GPIO1, OUTPUT);
    pinMode(TEMPLATE_GPIO2, OUTPUT);

    // Загрузка или создание конфига по умолчанию
    defaultConfigTemplate();
    if (load_config_template() == false) { save_config_template(); }

    _blinkState = false;

    if (_config.blinkInterval > 0) { SetTimerTask(blinkTimerTask, _config.blinkInterval); }
}

void CLASS_MODULE_TEMPLATE::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

void CLASS_MODULE_TEMPLATE::applyGpioState() {

    digitalWrite(TEMPLATE_GPIO1, _config.gpio1State ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2, _config.gpio2State ? HIGH : LOW);
}

void CLASS_MODULE_TEMPLATE::blinkTimerTask() {
    if (ModClassTemplate._config.blinkInterval == 0) {
        ModClassTemplate.applyGpioState();
        return;
    }

    ModClassTemplate._blinkState = !ModClassTemplate._blinkState;

    digitalWrite(TEMPLATE_GPIO1,
    (ModClassTemplate._blinkState && ModClassTemplate._config.gpio1State) ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2,
    (ModClassTemplate._blinkState && ModClassTemplate._config.gpio2State) ? HIGH : LOW);
    SetTimerTask(blinkTimerTask, ModClassTemplate._config.blinkInterval);
}

void CLASS_MODULE_TEMPLATE::web_Init() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    // AJAX — сохранение GPIO конфига (POST /template/save)
    ESPHTTPServer.on("/template/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleConfigGpio(request);
    });

    // AJAX — сохранение демо-конфига (POST /template/save_demo)
    ESPHTTPServer.on("/template/save_demo", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleConfigDemo(request);
    });

    // AJAX — получение всех значений конфига + время
    ESPHTTPServer.on("/template/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    // AJAX — только время и дата (опрос раз в секунду)
    ESPHTTPServer.on("/template/time", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleTime(request);
    });

    // Версия модуля
    ESPHTTPServer.on("/template/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ========== ВЕБ-ОБРАБОТЧИКИ ==========
void CLASS_MODULE_TEMPLATE::handleInfo(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    String values = "";
    values += "gpio1State|"      + String(_config.gpio1State ? "checked" : "")      + "|chk\n";
    values += "gpio2State|"      + String(_config.gpio2State ? "checked" : "")      + "|chk\n";
    values += "blinkInterval|"   + String(_config.blinkInterval)                    + "|input\n";
    values += "demoSampleText|"  + _config.demoSampleText                           + "|input\n";

    for (uint8_t i = 0; i < 3; i++) {
        String id = "demoArray" + String(i);
        values += id + "|" + _config.demoArray[i] + "|input\n";
    }

    // Время и дата
    String timeDate = "NTP not synced";
    if (NTP.getLastNTPSync() > 0) { timeDate = NTP.getTimeDateString(); }
    values += "templateTime|"    + timeDate + "|div\n";
    request->send(200, "text/plain", values);
}

// Отдаёт только время и дату — для每秒ного опроса без перезаписи формы
void CLASS_MODULE_TEMPLATE::handleTime(AsyncWebServerRequest *request) {
    String timeDate = "NTP not synced";
    if (NTP.getLastNTPSync() > 0) { timeDate = NTP.getTimeDateString(); }
    String values = "";
    values += "templateTime|" + timeDate + "|div\n";
    request->send(200, "text/plain", values);
}

void CLASS_MODULE_TEMPLATE::handleConfigGpio(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    _config.gpio1State = false;
    _config.gpio2State = false;

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "gpio1State" && request->arg(i) == "true") {
                _config.gpio1State = true;
                continue;
            }
            if (request->argName(i) == "gpio2State" && request->arg(i) == "true") {
                _config.gpio2State = true;
                continue;
            }
            if (request->argName(i) == "blinkInterval") {
                uint16_t newInterval = (uint16_t)request->arg(i).toInt();
                if (newInterval > 0) {
                    DelTimerTask(blinkTimerTask);
                    _config.blinkInterval = newInterval;
                    _blinkState = false;
                    SetTimerTask(blinkTimerTask, _config.blinkInterval);
                } else {
                    DelTimerTask(blinkTimerTask);
                    _config.blinkInterval = 0;
                    applyGpioState();
                }
                continue;
            }
        }

        applyGpioState();

        save_config_template();
        request->send(200, "text/plain", "OK");
    }
}

void CLASS_MODULE_TEMPLATE::handleConfigDemo(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "demoSampleText") {
                _config.demoSampleText = urldecode(request->arg(i));
                continue;
            }

            for (uint8_t j = 0; j < 3; j++) {
                String id = "demoArray" + String(j);
                if (request->argName(i) == id) {
                    _config.demoArray[j] = urldecode(request->arg(i));
                    break;
                }
            }
        }

        save_config_template();
        request->send(200, "text/plain", "OK");
    }
}

// ========== РАБОТА С КОНФИГОМ ==========

void CLASS_MODULE_TEMPLATE::defaultConfigTemplate() {
    _config.gpio1State     = false;
    _config.gpio2State     = false;
    _config.blinkInterval  = 0;
    _config.demoSampleText = "demo text";

    for (uint8_t i = 0; i < 3; i++) { _config.demoArray[i] = ""; }
}

bool CLASS_MODULE_TEMPLATE::load_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc) == false) { return false; }

    _config.gpio1State     = doc["gpio1State"].as<bool>();
    _config.gpio2State     = doc["gpio2State"].as<bool>();
    _config.blinkInterval  = doc["blinkInterval"].as<uint16_t>();
    _config.demoSampleText = doc["demoSampleText"].as<String>();

    if (doc["demoArray"].is<JsonArray>()) {
        JsonArray arr = doc["demoArray"].as<JsonArray>();
        for (uint8_t i = 0; i < 3; i++) {
            if (i < arr.size()) {
                _config.demoArray[i] = arr[i].as<String>();
            } else {
                _config.demoArray[i] = "";
            }
        }
    }

    DEBUGTEMPLATE("gpio1State: %d\r\n",     _config.gpio1State);
    DEBUGTEMPLATE("gpio2State: %d\r\n",     _config.gpio2State);
    DEBUGTEMPLATE("blinkInterval: %d\r\n",  _config.blinkInterval);
    DEBUGTEMPLATE("demoSampleText: %s\r\n", _config.demoSampleText.c_str());

    return true;
}

bool CLASS_MODULE_TEMPLATE::save_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc);
    doc["gpio1State"]     = _config.gpio1State;
    doc["gpio2State"]     = _config.gpio2State;
    doc["blinkInterval"]  = _config.blinkInterval;
    doc["demoSampleText"] = _config.demoSampleText;

    JsonArray arr = doc["demoArray"].to<JsonArray>();
    arr.clear();
    for (uint8_t i = 0; i < 3; i++) {
        arr.add(_config.demoArray[i]);
    }

    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_TEMPLATE, doc);
}

// ========== ВЕРСИОННЫЕ МЕТОДЫ ==========

String CLASS_MODULE_TEMPLATE::getVersionStr() {
    return String(MODULE_TEMPLATE_VERSION);
}

String CLASS_MODULE_TEMPLATE::getGeneratedTime() {
    return String(MODULE_TEMPLATE_GENERATED_TIME);
}

String CLASS_MODULE_TEMPLATE::getCommitDateStr() {
    return String(MODULE_TEMPLATE_COMMIT_DATE_STR);
}

void CLASS_MODULE_TEMPLATE::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    String values = "";
    values += "templateversion|" + getVersionStr()    + "|div\n";
    values += "templategentime|" + getGeneratedTime() + "|div\n";
    values += "templategendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
