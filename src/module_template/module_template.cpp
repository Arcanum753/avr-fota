#include "FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"

#include "module_template.h"
#include "common.h"
#include "module_template_version.h"
#include "eertos.h"

MODULE_CLASS_TEMPLATE ModClassTemplate(false);
MODULE_CLASS_TEMPLATE::MODULE_CLASS_TEMPLATE(bool _in) { dumb = _in; }

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

    // Инициализация пинов
    pinMode(TEMPLATE_GPIO1, OUTPUT);
    pinMode(TEMPLATE_GPIO2, OUTPUT);

    // Загрузка или создание конфига по умолчанию
    defaultConfigTemplate();
    if (load_config_template() == false) { save_config_template(); }

    // Применяем состояние GPIO из конфига
    // applyGpioState();
    _blinkState = false;

    // Запускаем задачу моргания, если интервал задан
    if (_config.blinkInterval > 0) { SetTimerTask(blinkTimerTask, _config.blinkInterval); }
    SetTimerTask(blinkTimerTask, 100); 
}

void MODULE_CLASS_TEMPLATE::applyGpioState() {

    digitalWrite(TEMPLATE_GPIO1, _config.gpio1State ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2, _config.gpio2State ? HIGH : LOW);
}

void MODULE_CLASS_TEMPLATE::blinkTimerTask() {
    if (ModClassTemplate._config.blinkInterval == 0) {
        ModClassTemplate.applyGpioState();
        return;
    }

    ModClassTemplate._blinkState = !ModClassTemplate._blinkState;

    digitalWrite(TEMPLATE_GPIO1,
    (ModClassTemplate._blinkState && ModClassTemplate._config.gpio1State) ? HIGH : LOW);
    digitalWrite(TEMPLATE_GPIO2,
    (ModClassTemplate._blinkState && ModClassTemplate._config.gpio2State) ? HIGH : LOW);
    SetTimerTask(blinkTimerTask, 500);
}

void MODULE_CLASS_TEMPLATE::webInit() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    // Приём формы с template.html (настройки GPIO)
    ESPHTTPServer.on(HTML_FILE_TEMPLATE, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleConfigGpio(request);
    });

    // Приём формы с template2.html (демо-форма)
    ESPHTTPServer.on(HTML_FILE_TEMPLATE2, HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleConfigDemo(request);
    });

    // AJAX — получение всех значений конфига + время
    ESPHTTPServer.on("/template/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    // Версия модуля
    ESPHTTPServer.on("/template/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ========== ВЕБ-ОБРАБОТЧИКИ ==========
void MODULE_CLASS_TEMPLATE::handleInfo(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    String values = "";
    values += "gpio1State|"      + String(_config.gpio1State ? "checked" : "")      + "|chk\n";
    values += "gpio2State|"      + String(_config.gpio2State ? "checked" : "")      + "|chk\n";
    values += "blinkInterval|"   + String(_config.blinkInterval)                    + "|input\n";
    values += "demoSampleText|"  + _config.demoSampleText                           + "|input\n";

    // Время и дата
    String timeDate = "NTP not synced";
    if (NTP.getLastNTPSync() > 0) { timeDate = NTP.getTimeDateString(); }
    values += "templateTime|"    + timeDate + "|div\n";
    request->send(200, "text/plain", values);
}

void MODULE_CLASS_TEMPLATE::handleConfigGpio(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    // Сбрасываем чекбоксы (они приходят только когда отмечены)
    _config.gpio1State = false;
    _config.gpio2State = false;

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "gpio1State") {
                _config.gpio1State = true;
                continue;
            }
            if (request->argName(i) == "gpio2State") {
                _config.gpio2State = true;
                continue;
            }
            if (request->argName(i) == "blinkInterval") {
                uint16_t newInterval = (uint16_t)request->arg(i).toInt();
                if (_config.blinkInterval == 0 && newInterval > 0) {
                    // Моргание только что включили — запускаем таймер
                    _config.blinkInterval = newInterval;
                    _blinkState = false;
                    SetTimerTask(blinkTimerTask, _config.blinkInterval);
                } else if (_config.blinkInterval > 0 && newInterval == 0) {
                    // Моргание выключили — удаляем таймер
                    DelTimerTask(blinkTimerTask);
                    _config.blinkInterval = 0;
                    applyGpioState();
                } else 
                if (newInterval > 0) {
                    // Меняем период — старый таймер сам перезапустится с новым интервалом
                    _config.blinkInterval = newInterval;
                }
                continue;
            }
        }

        // Применяем состояние GPIO
        applyGpioState();

        save_config_template();
        request->send_P(200, "text/html", Page_GeneralSysTemplate1);
    } else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

void MODULE_CLASS_TEMPLATE::handleConfigDemo(AsyncWebServerRequest *request) {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGTEMPLATE("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "demoSampleText") {
                _config.demoSampleText = urldecode(request->arg(i));
                continue;
            }
        }

        save_config_template();
        request->send_P(200, "text/html", Page_GeneralSysTemplate2);
    } else {
        ESPHTTPServer.handleFileRead(request->url(), request);
    }
}

// ========== РАБОТА С КОНФИГОМ ==========

void MODULE_CLASS_TEMPLATE::defaultConfigTemplate() {
    _config.gpio1State     = false;
    _config.gpio2State     = false;
    _config.blinkInterval  = 0;
    _config.demoSampleText = "demo text";
}

bool MODULE_CLASS_TEMPLATE::load_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc) == false) { return false; }

    _config.gpio1State     = doc["gpio1State"].as<bool>();
    _config.gpio2State     = doc["gpio2State"].as<bool>();
    _config.blinkInterval  = doc["blinkInterval"].as<uint16_t>();
    _config.demoSampleText = doc["demoSampleText"].as<String>();

    DEBUGTEMPLATE("gpio1State: %d\r\n",     _config.gpio1State);
    DEBUGTEMPLATE("gpio2State: %d\r\n",     _config.gpio2State);
    DEBUGTEMPLATE("blinkInterval: %d\r\n",  _config.blinkInterval);
    DEBUGTEMPLATE("demoSampleText: %s\r\n", _config.demoSampleText.c_str());

    return true;
}

bool MODULE_CLASS_TEMPLATE::save_config_template() {
    DEBUGTEMPLATE("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_TEMPLATE, doc);
    doc["gpio1State"]     = _config.gpio1State;
    doc["gpio2State"]     = _config.gpio2State;
    doc["blinkInterval"]  = _config.blinkInterval;
    doc["demoSampleText"] = _config.demoSampleText;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_TEMPLATE, doc);
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
