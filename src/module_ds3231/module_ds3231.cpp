#include "FSWebServerLib.h"

#include "module_ds3231.h"
#include "module_ds3231_version.h"
#include "common.h"
#include "eertos.h"

#include "core_json/core_json.h"
#include "core_ntp/NtpClientLib.h"

#include <Wire.h>
#include <TimeLib.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#endif

MODULE_CLASS_DS3231 ModClassDs3231(false);
MODULE_CLASS_DS3231::MODULE_CLASS_DS3231(bool _in) { dumb = _in; _lastError = 0; _wireStarted = false; }

#if defined(ESP32)
void MODULE_CLASS_DS3231::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void MODULE_CLASS_DS3231::setFs(FS* fs)
#endif
{
    _fs = fs;
}

// ====================================================================
// BCD — статические вспомогательные функции
// ====================================================================

static uint8_t _dec2bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

static uint8_t _bcd2dec(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

// ====================================================================
// I2C — низкоуровневые операции
// ====================================================================

uint8_t MODULE_CLASS_DS3231::_readReg(uint8_t reg) {
    if (_config.addr == DS3231_ADDR_NONE) { _lastError = -1; return 0; }
    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }
    Wire.beginTransmission(_config.addr);
    Wire.write(reg);
    _lastError = Wire.endTransmission();
    if (_lastError != 0) { return 0; }
    Wire.requestFrom((int)_config.addr, 1);
    if (Wire.available()) { return Wire.read(); }
    _lastError = -2;
    return 0;
}

bool MODULE_CLASS_DS3231::_writeReg(uint8_t reg, uint8_t val) {
    if (_config.addr == DS3231_ADDR_NONE) { _lastError = -1; return false; }
    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }
    Wire.beginTransmission(_config.addr);
    Wire.write(reg);
    Wire.write(val);
    _lastError = Wire.endTransmission();
    return (_lastError == 0);
}

bool MODULE_CLASS_DS3231::_readBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
    if (_config.addr == DS3231_ADDR_NONE) { _lastError = -1; return false; }
    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }
    Wire.beginTransmission(_config.addr);
    Wire.write(reg);
    _lastError = Wire.endTransmission();
    if (_lastError != 0) { return false; }
    Wire.requestFrom((int)_config.addr, (int)len);
    for (uint8_t i = 0; i < len; i++) {
        if (Wire.available()) { buf[i] = Wire.read(); }
        else { _lastError = -2; return false; }
    }
    return true;
}

bool MODULE_CLASS_DS3231::_writeBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
    if (_config.addr == DS3231_ADDR_NONE) { _lastError = -1; return false; }
    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }
    Wire.beginTransmission(_config.addr);
    Wire.write(reg);
    for (uint8_t i = 0; i < len; i++) { Wire.write(buf[i]); }
    _lastError = Wire.endTransmission();
    return (_lastError == 0);
}

// ====================================================================
// Детекция DS3231
// ====================================================================

bool MODULE_CLASS_DS3231::_detectDS3231(uint8_t addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err != 0) { return false; }

    Wire.beginTransmission(addr);
    Wire.write(0x0F);
    err = Wire.endTransmission();
    if (err != 0) { return false; }
    Wire.requestFrom((int)addr, 1);
    if (!Wire.available()) { return false; }
    uint8_t stat = Wire.read();

    Wire.beginTransmission(addr);
    Wire.write(0x0E);
    err = Wire.endTransmission();
    if (err != 0) { return false; }
    Wire.requestFrom((int)addr, 1);
    if (!Wire.available()) { return false; }
    uint8_t ctrl = Wire.read();

    if (ctrl == 0xFF || stat == 0xFF) { return false; }

    if ((stat & 0x80) && (stat & 0x08)) { } else { }

    Wire.beginTransmission(addr);
    Wire.write(0x11);
    err = Wire.endTransmission();
    if (err != 0) { return false; }
    Wire.requestFrom((int)addr, 2);
    if (Wire.available() < 2) { return false; }
    int8_t tempUpper = (int8_t)Wire.read();
    if (tempUpper < -40 || tempUpper > 85) { return false; }

    return true;
}

