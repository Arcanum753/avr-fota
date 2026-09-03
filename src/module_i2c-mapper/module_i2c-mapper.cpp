#include "core_web/FSWebServerLib.h"

#include "module_i2c-mapper.h"
#include "module_i2c-mapper_version.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#include <Wire.h>

CLASS_MODULE_I2C_MAPPER module_i2c_mapper(false);
CLASS_MODULE_I2C_MAPPER::CLASS_MODULE_I2C_MAPPER(bool _in) { dumb = _in; }

// ============================================================
// begin()
// ============================================================
void CLASS_MODULE_I2C_MAPPER::begin() {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);

    TerminalRegisterModule(i2cMapperTerminalRegister);
}

void CLASS_MODULE_I2C_MAPPER::begin(ModContext& ctx) {
    (void)ctx;
    begin();
}

// ============================================================
// web_Init()
// ============================================================
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

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_MODULE_I2C_MAPPER::handleScan(AsyncWebServerRequest *request) {
    DEBUGI2CMAPPER("%s\r\n", __FUNCTION__);

    uint8_t found[126];
    uint8_t count = scanBus(found, sizeof(found));

    String result = "[";
    bool first = true;
    for (uint8_t i = 0; i < count; i++) {
        if (!first) { result += ","; }
        first = false;
        char buf[8];
        snprintf(buf, sizeof(buf), "\"0x%02X\"", found[i]);
        result += buf;
    }
    result += "]";
    request->send(200, "application/json", result);
}

// ============================================================
// Версионные методы
// ============================================================
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

// ============================================================
// Конкретная логика модуля
// ============================================================
uint8_t CLASS_MODULE_I2C_MAPPER::scanBus(uint8_t *found, uint8_t maxCount) {
#if defined(ESP32)
    int sda = I2C_MAPPER_SDA;
    int scl = I2C_MAPPER_SCL;
    Wire.begin(sda, scl);
#else
    Wire.begin();
#endif

    uint8_t count = 0;
    for (uint8_t addr = 0x01; addr < 0x7F && count < maxCount; addr++) {
#if defined(ESP32)
        esp_task_wdt_reset();
#elif defined(ESP8266)
        ESP.wdtFeed();
#endif

        Wire.beginTransmission(addr);
        uint8_t error = Wire.endTransmission();

        if (error == 0) {
            if (count < maxCount) { found[count++] = addr; }
        }
    }
    return count;
}

void i2cMapperCmdScan() {
    uint8_t found[126];
    uint8_t count = module_i2c_mapper.scanBus(found, sizeof(found));

    Serial.printf("\r\nI2C Bus Map (0x01-0x7E)\r\n");
    Serial.printf("    0 1 2 3 4 5 6 7 8 9 a b c d e f\r\n");
    for (uint8_t r = 0; r < 8; r++) {
        Serial.printf("%02X: ", r * 0x10);
        for (uint8_t c = 0; c < 16; c++) {
            uint8_t addr = (r << 4) | c;
            bool foundAddr = false;
            for (uint8_t i = 0; i < count; i++) {
                if (found[i] == addr) { foundAddr = true; break; }
            }
            if (foundAddr) {
                Serial.printf("%02X ", addr);      // полный адрес на перекрестье строки и столбца
            } else {
                Serial.printf("-- ");
            }
        }
        Serial.printf("\r\n");
    }

    if (count == 0) {
        Serial.printf("No devices found.\r\n");
    } else {
        Serial.printf("Found %d device(s):", count);
        for (uint8_t i = 0; i < count; i++) { Serial.printf(" 0x%02X", found[i]); }
        Serial.printf("\r\n");
    }
}

void i2cMapperTerminalRegister() {
    term.addCommand("i2c-scan", i2cMapperCmdScan);
}
