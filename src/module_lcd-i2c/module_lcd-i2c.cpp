#include "FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"

#include "module_lcd-i2c.h"
#include "common.h"
#include "module_lcd-i2c_version.h"
#include "eertos.h"
#include "version.h"

MODULE_CLASS_LCD_I2C ModClassLcdI2c(false);
MODULE_CLASS_LCD_I2C::MODULE_CLASS_LCD_I2C(bool _in) { dumb = _in; _lcd = NULL; }

#if defined(ESP32)
void MODULE_CLASS_LCD_I2C::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void MODULE_CLASS_LCD_I2C::setFs(FS* fs)
#endif
{
    _fs = fs;
}

void MODULE_CLASS_LCD_I2C::begin() {
    DEBUGLCD("%s\r\n", __FUNCTION__);

    defaultConfigLcd();
    bool cfgLoaded = load_config();

    if (!cfgLoaded && _displayLines[0].isEmpty()) {
        _config.cols = 8;
        _config.rows = 1;
        _displayLines[0] = String(BUILD_ENV);
    }

    _initDisplay();

    SetTimerTask(_lcdUpdateTask, 1000);
}

void MODULE_CLASS_LCD_I2C::_initDisplay() {
    DEBUGLCD("%s: addr=0x%02X, cols=%d, rows=%d, bl=%d\r\n",
             __FUNCTION__, _config.i2cAddr, _config.cols, _config.rows, _config.backlight);

    Wire.begin(LCD_I2C_SDA, LCD_I2C_SCL);

    if (_lcd) { _lcd->clear(); _lcd->noBacklight(); _lcd = NULL; }

    _lcd = new LiquidCrystal_I2C(_config.i2cAddr, _config.cols, _config.rows);
    _lcd->init();
    if (_config.backlight) { _lcd->backlight(); } else { _lcd->noBacklight(); }
    _lcd->clear();

    _applyLines();
}

void MODULE_CLASS_LCD_I2C::_applyLines() {
    if (!_lcd) { return; }

    bool ntpSynced = (NTP.getLastNTPSync() > 0);

    for (uint8_t r = 0; r < _config.rows && r < LCD_I2C_MAX_ROWS; r++) {
        String line = _displayLines[r];
        String out;

        if (line == "date") {
            if (ntpSynced) {
                String dt = NTP.getTimeDateString();
                int spaceIdx = dt.indexOf(' ');
                if (spaceIdx > 0) {
                    String d = dt.substring(0, spaceIdx);
                    if (d.length() >= 10) {
                        out = d.substring(0, 10);
                    } else {
                        out = d;
                    }
                } else {
                    out = "--.--.----";
                }
            } else {
                out = "--.--.----";
            }
        } else if (line == "time") {
            if (ntpSynced) {
                String dt = NTP.getTimeDateString();
                int spaceIdx = dt.indexOf(' ');
                if (spaceIdx > 0 && (int)dt.length() > spaceIdx + 8) {
                    out = dt.substring(spaceIdx + 1, spaceIdx + 9);
                } else {
                    out = "--:--:--";
                }
            } else {
                out = "--:--:--";
            }
        } else {
            out = line;
        }

        if (out.length() > _config.cols) { out = out.substring(0, _config.cols); }
        _lcd->setCursor(0, r);
        _lcd->print(out);
        uint8_t remaining = _config.cols - out.length();
        if (remaining > 0) {
            for (uint8_t i = 0; i < remaining; i++) { _lcd->print(' '); }
        }
    }
}

void MODULE_CLASS_LCD_I2C::_lcdUpdateTask() {
    ModClassLcdI2c._applyLines();
    SetTimerTask(_lcdUpdateTask, 1000);
}

void MODULE_CLASS_LCD_I2C::webInit() {
    DEBUGLCD("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/lcd-i2c/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/lcd-i2c/save_content", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSaveContent(request);
    });

    ESPHTTPServer.on("/lcd-i2c/save_config", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSaveConfig(request);
    });

    ESPHTTPServer.on("/lcd-i2c/sysinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSysInfo(request);
    });

    ESPHTTPServer.on("/lcd-i2c/backlight", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        if (request->hasArg("backlight")) {
            _config.backlight = (request->arg("backlight") == "true");
            save_config();
            if (_lcd) {
                if (_config.backlight) { _lcd->backlight(); } else { _lcd->noBacklight(); }
            }
        }
        request->send(200, "text/plain", "OK");
    });

    ESPHTTPServer.on("/lcd-i2c/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

void MODULE_CLASS_LCD_I2C::handleInfo(AsyncWebServerRequest *request) {
    DEBUGLCD("%s\r\n", __FUNCTION__);
    String values = "";
    values += "trgt|"   + String(BUILD_ENV)                              + "|div\n";
    values += "verfw|"  + String(FIRMWARE_VERSION)                       + "|div\n";
    values += "verfs|"  + ESPHTTPServer.getFsVersionStr()                + "|div\n";
    values += "verm|"   + getVersionStr()                                + "|div\n";
    values += "uptime|" + NTP.getUptimeString()                          + "|div\n";
    values += "i2cAddr|" + String(_config.i2cAddr, HEX)                  + "|input\n";
    values += "cols|"   + String(_config.cols)                           + "|input\n";
    values += "rows|"   + String(_config.rows)                           + "|input\n";
    values += "backlight|" + String(_config.backlight ? "checked" : "")  + "|chk\n";

    for (uint8_t r = 0; r < LCD_I2C_MAX_ROWS; r++) {
        String id = "line" + String(r);
        values += id + "|" + _displayLines[r] + "|input\n";
    }

    request->send(200, "text/plain", values);
}

void MODULE_CLASS_LCD_I2C::handleSysInfo(AsyncWebServerRequest *request) {
    DEBUGLCD("%s\r\n", __FUNCTION__);
    String values = "";
    values += "trgt|"   + String(BUILD_ENV)        + "|div\n";
    values += "verfw|"  + String(FIRMWARE_VERSION) + "|div\n";
    values += "verfs|"  + ESPHTTPServer.getFsVersionStr() + "|div\n";
    values += "verm|"   + getVersionStr()          + "|div\n";
    values += "uptime|" + NTP.getUptimeString()    + "|div\n";
    request->send(200, "text/plain", values);
}

void MODULE_CLASS_LCD_I2C::handleSaveContent(AsyncWebServerRequest *request) {
    DEBUGLCD("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGLCD("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            for (uint8_t r = 0; r < LCD_I2C_MAX_ROWS; r++) {
                String id = "line" + String(r);
                if (request->argName(i) == id) {
                    _displayLines[r] = urldecode(request->arg(i));
                    break;
                }
            }
        }

        save_config();
        _applyLines();
        request->send(200, "text/plain", "OK");
    }
}