uint8_t MODULE_CLASS_DS3231::_scanForDS3231() {
    DEBUGDS3231("DS3231: сканирование шины I2C...\r\n");
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
#if defined(ESP32)
        esp_task_wdt_reset();
#elif defined(ESP8266)
        ESP.wdtFeed();
#endif
        if (_detectDS3231(addr)) {
            DEBUGDS3231("DS3231: найдено устройство по адресу 0x%02X\r\n", addr);
            return addr;
        }
    }
    DEBUGDS3231("DS3231: устройство НЕ найдено\r\n");
    return DS3231_ADDR_NONE;
}

// ====================================================================
// Чтение / запись времени
// ====================================================================

time_t MODULE_CLASS_DS3231::_readTime() {
    uint8_t buf[7];
    if (!_readBlock(0x00, buf, 7)) { return 0; }

    uint8_t sec   = _bcd2dec(buf[0] & 0x7F);
    uint8_t min   = _bcd2dec(buf[1]);
    uint8_t hour  = _bcd2dec(buf[2] & 0x3F);
    uint8_t day   = _bcd2dec(buf[4]);
    uint8_t mon   = _bcd2dec(buf[5] & 0x1F);
    uint16_t yr   = _bcd2dec(buf[6]) + 2000;

    tmElements_t tm;
    tm.Year   = yr - 1970;
    tm.Month  = mon;
    tm.Day    = day;
    tm.Hour   = hour;
    tm.Minute = min;
    tm.Second = sec;
    return makeTime(tm);
}

bool MODULE_CLASS_DS3231::_writeTime(time_t t) {
    tmElements_t tm;
    breakTime(t, tm);

    uint8_t buf[7];
    buf[0] = _dec2bcd(tm.Second) & 0x7F;
    buf[1] = _dec2bcd(tm.Minute);
    buf[2] = _dec2bcd(tm.Hour);
    buf[3] = _dec2bcd(weekday(t));
    buf[4] = _dec2bcd(tm.Day);
    buf[5] = _dec2bcd(tm.Month);
    buf[6] = _dec2bcd((tm.Year + 1970) - 2000);
    return _writeBlock(0x00, buf, 7);
}

// ====================================================================
// Публичное API
// ====================================================================

time_t MODULE_CLASS_DS3231::getTime() {
    uint8_t secReg = _readReg(0x00);
    if (secReg & 0x80) {
        _writeReg(0x00, secReg & 0x7F);
    }
    return _readTime();
}

bool MODULE_CLASS_DS3231::setTime(time_t t) {
    return _writeTime(t);
}

bool MODULE_CLASS_DS3231::setTime(int yr, int mon, int day, int hr, int min, int sec) {
    if (yr < 2000) { yr += 2000; }
    if (yr < 2000 || yr > 2099) { _lastError = -10; return false; }
    if (mon < 1 || mon > 12)    { _lastError = -11; return false; }
    if (day < 1 || day > 31)    { _lastError = -12; return false; }
    if (hr > 23)                { _lastError = -13; return false; }
    if (min > 59)               { _lastError = -14; return false; }
    if (sec > 59)               { _lastError = -15; return false; }
    tmElements_t tm;
    tm.Year   = yr - 1970;
    tm.Month  = mon;
    tm.Day    = day;
    tm.Hour   = hr;
    tm.Minute = min;
    tm.Second = sec;
    time_t t = makeTime(tm);
    return _writeTime(t);
}

bool MODULE_CLASS_DS3231::getTemperature(float &temp) {
    uint8_t buf[2];
    if (!_readBlock(0x11, buf, 2)) { return false; }
    int16_t raw = ((int16_t)(int8_t)buf[0] << 8) | buf[1];
    temp = raw / 256.0f;
    return true;
}

bool MODULE_CLASS_DS3231::isConnected() {
    return (_config.addr != DS3231_ADDR_NONE);
}

uint8_t MODULE_CLASS_DS3231::getAddr() {
    return _config.addr;
}

int MODULE_CLASS_DS3231::getLastError() {
    return _lastError;
}

// ====================================================================
// Будильники — декодирование/кодирование режимов
// Alarm 1: 4 байта с 0x07 (sec, min, hour, day/date)
// Alarm 2: 3 байта с 0x0B (min, hour, day/date)
// mode: 0=once, 1=match_sec/min, 2=match_min_sec/min_min, 3=match_hr_min_sec/hr_min, 4=match_day_date
// ====================================================================

