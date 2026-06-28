#include "FSWebServerLib.h"

#include "core_json/core_json.h"

#include "device_clock-mech.h"
#include "common.h"
#include "device_clock-mech_version.h"

MODULE_CLASS_CLOCKMECH ModClassClockMech(false);
MODULE_CLASS_CLOCKMECH::MODULE_CLASS_CLOCKMECH(bool _in) { dumb = _in; }

#if defined(ESP32)
void MODULE_CLASS_CLOCKMECH::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void MODULE_CLASS_CLOCKMECH::setFs(FS* fs)
#endif
{
    _fs = fs;
}

void MODULE_CLASS_CLOCKMECH::begin() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    defaultConfigClockMech();
    if (load_config_clockmech() == false) { save_config_clockmech(); }
}

void MODULE_CLASS_CLOCKMECH::webInit() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/clock-mech/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleConfig(request);
    });

    ESPHTTPServer.on("/clock-mech/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/clock-mech/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

void MODULE_CLASS_CLOCKMECH::handleInfo(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "enabled|"       + String(_config.enabled ? "checked" : "")  + "|chk\n";
    values += "triggerHour|"   + String(_config.triggerHour)               + "|input\n";
    values += "triggerMinute|" + String(_config.triggerMinute)             + "|input\n";
    request->send(200, "text/plain", values);
}

void MODULE_CLASS_CLOCKMECH::handleConfig(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);

    _config.enabled = false;

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGCLOCKMECH("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "enabled" && request->arg(i) == "true") {
                _config.enabled = true;
                continue;
            }
            if (request->argName(i) == "triggerHour") {
                _config.triggerHour = (uint16_t)request->arg(i).toInt();
                if (_config.triggerHour > 23) { _config.triggerHour = 23; }
                continue;
            }
            if (request->argName(i) == "triggerMinute") {
                _config.triggerMinute = (uint16_t)request->arg(i).toInt();
                if (_config.triggerMinute > 59) { _config.triggerMinute = 59; }
                continue;
            }
        }

        save_config_clockmech();
        request->send(200, "text/plain", "OK");
    }
}

void MODULE_CLASS_CLOCKMECH::defaultConfigClockMech() {
    _config.enabled      = false;
    _config.triggerHour  = 12;
    _config.triggerMinute = 0;
}

bool MODULE_CLASS_CLOCKMECH::load_config_clockmech() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc) == false) { return false; }

    _config.enabled       = doc["enabled"].as<bool>();
    _config.triggerHour   = doc["triggerHour"].as<uint16_t>();
    _config.triggerMinute = doc["triggerMinute"].as<uint16_t>();

    DEBUGCLOCKMECH("enabled: %d\r\n",       _config.enabled);
    DEBUGCLOCKMECH("triggerHour: %d\r\n",   _config.triggerHour);
    DEBUGCLOCKMECH("triggerMinute: %d\r\n", _config.triggerMinute);

    return true;
}

bool MODULE_CLASS_CLOCKMECH::save_config_clockmech() {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_CLOCKMECH, doc);
    doc["enabled"]       = _config.enabled;
    doc["triggerHour"]   = _config.triggerHour;
    doc["triggerMinute"] = _config.triggerMinute;

    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_CLOCKMECH, doc);
}

String MODULE_CLASS_CLOCKMECH::getVersionStr() {
    return String(DEVICE_CLOCKMECH_VERSION);
}

String MODULE_CLASS_CLOCKMECH::getGeneratedTime() {
    return String(DEVICE_CLOCKMECH_GENERATED_TIME);
}

String MODULE_CLASS_CLOCKMECH::getCommitDateStr() {
    return String(DEVICE_CLOCKMECH_COMMIT_DATE_STR);
}

void MODULE_CLASS_CLOCKMECH::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGCLOCKMECH("%s\r\n", __FUNCTION__);
    String values = "";
    values += "clockmechversion|" + getVersionStr()    + "|dev\n";
    values += "clockmechgentime|" + getGeneratedTime() + "|dev\n";
    values += "clockmechgendate|" + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}