void MODULE_CLASS_LCD_I2C::handleSaveConfig(AsyncWebServerRequest *request) {
    DEBUGLCD("%s\r\n", __FUNCTION__);

    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            DEBUGLCD("Arg %d: %s %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());

            if (request->argName(i) == "i2cAddr") {
                _config.i2cAddr = (uint8_t)strtol(request->arg(i).c_str(), NULL, 16);
                continue;
            }
            if (request->argName(i) == "cols") {
                _config.cols = (uint8_t)request->arg(i).toInt();
                continue;
            }
            if (request->argName(i) == "rows") {
                uint8_t newRows = (uint8_t)request->arg(i).toInt();
                if (newRows > LCD_I2C_MAX_ROWS) { newRows = LCD_I2C_MAX_ROWS; }
                _config.rows = newRows;
                continue;
            }
            if (request->argName(i) == "backlight") {
                _config.backlight = (request->arg(i) == "true");
                continue;
            }
        }

        save_config();
        _initDisplay();
        request->send(200, "text/plain", "OK");
    }
}

void MODULE_CLASS_LCD_I2C::defaultConfigLcd() {
    _config.i2cAddr    = 0x27;
    _config.cols       = 8;
    _config.rows       = 1;
    _config.backlight  = true;

    for (uint8_t r = 0; r < LCD_I2C_MAX_ROWS; r++) {
        _displayLines[r] = "";
    }
}

bool MODULE_CLASS_LCD_I2C::load_config() {
    DEBUGLCD("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_LCD_I2C, doc) == false) { return false; }

    _config.i2cAddr   = doc["i2cAddr"].as<uint8_t>();
    _config.cols      = doc["cols"].as<uint8_t>();
    _config.rows      = doc["rows"].as<uint8_t>();
    _config.backlight = doc["backlight"].as<bool>();

    if (_config.rows > LCD_I2C_MAX_ROWS) { _config.rows = LCD_I2C_MAX_ROWS; }

    if (doc["display_lines"].is<JsonArray>()) {
        JsonArray arr = doc["display_lines"].as<JsonArray>();
        for (uint8_t r = 0; r < LCD_I2C_MAX_ROWS; r++) {
            if (r < arr.size()) {
                _displayLines[r] = arr[r].as<String>();
            } else {
                _displayLines[r] = "";
            }
        }
    }

    DEBUGLCD("i2cAddr: 0x%02X, cols: %d, rows: %d, backlight: %d\r\n",
             _config.i2cAddr, _config.cols, _config.rows, _config.backlight);

    return true;
}

bool MODULE_CLASS_LCD_I2C::save_config() {
    DEBUGLCD("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_LCD_I2C, doc);
    doc["i2cAddr"]   = _config.i2cAddr;
    doc["cols"]      = _config.cols;
    doc["rows"]      = _config.rows;
    doc["backlight"] = _config.backlight;

    JsonArray arr = doc["display_lines"].to<JsonArray>();
    arr.clear();
    for (uint8_t r = 0; r < LCD_I2C_MAX_ROWS; r++) {
        arr.add(_displayLines[r]);
    }

    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_LCD_I2C, doc);
}

String MODULE_CLASS_LCD_I2C::getVersionStr() {
    return String(MODULE_LCD_I2C_VERSION);
}

String MODULE_CLASS_LCD_I2C::getGeneratedTime() {
    return String(MODULE_LCD_I2C_GENERATED_TIME);
}

String MODULE_CLASS_LCD_I2C::getCommitDateStr() {
    return String(MODULE_LCD_I2C_COMMIT_DATE_STR);
}

void MODULE_CLASS_LCD_I2C::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGLCD("%s\r\n", __FUNCTION__);
    String values = "";
    values += "lcdi2cversion|" + getVersionStr()    + "|div\n";
    values += "lcdi2cgentime|" + getGeneratedTime() + "|div\n";
    values += "lcdi2cgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