static void _decodeAlarm1Mode(uint8_t *buf, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    bool a1m1 = (buf[0] >> 7) & 1;
    bool a1m2 = (buf[1] >> 7) & 1;
    bool a1m3 = (buf[2] >> 7) & 1;
    bool a1m4 = (buf[3] >> 7) & 1;
    isDayOfWeek = (buf[3] >> 6) & 1;

    if (a1m1 && a1m2 && a1m3 && a1m4)              { mode = 0; }
    else if (!a1m1 && a1m2 && a1m3 && a1m4)        { mode = 1; }
    else if (!a1m1 && !a1m2 && a1m3 && a1m4)        { mode = 2; }
    else if (!a1m1 && !a1m2 && !a1m3 && a1m4)        { mode = 3; }
    else                                             { mode = 4; }

    dayOrDate = _bcd2dec(buf[3] & 0x3F);
}

static void _encodeAlarm1Mode(uint8_t *buf, uint8_t sec, uint8_t min, uint8_t hour, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    buf[0] = _dec2bcd(sec);
    buf[1] = _dec2bcd(min);
    buf[2] = _dec2bcd(hour);
    buf[3] = _dec2bcd(dayOrDate);
    if (isDayOfWeek) { buf[3] |= 0x40; }

    switch (mode) {
        case 0: buf[0] |= 0x80; buf[1] |= 0x80; buf[2] |= 0x80; buf[3] |= 0x80; break;
        case 1:                     buf[1] |= 0x80; buf[2] |= 0x80; buf[3] |= 0x80; break;
        case 2:                                         buf[2] |= 0x80; buf[3] |= 0x80; break;
        case 3:                                                             buf[3] |= 0x80; break;
        case 4: break;
    }
}

static void _decodeAlarm2Mode(uint8_t *buf, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    bool a2m2 = (buf[0] >> 7) & 1;
    bool a2m3 = (buf[1] >> 7) & 1;
    bool a2m4 = (buf[2] >> 7) & 1;
    isDayOfWeek = (buf[2] >> 6) & 1;

    if (a2m2 && a2m3 && a2m4)              { mode = 0; }
    else if (!a2m2 && a2m3 && a2m4)         { mode = 1; }
    else if (!a2m2 && !a2m3 && a2m4)         { mode = 2; }
    else                                      { mode = 3; }

    dayOrDate = _bcd2dec(buf[2] & 0x3F);
}

static void _encodeAlarm2Mode(uint8_t *buf, uint8_t min, uint8_t hour, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    buf[0] = _dec2bcd(min);
    buf[1] = _dec2bcd(hour);
    buf[2] = _dec2bcd(dayOrDate);
    if (isDayOfWeek) { buf[2] |= 0x40; }

    switch (mode) {
        case 0: buf[0] |= 0x80; buf[1] |= 0x80; buf[2] |= 0x80; break;
        case 1:                     buf[1] |= 0x80; buf[2] |= 0x80; break;
        case 2:                                         buf[2] |= 0x80; break;
        case 3: break;
    }
}

bool MODULE_CLASS_DS3231::getAlarm1(uint8_t &hour, uint8_t &min, uint8_t &sec, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    uint8_t buf[4];
    if (!_readBlock(0x07, buf, 4)) { return false; }
    sec  = _bcd2dec(buf[0] & 0x7F);
    min  = _bcd2dec(buf[1] & 0x7F);
    hour = _bcd2dec(buf[2] & 0x3F);
    _decodeAlarm1Mode(buf, mode, dayOrDate, isDayOfWeek);
    return true;
}

bool MODULE_CLASS_DS3231::setAlarm1(uint8_t hour, uint8_t min, uint8_t sec, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    if (mode > 4) { mode = 4; }
    uint8_t buf[4];
    _encodeAlarm1Mode(buf, sec, min, hour, mode, dayOrDate, isDayOfWeek);
    bool ok = _writeBlock(0x07, buf, 4);
    if (ok) {
        uint8_t ctrl = _readReg(0x0E);
        ctrl |= 0x01;
        _writeReg(0x0E, ctrl);
    }
    return ok;
}

bool MODULE_CLASS_DS3231::getAlarm2(uint8_t &hour, uint8_t &min, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    uint8_t buf[3];
    if (!_readBlock(0x0B, buf, 3)) { return false; }
    min  = _bcd2dec(buf[0] & 0x7F);
    hour = _bcd2dec(buf[1] & 0x3F);
    _decodeAlarm2Mode(buf, mode, dayOrDate, isDayOfWeek);
    return true;
}

