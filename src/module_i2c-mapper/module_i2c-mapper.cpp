#include "FSWebServerLib.h"

#include "module_i2c-mapper.h"
#include "module_i2c-mapper_version.h"

#include <Wire.h>

CLASS_MODULE_I2C_MAPPER ModClassI2cMapper(false);
CLASS_MODULE_I2C_MAPPER::CLASS_MODULE_I2C_MAPPER(bool _in) { dumb = _in; }

void CLASS_MODULE_I2C_MAPPER::begin() {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);
}

void CLASS_MODULE_I2C_MAPPER::begin(ModContext& ctx) {
    (void)ctx;
    begin();
}

void CLASS_MODULE_I2C_MAPPER::web_Init() {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/i2cmapper/scan", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleScan(request);
    });

    ESPHTTPServer.on("/i2cmapper/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

void CLASS_MODULE_I2C_MAPPER::handleScan(AsyncWebServerRequest *request) {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);

#if defined(ESP32)
    int sda = I2C_MAPPER_SDA;
    int scl = I2C_MAPPER_SCL;
    Wire.begin(sda, scl);
#else
    Wire.begin();
#endif

    String result = "[";

    bool first = true;
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
#if defined(ESP32)
        esp_task_wdt_reset();
#elif defined(ESP8266)
        ESP.wdtFeed();
#endif

        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            if (!first) { result += ","; }
            first = false;
            char buf[8];
            snprintf(buf, sizeof(buf), "\"0x%02X\"", addr);
            result += buf;
        }
    }

    result += "]";
    request->send(200, "application/json", result);
}

String CLASS_MODULE_I2C_MAPPER::getVersionStr() {
    return String(MODULE_I2C_MAPPER_VERSION);
}

String CLASS_MODULE_I2C_MAPPER::getGeneratedTime() {
    return String(MODULE_I2C_MAPPER_GENERATED_TIME);
}

String CLASS_MODULE_I2C_MAPPER::getCommitDateStr() {
    return String(MODULE_I2C_MAPPER_COMMIT_DATE_STR);
}

void CLASS_MODULE_I2C_MAPPER::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);
    String values = "";
    values += "i2cmapperversion|" + getVersionStr()    + "|div\n";
    values += "i2cmappergentime|" + getGeneratedTime() + "|div\n";
    values += "i2cmappergendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
