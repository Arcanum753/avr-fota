#include "FSWebServerLib.h"
#include "module_template.h"
#include "common.h"
#include "core_ntp/NtpClientLib.h"
#include "module_template_version.h"

MODULE_CLASS_TEMPLATE ModClassTemplate(false);

MODULE_CLASS_TEMPLATE::MODULE_CLASS_TEMPLATE(bool _in) {
    dumb = _in;
}

#if defined(ESP32)
void MODULE_CLASS_TEMPLATE::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void MODULE_CLASS_TEMPLATE::setFs(FS* fs)
#endif
{
    _fs = fs;
}

void MODULE_CLASS_TEMPLATE::begin() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    defaultConfigTemplate();
    if (load_config_template() == false) {
        save_config_template();
    }
    // Инициализация GPIO пина при старте
    if (_config.gpioPin < 16) {
        pinMode(_config.gpioPin, OUTPUT);
        digitalWrite(_config.gpioPin, LOW);
    }
}

void MODULE_CLASS_TEMPLATE::webInit() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    // Приём формы с template.html (базовые настройки)
    ESPHTTPServer.on(HTML_FILE_TEMPLATE, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleTemplate1Config(request);
    });

    // Приём формы с template2.html (дополнительные настройки)
    ESPHTTPServer.on(HTML_FILE_TEMPLATE2, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleTemplate2Config(request);
    });

    // AJAX — получение всех значений конфига для заполнения форм
    ESPHTTPServer.on("/template/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleTemplateInfo(request);
    });

    // AJAX — управление GPIO
    ESPHTTPServer.on("/template/gpio", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleGpio(request);
    });

    // Версия модуля
    ESPHTTPServer.on("/template/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ========== ВЕБ-ОБРАБОТЧИКИ ==========

void MODULE_CLASS_TEMPLATE::handleTemplateInfo(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    String values = "";
    values += "textField|"       + _config.textField                          + "|input\n";
    values += "interval|"        + String(_config.interval)                   + "|input\n";
    values += "gpioPin|"         + String(_config.gpioPin)                    + "|input\n";
    values += "baudRate|"        + String(_config.baudRate)                   + "|input\n";
    values += "enableLogging|"   + String(_config.enableLogging ? "checked" : "") + "|chk\n";
    request->send(200, "text/plain", values);
}

void MODULE_CLASS_TEMPLATE::handleTemplate1Config(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
            if (request->argName(i) == "textField") {
                _config.textField = urldecode(request->arg(i));
                continue;
            }
            if (request->argName(i) == "interval") {
                _config.interval = request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "gpioPin") {
                // Сбрасываем старый пин
                uint8_t oldPin = _config.gpioPin;
                _config.gpioPin = (uint8_t)request->arg(i).toInt();
                // Инициализируем новый пин, если он изменился
                if (_config.gpioPin != oldPin && _config.gpioPin < 16) {
                    pinMode(_config.gpioPin, OUTPUT);
                    digitalWrite(_config.gpioPin, LOW);
                }
                continue;
            }
        }
        request->send_P(200, "text/html", Page_GeneralSys);
        save_config_template();
    } else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

void MODULE_CLASS_TEMPLATE::handleTemplate2Config(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    _config.enableLogging = false;
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
            if (request->argName(i) == "baudRate") {
                _config.baudRate = (uint32_t)request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "enableLogging") {
                _config.enableLogging = true;
                continue;
            }
        }
        request->send_P(200, "text/html", Page_GeneralSys);
        save_config_template();
    } else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

void MODULE_CLASS_TEMPLATE::handleGpio(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    if (request->hasArg("state")) {
        String state = request->arg("state");
        DEBUGTEMPLATE("GPIO state: %s\r\n", state.c_str());
        if (_config.gpioPin < 16) {
            if (state == "1") {
                digitalWrite(_config.gpioPin, HIGH);
                request->send(200, "text/plain", "ON");
            } else if (state == "0") {
                digitalWrite(_config.gpioPin, LOW);
                request->send(200, "text/plain", "OFF");
            } else {
                request->send(400, "text/plain", "Bad state");
            }
        } else {
            request->send(400, "text/plain", "Invalid GPIO pin");
        }
    } else {
        request->send(400, "text/plain", "Missing state");
    }
}

// ========== РАБОТА С КОНФИГОМ ==========

void MODULE_CLASS_TEMPLATE::defaultConfigTemplate() {
    _config.textField     = "hello";
    _config.interval      = 5;
    _config.gpioPin       = 2;
    _config.baudRate      = 115200;
    _config.enableLogging = true;
}

bool MODULE_CLASS_TEMPLATE::load_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (!ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc)) return false;
    _config.textField     = doc["textField"].as<String>();
    _config.interval      = doc["interval"].as<uint16_t>();
    _config.gpioPin       = doc["gpioPin"].as<uint8_t>();
    _config.baudRate      = doc["baudRate"].as<uint32_t>();
    _config.enableLogging = doc["enableLogging"].as<bool>();

    DEBUGTEMPLATE("textField: %s\r\n",     _config.textField.c_str());
    DEBUGTEMPLATE("interval: %d\r\n",      _config.interval);
    DEBUGTEMPLATE("gpioPin: %d\r\n",       _config.gpioPin);
    DEBUGTEMPLATE("baudRate: %lu\r\n",     _config.baudRate);
    DEBUGTEMPLATE("enableLogging: %d\r\n", _config.enableLogging);

    return true;
}

bool MODULE_CLASS_TEMPLATE::save_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc);
    doc["textField"]     = _config.textField;
    doc["interval"]      = _config.interval;
    doc["gpioPin"]       = _config.gpioPin;
    doc["baudRate"]      = _config.baudRate;
    doc["enableLogging"] = _config.enableLogging;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_TEMPLATE, doc);
}

String MODULE_CLASS_TEMPLATE::getTimeStr() {
    String timeStr = NTP.getTimeStr();
    if (timeStr.length() == 0) {
        return "NTP not synced";
    }
    return timeStr;
}

// ========== ВЕРСИОННЫЕ МЕТОДЫ ==========

String MODULE_CLASS_TEMPLATE::getVersionStr() {
    return String(MODULE_TEMPLATE_VERSION);
}

String MODULE_CLASS_TEMPLATE::getGeneratedTime() {
    return String(MODULE_TEMPLATE_GENERATED_TIME);
}

String MODULE_CLASS_TEMPLATE::getCommitDateStr() {
    return String(MODULE_TEMPLATE_COMMIT_DATE_STR);
}

void MODULE_CLASS_TEMPLATE::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    String values = "";
    values += "templateversion|" + getVersionStr()    + "|dev\n";
    values += "templategentime|" + getGeneratedTime() + "|dev\n";
    values += "templategendate|" + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}