bool MODULE_CLASS_DS3231::setAlarm2(uint8_t hour, uint8_t min, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    if (mode > 3) { mode = 3; }
    uint8_t buf[3];
    _encodeAlarm2Mode(buf, min, hour, mode, dayOrDate, isDayOfWeek);
    bool ok = _writeBlock(0x0B, buf, 3);
    if (ok) {
        uint8_t ctrl = _readReg(0x0E);
        ctrl |= 0x02;
        _writeReg(0x0E, ctrl);
    }
    return ok;
}

// ====================================================================
// begin()
// ====================================================================

void MODULE_CLASS_DS3231::begin() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }

    if (_config.addr != DS3231_ADDR_NONE) {
        bool found = false;
        for (uint8_t retry = 0; retry < DS3231_SCAN_RETRIES; retry++) {
            if (_detectDS3231(_config.addr)) { found = true; break; }
            delay(100);
        }
        if (!found) {
            DEBUGDS3231("DS3231:  0x%02X  didn't response. Rescan...\r\n", _config.addr);
            _config.addr = _scanForDS3231();
            saveConfig();
        }
    } else {
        _config.addr = _scanForDS3231();
        saveConfig();
    }

    if (_config.addr != DS3231_ADDR_NONE) {
        DEBUGDS3231("DS3231: inited, adr: 0x%02X\r\n", _config.addr);
    } else {
        DEBUGDS3231("DS3231: Not found!\r\n");
    }
}

// ====================================================================
// webInit()
// ====================================================================

void MODULE_CLASS_DS3231::webInit() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/ds3231/read", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleRead(request);
    });

    ESPHTTPServer.on("/ds3231/poll", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handlePoll(request);
    });

    ESPHTTPServer.on("/ds3231/set_time", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSetTime(request);
    });

    ESPHTTPServer.on("/ds3231/set_alarm1", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSetAlarm1(request);
    });

    ESPHTTPServer.on("/ds3231/set_alarm2", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSetAlarm2(request);
    });

    ESPHTTPServer.on("/ds3231/set_reg", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSetReg(request);
    });

    ESPHTTPServer.on("/ds3231/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSaveConfig(request);
    });

    ESPHTTPServer.on("/ds3231/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/ds3231/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ====================================================================
// Веб-обработчики
// ====================================================================

void MODULE_CLASS_DS3231::handleRead(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";

    if (_config.addr == DS3231_ADDR_NONE) {
        values += "ds_state|disconnected|div\n";
        values += "ds_addr|0|div\n";
        values += "ds_temp|--|div\n";
        values += "ds_ctrl_reg|00|div\n";
        values += "ds_stat_reg|00|div\n";
        values += "ds_aging_offset|0|input\n";
        values += "ds_alarm1_hour|0|input\n";
        values += "ds_alarm1_min|0|input\n";
        values += "ds_alarm1_sec|0|input\n";
        values += "ds_alarm1_mode|0|input\n";
        values += "ds_alarm1_dayOrDate|1|input\n";
        values += "ds_alarm1_isDayOfWeek||chk\n";
        values += "ds_alarm2_hour|0|input\n";
        values += "ds_alarm2_min|0|input\n";
        values += "ds_alarm2_mode|0|input\n";
        values += "ds_alarm2_dayOrDate|1|input\n";
        values += "ds_alarm2_isDayOfWeek||chk\n";
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", _config.addr);
        values += "ds_addr|" + String(buf) + "|div\n";
        request->send(200, "text/plain", values);
        return;
    }

    values += "ds_state|connected|div\n";
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", _config.addr);
        values += "ds_addr|" + String(buf) + "|div\n";
    }

    // Время
    {
        time_t t = _readTime();
        if (t > 0) {
            String dt = "";
            if (year(t) > 2000) {
                dt += String(year(t)) + "-";
                if (month(t) < 10) { dt += "0"; }
                dt += String(month(t)) + "-";
                if (day(t) < 10) { dt += "0"; }
                dt += String(day(t)) + " ";
                if (hour(t) < 10) { dt += "0"; }
                dt += String(hour(t)) + ":";
                if (minute(t) < 10) { dt += "0"; }
                dt += String(minute(t)) + ":";
                if (second(t) < 10) { dt += "0"; }
                dt += String(second(t));
            }
            values += "ds_time|" + dt + "|div\n";
        } else {
            values += "ds_time|Read error|div\n";
        }
    }

    // Температура
    {
        float temp;
        if (getTemperature(temp)) {
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%.2f", temp);
            values += "ds_temp|" + String(tbuf) + "|div\n";
        } else {
            values += "ds_temp|--|div\n";
        }
    }

    // Регистры управления
    {
        uint8_t ctrl = _readReg(0x0E);
        uint8_t stat = _readReg(0x0F);
        uint8_t aging = _readReg(0x10);
        char rbuf[16];
        snprintf(rbuf, sizeof(rbuf), "0x%02X", ctrl);
        values += "ds_ctrl_reg|" + String(rbuf) + "|div\n";
        snprintf(rbuf, sizeof(rbuf), "0x%02X", stat);
        values += "ds_stat_reg|" + String(rbuf) + "|div\n";
        values += "ds_aging_offset|" + String(aging) + "|input\n";

        // Биты управления
        values += "ds_ctrl_eosc|"   + String((ctrl & 0x80) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_bbsqw|"  + String((ctrl & 0x40) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_conv|"   + String((ctrl & 0x20) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_rs|"     + String((ctrl >> 3) & 3)           + "|div\n";
        values += "ds_ctrl_intcn|"  + String((ctrl & 0x04) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_a2ie|"   + String((ctrl & 0x02) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_a1ie|"   + String((ctrl & 0x01) ? "1" : "0") + "|div\n";

        // Биты статуса
        values += "ds_stat_osf|"    + String((stat & 0x80) ? "1" : "0") + "|div\n";
        values += "ds_stat_en32khz|"+ String((stat & 0x08) ? "1" : "0") + "|div\n";
        values += "ds_stat_bsy|"    + String((stat & 0x04) ? "1" : "0") + "|div\n";
        values += "ds_stat_a2f|"    + String((stat & 0x02) ? "1" : "0") + "|div\n";
        values += "ds_stat_a1f|"    + String((stat & 0x01) ? "1" : "0") + "|div\n";
    }

    // Будильники
    {
        uint8_t h, m, s, mode, dayOrDate;
        bool isDOW;
        if (getAlarm1(h, m, s, mode, dayOrDate, isDOW)) {
            values += "ds_alarm1_hour|"        + String(h)        + "|input\n";
            values += "ds_alarm1_min|"         + String(m)        + "|input\n";
            values += "ds_alarm1_sec|"         + String(s)        + "|input\n";
            values += "ds_alarm1_mode|"        + String(mode)     + "|select\n";
            values += "ds_alarm1_dayOrDate|"   + String(dayOrDate)+ "|input\n";
            values += "ds_alarm1_isDayOfWeek|" + String(isDOW ? "checked" : "") + "|chk\n";
        }
        if (getAlarm2(h, m, mode, dayOrDate, isDOW)) {
            values += "ds_alarm2_hour|"        + String(h)        + "|input\n";
            values += "ds_alarm2_min|"         + String(m)        + "|input\n";
            values += "ds_alarm2_mode|"        + String(mode)     + "|select\n";
            values += "ds_alarm2_dayOrDate|"   + String(dayOrDate)+ "|input\n";
            values += "ds_alarm2_isDayOfWeek|" + String(isDOW ? "checked" : "") + "|chk\n";
        }
    }

    request->send(200, "text/plain", values);
}

void MODULE_CLASS_DS3231::handlePoll(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";

    if (_config.addr == DS3231_ADDR_NONE) {
        values += "ds_state|disconnected|div\n";
        request->send(200, "text/plain", values);
        return;
    }

    values += "ds_state|connected|div\n";
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%02X", _config.addr);
        values += "ds_addr|" + String(buf) + "|div\n";
    }

    {
        time_t t = _readTime();
        if (t > 0) {
            String dt = "";
            if (year(t) > 2000) {
                dt += String(year(t)) + "-";
                if (month(t) < 10) { dt += "0"; }
                dt += String(month(t)) + "-";
                if (day(t) < 10) { dt += "0"; }
                dt += String(day(t)) + " ";
                if (hour(t) < 10) { dt += "0"; }
                dt += String(hour(t)) + ":";
                if (minute(t) < 10) { dt += "0"; }
                dt += String(minute(t)) + ":";
                if (second(t) < 10) { dt += "0"; }
                dt += String(second(t));
            }
            values += "ds_time|" + dt + "|div\n";
        } else {
            values += "ds_time|Read error|div\n";
        }
    }

    {
        float temp;
        if (getTemperature(temp)) {
            char tbuf[16];
            snprintf(tbuf, sizeof(tbuf), "%.2f", temp);
            values += "ds_temp|" + String(tbuf) + "|div\n";
        } else {
            values += "ds_temp|--|div\n";
        }
    }

    {
        uint8_t ctrl = _readReg(0x0E);
        uint8_t stat = _readReg(0x0F);
        char rbuf[16];
        snprintf(rbuf, sizeof(rbuf), "0x%02X", ctrl);
        values += "ds_ctrl_reg|" + String(rbuf) + "|div\n";
        snprintf(rbuf, sizeof(rbuf), "0x%02X", stat);
        values += "ds_stat_reg|" + String(rbuf) + "|div\n";

        values += "ds_ctrl_eosc|"   + String((ctrl & 0x80) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_bbsqw|"  + String((ctrl & 0x40) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_conv|"   + String((ctrl & 0x20) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_rs|"     + String((ctrl >> 3) & 3)           + "|div\n";
        values += "ds_ctrl_intcn|"  + String((ctrl & 0x04) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_a2ie|"   + String((ctrl & 0x02) ? "1" : "0") + "|div\n";
        values += "ds_ctrl_a1ie|"   + String((ctrl & 0x01) ? "1" : "0") + "|div\n";

        values += "ds_stat_osf|"    + String((stat & 0x80) ? "1" : "0") + "|div\n";
        values += "ds_stat_en32khz|"+ String((stat & 0x08) ? "1" : "0") + "|div\n";
        values += "ds_stat_bsy|"    + String((stat & 0x04) ? "1" : "0") + "|div\n";
        values += "ds_stat_a2f|"    + String((stat & 0x02) ? "1" : "0") + "|div\n";
        values += "ds_stat_a1f|"    + String((stat & 0x01) ? "1" : "0") + "|div\n";
    }

    request->send(200, "text/plain", values);
}

void MODULE_CLASS_DS3231::handleSetTime(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_config.addr == DS3231_ADDR_NONE) { request->send(200, "text/plain", "DS3231 not connected"); return; }

    if (request->hasArg("source") && request->arg("source") == "ntp") {
        if (NTP.getLastNTPSync() > 0) {
            time_t nowT = now();
            if (_writeTime(nowT)) {
                request->send(200, "text/plain", "OK");
            } else {
                request->send(200, "text/plain", "Write error");
            }
        } else {
            request->send(200, "text/plain", "NTP not synced");
        }
        return;
    }

    int yr    = request->hasArg("year")   ? request->arg("year").toInt()   : -1;
    int mon   = request->hasArg("month")  ? request->arg("month").toInt()  : -1;
    int day   = request->hasArg("day")    ? request->arg("day").toInt()    : -1;
    int hr    = request->hasArg("hour")   ? request->arg("hour").toInt()   : -1;
    int min   = request->hasArg("minute") ? request->arg("minute").toInt() : -1;
    int sec   = request->hasArg("second") ? request->arg("second").toInt() : 0;

    if (yr < 0 || mon < 0 || day < 0 || hr < 0 || min < 0) {
        request->send(200, "text/plain", "Missing parameters");
        return;
    }

    if (setTime(yr, mon, day, hr, min, sec)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "Error");
    }
}

void MODULE_CLASS_DS3231::handleSetAlarm1(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_config.addr == DS3231_ADDR_NONE) { request->send(200, "text/plain", "DS3231 not connected"); return; }

    uint8_t hour  = request->hasArg("hour")   ? (uint8_t)request->arg("hour").toInt()   : 0;
    uint8_t min   = request->hasArg("min")    ? (uint8_t)request->arg("min").toInt()    : 0;
    uint8_t sec   = request->hasArg("sec")    ? (uint8_t)request->arg("sec").toInt()    : 0;
    uint8_t mode  = request->hasArg("mode")   ? (uint8_t)request->arg("mode").toInt()   : 4;
    uint8_t dayOrDate = request->hasArg("dayOrDate") ? (uint8_t)request->arg("dayOrDate").toInt() : 1;
    bool isDOW    = request->hasArg("isDayOfWeek") && request->arg("isDayOfWeek") == "true";

    if (setAlarm1(hour, min, sec, mode, dayOrDate, isDOW)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "Error");
    }
}

void MODULE_CLASS_DS3231::handleSetAlarm2(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_config.addr == DS3231_ADDR_NONE) { request->send(200, "text/plain", "DS3231 not connected"); return; }

    uint8_t hour  = request->hasArg("hour")   ? (uint8_t)request->arg("hour").toInt()   : 0;
    uint8_t min   = request->hasArg("min")    ? (uint8_t)request->arg("min").toInt()    : 0;
    uint8_t mode  = request->hasArg("mode")   ? (uint8_t)request->arg("mode").toInt()   : 3;
    uint8_t dayOrDate = request->hasArg("dayOrDate") ? (uint8_t)request->arg("dayOrDate").toInt() : 1;
    bool isDOW    = request->hasArg("isDayOfWeek") && request->arg("isDayOfWeek") == "true";

    if (setAlarm2(hour, min, mode, dayOrDate, isDOW)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "Error");
    }
}

void MODULE_CLASS_DS3231::handleSetReg(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_config.addr == DS3231_ADDR_NONE) { request->send(200, "text/plain", "DS3231 not connected"); return; }

    if (request->hasArg("ctrl")) {
        uint8_t val = (uint8_t)strtol(request->arg("ctrl").c_str(), NULL, 16);
        _writeReg(0x0E, val);
    }
    if (request->hasArg("stat")) {
        uint8_t val = (uint8_t)strtol(request->arg("stat").c_str(), NULL, 16);
        _writeReg(0x0F, val);
    }

    request->send(200, "text/plain", "OK");
}

void MODULE_CLASS_DS3231::handleSaveConfig(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (request->hasArg("autoPoll")) {
        _config.autoPoll = (request->arg("autoPoll") == "true");
    }
    if (request->hasArg("pollInterval")) {
        uint16_t val = (uint16_t)request->arg("pollInterval").toInt();
        if (val < 1)  { val = 1; }
        if (val > 3600) { val = 3600; }
        _config.pollInterval = val;
    }
    saveConfig();
    request->send(200, "text/plain", "OK");
}

void MODULE_CLASS_DS3231::handleInfo(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ds_autoPoll|"      + String(_config.autoPoll ? "checked" : "") + "|chk\n";
    values += "ds_pollInterval|"  + String(_config.pollInterval)               + "|input\n";
    values += "ds_scanRetries|"   + String(DS3231_SCAN_RETRIES)               + "|div\n";
    request->send(200, "text/plain", values);
}

// ====================================================================
// Конфиг
// ====================================================================

void MODULE_CLASS_DS3231::defaultConfig() {
    _config.addr         = DS3231_ADDR_NONE;
    _config.autoPoll     = false;
    _config.pollInterval = 5;
}

bool MODULE_CLASS_DS3231::loadConfig() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_DS3231, doc) == false) { return false; }

    _config.addr         = doc["addr"].as<uint8_t>();
    _config.autoPoll     = doc["autoPoll"].as<bool>();
    _config.pollInterval = doc["pollInterval"].as<uint16_t>();

    DEBUGDS3231("addr: 0x%02X, autoPoll: %d, pollInterval: %d\r\n", _config.addr, _config.autoPoll, _config.pollInterval);
    return true;
}

bool MODULE_CLASS_DS3231::saveConfig() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_DS3231, doc);
    doc["addr"]         = _config.addr;
    doc["autoPoll"]     = _config.autoPoll;
    doc["pollInterval"] = _config.pollInterval;
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_DS3231, doc);
}

// ====================================================================
// Версионные методы
// ====================================================================

String MODULE_CLASS_DS3231::getVersionStr() {
    return String(MODULE_DS3231_VERSION);
}

String MODULE_CLASS_DS3231::getGeneratedTime() {
    return String(MODULE_DS3231_GENERATED_TIME);
}

String MODULE_CLASS_DS3231::getCommitDateStr() {
    return String(MODULE_DS3231_COMMIT_DATE_STR);
}

void MODULE_CLASS_DS3231::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ds3231version|" + getVersionStr()    + "|div\n";
    values += "ds3231gentime|" + getGeneratedTime() + "|div\n";
    values += "ds3231gendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
