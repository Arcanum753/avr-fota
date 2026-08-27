#include "FSWebServerLib.h"

#include "module_ds3231.h"
#include "module_ds3231_version.h"
#include "common.h"
#include "eertos.h"

#include "core_json/core_json.h"
#include "core_ntp/NtpClientLib.h"
#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#include <Wire.h>
#include <TimeLib.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp_attr.h>
#endif

CLASS_MODULE_DS3231 ModClassDs3231(false);
CLASS_MODULE_DS3231::CLASS_MODULE_DS3231(bool _in) {
    dumb = _in; _lastError = 0; _wireStarted = false;
#if defined(ESP32)
    _sqwIrqFlag = false;
    _sqwLastLowEdge = false;
    _sqwCareActive = false;
    _alarm1Fired = false;
    _alarm2Fired = false;
    _lastAlarm1At = 0;
    _lastAlarm2At = 0;
    _sqwInterrupting = false;
#endif
}

// ====================================================================
// SQW / GPIO — свободные функции (только ESP32)
// ====================================================================
#if defined(ESP32)
void IRAM_ATTR ds3231SqwIsr() {
    ModClassDs3231.sqwSetIrqFlag();
}

void ds3231SqwPollTask() {
    ModClassDs3231.sqwPollStep();
}
#endif

#if defined(ESP32)
void CLASS_MODULE_DS3231::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void CLASS_MODULE_DS3231::setFs(FS* fs)
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

uint8_t CLASS_MODULE_DS3231::_readReg(uint8_t reg) {
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

bool CLASS_MODULE_DS3231::_writeReg(uint8_t reg, uint8_t val) {
    if (_config.addr == DS3231_ADDR_NONE) { _lastError = -1; return false; }
    if (!_wireStarted) { _wireStarted = true; Wire.begin(); }
    Wire.beginTransmission(_config.addr);
    Wire.write(reg);
    Wire.write(val);
    _lastError = Wire.endTransmission();
    return (_lastError == 0);
}

bool CLASS_MODULE_DS3231::_readBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
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

bool CLASS_MODULE_DS3231::_writeBlock(uint8_t reg, uint8_t *buf, uint8_t len) {
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

bool CLASS_MODULE_DS3231::_detectDS3231(uint8_t addr) {
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

uint8_t CLASS_MODULE_DS3231::_scanForDS3231() {
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

time_t CLASS_MODULE_DS3231::_readTime() {
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

bool CLASS_MODULE_DS3231::_writeTime(time_t t) {
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

time_t CLASS_MODULE_DS3231::getTime() {
    if (_config.addr == DS3231_ADDR_NONE) { return 0; }
    // Бит 7 регистра 0x00 — CH (Clock Halt). Если осциллятор был остановлен,
    // снимаем стоп-бит, чтобы продолжить счёт времени.
    uint8_t secReg = _readReg(0x00);
    if (secReg & 0x80) {
        _writeReg(0x00, secReg & 0x7F);
    }
    // OSF (0x0F bit7) — осциллятор останавливался, время недостоверно.
    // Возвращаем только надёжное время; флаг не сбрасываем.
    uint8_t stat = _readReg(0x0F);
    if (stat & 0x80) { return 0; }
    return _readTime();
}

bool CLASS_MODULE_DS3231::setTime(time_t t) {
    return _writeTime(t);
}

bool CLASS_MODULE_DS3231::setTime(int yr, int mon, int day, int hr, int min, int sec) {
    if (yr < 2000) { yr += 2000; }
    if (yr < 2000 || yr > 2099) { _lastError = -10; return false; }
    if (mon < 1 || mon > 12)    { _lastError = -11; return false; }
    if (day < 1 || hr < 0 || min < 0 || sec < 0 ||
        hr > 23 || min > 59 || sec > 59) { _lastError = -12; return false; }
    // Проверяем фактическое число дней в месяце (с учётом високосного года).
    static const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t maxDays = daysInMonth[mon - 1];
    if (mon == 2) {
        bool leap = ((yr % 4 == 0) && (yr % 100 != 0)) || (yr % 400 == 0);
        if (leap) { maxDays = 29; }
    }
    if (day > maxDays) { _lastError = -12; return false; }
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

bool CLASS_MODULE_DS3231::getTemperature(float &temp) {
    uint8_t buf[2];
    if (!_readBlock(0x11, buf, 2)) { return false; }
    // Проверяем целый градус в допустимом диапазоне — иначе данные невалидны
    // (например, чтение плавающего вывода без подтяжки).
    int8_t tempUpper = (int8_t)buf[0];
    if (tempUpper < -40 || tempUpper > 85) { return false; }
    int16_t raw = ((int16_t)tempUpper << 8) | buf[1];
    temp = raw / 256.0f;
    return true;
}

bool CLASS_MODULE_DS3231::isConnected() {
    return (_config.addr != DS3231_ADDR_NONE);
}

uint8_t CLASS_MODULE_DS3231::getAddr() {
    return _config.addr;
}

int CLASS_MODULE_DS3231::getLastError() {
    return _lastError;
}

uint8_t CLASS_MODULE_DS3231::getStatusReg() {
    return _readReg(0x0F);
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

bool CLASS_MODULE_DS3231::getAlarm1(uint8_t &hour, uint8_t &min, uint8_t &sec, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    uint8_t buf[4];
    if (!_readBlock(0x07, buf, 4)) { return false; }
    sec  = _bcd2dec(buf[0] & 0x7F);
    min  = _bcd2dec(buf[1] & 0x7F);
    hour = _bcd2dec(buf[2] & 0x3F);
    _decodeAlarm1Mode(buf, mode, dayOrDate, isDayOfWeek);
    return true;
}

bool CLASS_MODULE_DS3231::setAlarm1(uint8_t hour, uint8_t min, uint8_t sec, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    if (mode > 4) { mode = 4; }
    uint8_t buf[4];
    _encodeAlarm1Mode(buf, sec, min, hour, mode, dayOrDate, isDayOfWeek);
    bool ok = _writeBlock(0x07, buf, 4);
    if (ok) {
        // Разрешаем прерывание Alarm 1 (A1IE) только когда выход настроен на
        // будильники (INTCN=1). При меандре (BBSQW=1) биты прерываний не влияют.
        if (_config.ctrlIntcn && !_config.ctrlBbsqw) {
            uint8_t ctrl = _readReg(0x0E);
            ctrl |= 0x01;
            _writeReg(0x0E, ctrl);
        }
        // Сбрасываем внутренний флаг, а также флаг A1F в Status, чтобы
        // предыдущее срабатывание не считалось новым.
#if defined(ESP32)
        _alarm1Fired = false;
#endif
        uint8_t stat = _readReg(0x0F);
        stat &= ~0x01;
        _writeReg(0x0F, stat);
    }
    return ok;
}

bool CLASS_MODULE_DS3231::getAlarm2(uint8_t &hour, uint8_t &min, uint8_t &mode, uint8_t &dayOrDate, bool &isDayOfWeek) {
    uint8_t buf[3];
    if (!_readBlock(0x0B, buf, 3)) { return false; }
    min  = _bcd2dec(buf[0] & 0x7F);
    hour = _bcd2dec(buf[1] & 0x3F);
    _decodeAlarm2Mode(buf, mode, dayOrDate, isDayOfWeek);
    return true;
}

bool CLASS_MODULE_DS3231::setAlarm2(uint8_t hour, uint8_t min, uint8_t mode, uint8_t dayOrDate, bool isDayOfWeek) {
    if (mode > 3) { mode = 3; }
    uint8_t buf[3];
    _encodeAlarm2Mode(buf, min, hour, mode, dayOrDate, isDayOfWeek);
    bool ok = _writeBlock(0x0B, buf, 3);
    if (ok) {
        // Разрешаем прерывание Alarm 2 (A2IE) только когда выход настроен на
        // будильники (INTCN=1). При меандре (BBSQW=1) биты прерываний не влияют.
        if (_config.ctrlIntcn && !_config.ctrlBbsqw) {
            uint8_t ctrl = _readReg(0x0E);
            ctrl |= 0x02;
            _writeReg(0x0E, ctrl);
        }
        // Сбрасываем внутренний флаг, а также флаг A2F в Status, чтобы
        // предыдущее срабатывание не считалось новым.
#if defined(ESP32)
        _alarm2Fired = false;
#endif
        uint8_t stat = _readReg(0x0F);
        stat &= ~0x02;
        _writeReg(0x0F, stat);
    }
    return ok;
}

// ====================================================================
// SQW / GPIO — реализация (только ESP32)
// ====================================================================
#if defined(ESP32)

// Собрать байт Control (0x0E) из полей конфига
bool CLASS_MODULE_DS3231::_ctrlBitsToReg() {
    // EOSC (bit7)=0 — осциллятор включён
    uint8_t ctrl = 0x00;
    if (_config.ctrlBbsqw)  { ctrl |= 0x40; }
    if ((_config.ctrlRs & 0x03) != 0) { ctrl |= ((_config.ctrlRs & 0x03) << 3); }
    if (_config.ctrlIntcn)  { ctrl |= 0x04; }
    if (_config.ctrlA2ie)   { ctrl |= 0x02; }
    if (_config.ctrlA1ie)   { ctrl |= 0x01; }
    return _writeReg(0x0E, ctrl);
}

// Записать биты Control (0x0E) из конфига
void CLASS_MODULE_DS3231::_applyCtrlBits() {
    if (_config.addr == DS3231_ADDR_NONE) { return; }
    _ctrlBitsToReg();
}

// Инициализация GPIO вывода SQW/INT#
void CLASS_MODULE_DS3231::sqwGpioInit() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    _sqwIrqFlag = false;
    _sqwCareActive = false;
    _alarm1Fired = false;
    _alarm2Fired = false;
    _sqwInterrupting = false;

    if (_config.addr == DS3231_ADDR_NONE) {
        DEBUGDS3231("DS3231: cannot init SQW, DS3231 not connected\r\n");
        return;
    }

    // Всегда планируем периодический детектор флагов будильников (раз в секунду),
    // независимо от способа чтения GPIO, чтобы сообщения выводились вовремя.
    DelTimerTask(ds3231SqwPollTask);
    SetTimerTask(ds3231SqwPollTask, 1000);
    DEBUGDS3231("DS3231: alarm detector scheduled (1s)\r\n");

    if (!_config.sqwEnabled) {
        DEBUGDS3231("DS3231: monitoring SQW disabled\r\n");
        return;
    }

    pinMode(DS3231_SQW_PIN, INPUT_PULLUP);
    _sqwLastLowEdge = (digitalRead(DS3231_SQW_PIN) == (uint8_t)_config.sqwLevelActive);
    DEBUGDS3231("DS3231: SQW pin D%d mode=%s activeLevel=%d\r\n",
                DS3231_SQW_PIN,
                (_config.sqwMode == DS3231_SQW_MODE_INTERRUPT) ? "interrupt" : "polling",
                _config.sqwLevelActive ? 1 : 0);

    if (_config.sqwMode == DS3231_SQW_MODE_INTERRUPT) {
        sqwEnableInterrupt();
    }
}

// Остановка мониторинга GPIO SQW (детектор флагов остаётся активным)
void CLASS_MODULE_DS3231::sqwGpioStop() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
#if defined(ESP32)
    sqwDisableInterrupt();
#endif
}

// Переинициализация после смены конфига
void CLASS_MODULE_DS3231::sqwGpioReinit() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    sqwGpioStop();
    sqwGpioInit();
}

// Подключение прерывания
void CLASS_MODULE_DS3231::sqwEnableInterrupt() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_sqwInterrupting) { return; }
    // При активном высоком уровне отслеживаем RISING, при низком — FALLING
    int intMode = _config.sqwLevelActive ? RISING : FALLING;
    attachInterrupt(digitalPinToInterrupt(DS3231_SQW_PIN), ds3231SqwIsr, intMode);
    _sqwInterrupting = true;
    DEBUGDS3231("DS3231: SQW interrupt attached, mode=%s\r\n", (intMode == RISING) ? "RISING" : "FALLING");
}

// Отключение прерывания
void CLASS_MODULE_DS3231::sqwDisableInterrupt() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (!_sqwInterrupting) { return; }
    detachInterrupt(digitalPinToInterrupt(DS3231_SQW_PIN));
    _sqwInterrupting = false;
}

// Вызвается из ISR — только ставим флаг, тяжёлую работу делаем в main-loop
void CLASS_MODULE_DS3231::sqwSetIrqFlag() {
    _sqwIrqFlag = true;
}

// Обработка флага прерывания/спайка в основном контексте
void CLASS_MODULE_DS3231::checkSqw() {
    bool level = (digitalRead(DS3231_SQW_PIN) == (uint8_t)_config.sqwLevelActive);
    // Ловим фронт «наступление активного сигнала»
    if (level && !_sqwLastLowEdge) {
        if (!_sqwCareActive) {
            _sqwCareActive = true;
            DEBUGDS3231("DS3231: INT# / SQW стал активным (будильник сработал)\r\n");
        }
    }
    _sqwLastLowEdge = level;
    _sqwIrqFlag = false;
}

// Шаг опроса (периодическая задача)
void CLASS_MODULE_DS3231::sqwPollStep() {
    checkAlarmFlags();
    // Перепланируем задачу на следующий интервал
    SetTimerTask(ds3231SqwPollTask, 1000);
}

// Текущий уровень GPIO SQW
bool CLASS_MODULE_DS3231::getSqwLevel() {
    return (digitalRead(DS3231_SQW_PIN) == HIGH);
}

bool CLASS_MODULE_DS3231::getAlarmFired1() { return _alarm1Fired; }
bool CLASS_MODULE_DS3231::getAlarmFired2() { return _alarm2Fired; }
time_t CLASS_MODULE_DS3231::getLastAlarm1Time() { return _lastAlarm1At; }
time_t CLASS_MODULE_DS3231::getLastAlarm2Time() { return _lastAlarm2At; }

// Форматирование времени срабатывания: YYYY-MM-DD HH:MM:SS. Если время
// недостоверно (t==0, например OSF), возвращаем пометку "time invalid".
static void _formatAlarmTime(time_t t, String &out) {
    if (t == 0) { out = "(time invalid)"; return; }
    String dt = "";
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
    out = dt;
}

// Форматирование штампа времени срабатывания для отображения.
// Если времени нет (t==0 — не срабатывал), возвращаем "--".
static void _formatAlarmStamp(time_t t, String &out) {
    if (t == 0) { out = "--"; return; }
    _formatAlarmTime(t, out);
}

// Обработка срабатывания одного будильника: вывод сообщения с временем в терминал
// и автовзвод (перезапись) для периодических режимов mode=1..3.
// mode=0 (раз в секунду) — только краткое сообщение, перезапись не требуется.
// mode=4 (по дню/дате) — полное сообщение, но повторно в течение суток не взводим.
void CLASS_MODULE_DS3231::_handleAlarmFired(uint8_t alarmNum, uint8_t mode,
                                            uint8_t hour, uint8_t min, uint8_t sec,
                                            uint8_t dayOrDate, bool isDayOfWeek) {
    time_t t = _readTime();
    String when;
    _formatAlarmTime(t, when);

    // Сохраняем время последнего срабатывания (для всех режимов, включая mode=0).
    // Не сбрасывается при setAlarm1/setAlarm2 — обновляется только фактической сработкой.
    if (alarmNum == 1) { _lastAlarm1At = t; } else { _lastAlarm2At = t; }

    if (mode == 0) {
        // Раз в секунду — краткое сообщение без полной даты.
        DEBUGDS3231("DS3231: Alarm %d fired\r\n", alarmNum);
        return;
    }

    // Полная дата и время срабатывания для остальных режимов.
    DEBUGDS3231("DS3231: Alarm %d fired: %s\r\n", alarmNum, when.c_str());

    // Автовзвод только для периодических режимов mode=1..3 (не mode=0 и не mode=4).
    if (mode >= 1 && mode <= 3) {
        if (alarmNum == 1) {
            setAlarm1(hour, min, sec, mode, dayOrDate, isDayOfWeek);
        } else {
            setAlarm2(hour, min, mode, dayOrDate, isDayOfWeek);
        }
    }
}

// Проверка флагов регистра Status (0x0F): A1F(bit0), A2F(bit1)
void CLASS_MODULE_DS3231::checkAlarmFlags() {
    if (_config.addr == DS3231_ADDR_NONE) { return; }

    // Обработка сигнала от GPIO (если включён мониторинг)
    if (_config.sqwEnabled && _sqwIrqFlag) {
        checkSqw();
    }

    uint8_t stat = _readReg(0x0F);
    uint8_t a1f = (stat & 0x01) ? 1 : 0;
    uint8_t a2f = (stat & 0x02) ? 1 : 0;

    // Вывод сообщения по фронту (флаг перешёл из 0 в 1) — под флагом DEBUG_DS3231.
    // После срабатывания сбрасываем флаг, чтобы каждое новое срабатывание давало
    // сообщение. Для периодических режимов (mode=1..3) дополнительно перезаписываем
    // будильник (автовзвод), чтобы гарантировать новый фронт при следующем совпадении.
    if (a1f && !_alarm1Fired) {
        _alarm1Fired = true;
        stat &= ~0x01;
        _writeReg(0x0F, stat);

        uint8_t hour, min, sec, mode, dayOrDate;
        bool isDOW;
        if (getAlarm1(hour, min, sec, mode, dayOrDate, isDOW)) {
            _handleAlarmFired(1, mode, hour, min, sec, dayOrDate, isDOW);
        } else {
            DEBUGDS3231("DS3231: Alarm 1 fired (A1F=1)\r\n");
        }
    }
    if (a2f && !_alarm2Fired) {
        _alarm2Fired = true;
        stat &= ~0x02;
        _writeReg(0x0F, stat);

        uint8_t hour, min, mode, dayOrDate;
        bool isDOW;
        if (getAlarm2(hour, min, mode, dayOrDate, isDOW)) {
            _handleAlarmFired(2, mode, hour, min, 0, dayOrDate, isDOW);
        } else {
            DEBUGDS3231("DS3231: Alarm 2 fired (A2F=1)\r\n");
        }
    }
}

// Поля конфигурации SQW/GPIO для страницы. Вызывается handleRead/handlePoll
void CLASS_MODULE_DS3231::emitSqwFields(String &values) {
    values += "ds_sqw_enabled|" + String(_config.sqwEnabled ? "checked" : "") + "|chk\n";
    values += "ds_sqw_mode|"    + String(_config.sqwMode)                       + "|input\n";
    values += "ds_sqw_level|"   + String(_config.sqwLevelActive ? "1" : "0")    + "|input\n";
    values += "ds_sqw_level_read|" + String(getSqwLevel() ? "1" : "0")          + "|div\n";
    values += "ds_sqw_pin|"     + String(DS3231_SQW_PIN)                        + "|div\n";

    // Управляемые биты Control (0x0E)
    values += "ds_ctrl_bbsqw|"  + String(_config.ctrlBbsqw ? "1" : "0") + "|input\n";
    values += "ds_ctrl_rs|"     + String(_config.ctrlRs)               + "|input\n";
    values += "ds_ctrl_intcn|"  + String(_config.ctrlIntcn ? "1" : "0") + "|input\n";
    values += "ds_ctrl_a1ie|"   + String(_config.ctrlA1ie ? "checked" : "") + "|chk\n";
    values += "ds_ctrl_a2ie|"   + String(_config.ctrlA2ie ? "checked" : "") + "|chk\n";

    emitAlarmState(values);
}

// Статус сработавших будильников на основе сохранённого времени последнего срабатывания.
void CLASS_MODULE_DS3231::emitAlarmState(String &values) {
    bool has1 = (_lastAlarm1At != 0);
    bool has2 = (_lastAlarm2At != 0);
    String t1, t2;
    _formatAlarmStamp(_lastAlarm1At, t1);
    _formatAlarmStamp(_lastAlarm2At, t2);

    String st = "";
    if (has1 && has2) { st = "Alarm 1: " + t1 + " | Alarm 2: " + t2; }
    else if (has1)    { st = "Alarm 1 сработал: " + t1; }
    else if (has2)    { st = "Alarm 2 сработал: " + t2; }
    else              { st = "--"; }
    values += "ds_alarm_state|" + st + "|div\n";
    values += "ds_alarm_any|"   + String(((has1 || has2) ? "1" : "0")) + "|div\n";
}

#endif // ESP32

// ====================================================================
// begin()
// ====================================================================

void CLASS_MODULE_DS3231::begin() {
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

    TerminalRegisterModule(ds3231TerminalRegister);

#if defined(ESP32)
    // Применяем биты Control (0x0E), отвечающие за режим выхода SQW
    _applyCtrlBits();
    // Инициализируем GPIO для мониторинга вывода SQW/INT#
    sqwGpioInit();
    // Считываем флаги будильников в начальное состояние
    checkAlarmFlags();
#endif
}

void CLASS_MODULE_DS3231::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ====================================================================
// web_Init()
// ====================================================================

void CLASS_MODULE_DS3231::web_Init() {
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

void CLASS_MODULE_DS3231::handleRead(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";

    if (_config.addr == DS3231_ADDR_NONE) {
        values += "ds_state|disconnected|div\n";
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

    // SQW / будильники — проверяем флаги и выводим поля конфигурации
#if defined(ESP32)
    checkAlarmFlags();
    emitSqwFields(values);
#endif

    request->send(200, "text/plain", values);
}

void CLASS_MODULE_DS3231::handlePoll(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";
    // Автоопрос (autoPoll/pollInterval) — клиентский: интервал задаёт JS на
    // веб-странице, сервер только отдаёт текущее состояние по запросу.
    // Поэтому поля конфига интервала здесь не применяются.

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

    // SQW / будильники — проверяем флаги и выводим поля конфигурации
#if defined(ESP32)
    checkAlarmFlags();
    emitSqwFields(values);
#endif

    request->send(200, "text/plain", values);
}

void CLASS_MODULE_DS3231::handleSetTime(AsyncWebServerRequest *request) {
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

void CLASS_MODULE_DS3231::handleSetAlarm1(AsyncWebServerRequest *request) {
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

void CLASS_MODULE_DS3231::handleSetAlarm2(AsyncWebServerRequest *request) {
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

void CLASS_MODULE_DS3231::handleSetReg(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    if (_config.addr == DS3231_ADDR_NONE) { request->send(200, "text/plain", "DS3231 not connected"); return; }

    if (request->hasArg("ctrl")) {
        uint8_t val = (uint8_t)strtol(request->arg("ctrl").c_str(), NULL, 16);
        _writeReg(0x0E, val);
    } else if (request->hasArg("bbsqw") || request->hasArg("rs") ||
               request->hasArg("intcn") || request->hasArg("a1ie") || request->hasArg("a2ie")) {
        // Поразрядное управление битами Control (0x0E) — режим выхода SQW/INT
#if defined(ESP32)
        if (request->hasArg("bbsqw")) {
            _config.ctrlBbsqw = (request->arg("bbsqw") == "true" || request->arg("bbsqw") == "1");
        }
        if (request->hasArg("rs")) {
            uint8_t rs = (uint8_t)request->arg("rs").toInt();
            if (rs > 3) { rs = 3; }
            _config.ctrlRs = rs;
        }
        if (request->hasArg("intcn")) {
            _config.ctrlIntcn = (request->arg("intcn") == "true" || request->arg("intcn") == "1");
        }
        if (request->hasArg("a1ie")) {
            _config.ctrlA1ie = (request->arg("a1ie") == "true" || request->arg("a1ie") == "1");
        }
        if (request->hasArg("a2ie")) {
            _config.ctrlA2ie = (request->arg("a2ie") == "true" || request->arg("a2ie") == "1");
        }
        _applyCtrlBits();
        saveConfig();
#endif
    }
    if (request->hasArg("stat")) {
        uint8_t val = (uint8_t)strtol(request->arg("stat").c_str(), NULL, 16);
        _writeReg(0x0F, val);
        // После сброса флагов Status выводим сообщение о свободном состоянии
        DEBUGDS3231("DS3231: Status register written 0x%02X\r\n", val);
    }
#if defined(ESP32)
    checkAlarmFlags();
#endif
    request->send(200, "text/plain", "OK");
}

void CLASS_MODULE_DS3231::handleSaveConfig(AsyncWebServerRequest *request) {
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
#if defined(ESP32)
    if (request->hasArg("sqwEnabled")) {
        _config.sqwEnabled = (request->arg("sqwEnabled") == "true");
    }
    if (request->hasArg("sqwMode")) {
        uint8_t m = (uint8_t)request->arg("sqwMode").toInt();
        if (m != DS3231_SQW_MODE_POLLING && m != DS3231_SQW_MODE_INTERRUPT) { m = DS3231_SQW_MODE_POLLING; }
        _config.sqwMode = m;
    }
    if (request->hasArg("sqwLevel")) {
        _config.sqwLevelActive = (request->arg("sqwLevel") == "1" || request->arg("sqwLevel") == "true");
    }
    // Бит SQW — отдельная кнопка /set_reg, здесь не пишем.
#endif
    saveConfig();
#if defined(ESP32)
    // Переинициализация GPIO согласно новому конфигу
    sqwGpioReinit();
#endif
    request->send(200, "text/plain", "OK");
}

void CLASS_MODULE_DS3231::handleInfo(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ds_autoPoll|"      + String(_config.autoPoll ? "checked" : "") + "|chk\n";
    values += "ds_pollInterval|"  + String(_config.pollInterval)               + "|input\n";
    values += "ds_scanRetries|"   + String(DS3231_SCAN_RETRIES)               + "|div\n";
#if defined(ESP32)
    values += "ds_sqw_enabled|"   + String(_config.sqwEnabled ? "checked" : "") + "|chk\n";
    values += "ds_sqw_mode|"      + String(_config.sqwMode)                     + "|input\n";
    values += "ds_sqw_level|"     + String(_config.sqwLevelActive ? "1" : "0")  + "|input\n";
    values += "ds_sqw_pin|"       + String(DS3231_SQW_PIN)                      + "|div\n";
    values += "ds_ctrl_bbsqw|"    + String(_config.ctrlBbsqw ? "1" : "0")       + "|input\n";
    values += "ds_ctrl_rs|"       + String(_config.ctrlRs)                      + "|input\n";
    values += "ds_ctrl_intcn|"    + String(_config.ctrlIntcn ? "1" : "0")       + "|input\n";
    values += "ds_ctrl_a1ie|"     + String(_config.ctrlA1ie ? "checked" : "")   + "|chk\n";
    values += "ds_ctrl_a2ie|"     + String(_config.ctrlA2ie ? "checked" : "")   + "|chk\n";
#endif
    request->send(200, "text/plain", values);
}

// ====================================================================
// Конфиг
// ====================================================================

void CLASS_MODULE_DS3231::defaultConfig() {
    _config.addr         = DS3231_ADDR_NONE;
    _config.autoPoll     = false;
    _config.pollInterval = 5;
#if defined(ESP32)
    _config.sqwEnabled     = true;          // мониторинг SQW/INT# включён по умолчанию
    _config.sqwMode        = DS3231_SQW_MODE_POLLING;
    _config.sqwLevelActive = false;         // INT# активен по низкому уровню
    // Бит Control (0x0E): выход INT# по будильникам, меандр выключен
    _config.ctrlBbsqw = false;
    _config.ctrlRs     = 0;                 // 1Гц (при BBSQW=1)
    _config.ctrlIntcn  = true;              // INT# по будильникам
    _config.ctrlA1ie   = true;
    _config.ctrlA2ie   = true;
#endif
}

bool CLASS_MODULE_DS3231::loadConfig() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (ModClassJson.jsonFileLoadDoc(CONFIG_FILE_DS3231, doc) == false) { return false; }

    _config.addr         = doc["addr"].as<uint8_t>();
    _config.autoPoll     = doc["autoPoll"].as<bool>();
    _config.pollInterval = doc["pollInterval"].as<uint16_t>();
#if defined(ESP32)
    _config.sqwEnabled     = doc["sqwEnabled"].as<bool>();
    _config.sqwMode        = doc["sqwMode"].as<uint8_t>();
    _config.sqwLevelActive = doc["sqwLevelActive"].as<bool>();
    _config.ctrlBbsqw      = doc["ctrlBbsqw"].as<bool>();
    _config.ctrlRs         = doc["ctrlRs"].as<uint8_t>();
    _config.ctrlIntcn      = doc["ctrlIntcn"].as<bool>();
    _config.ctrlA1ie       = doc["ctrlA1ie"].as<bool>();
    _config.ctrlA2ie       = doc["ctrlA2ie"].as<bool>();
    if (_config.sqwMode != DS3231_SQW_MODE_POLLING && _config.sqwMode != DS3231_SQW_MODE_INTERRUPT) {
        _config.sqwMode = DS3231_SQW_MODE_POLLING;
    }
#endif

    DEBUGDS3231("addr: 0x%02X, autoPoll: %d, pollInterval: %d\r\n", _config.addr, _config.autoPoll, _config.pollInterval);
    return true;
}

bool CLASS_MODULE_DS3231::saveConfig() {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(CONFIG_FILE_DS3231, doc);
    doc["addr"]         = _config.addr;
    doc["autoPoll"]     = _config.autoPoll;
    doc["pollInterval"] = _config.pollInterval;
#if defined(ESP32)
    doc["sqwEnabled"]     = _config.sqwEnabled;
    doc["sqwMode"]        = _config.sqwMode;
    doc["sqwLevelActive"] = _config.sqwLevelActive;
    doc["ctrlBbsqw"]      = _config.ctrlBbsqw;
    doc["ctrlRs"]         = _config.ctrlRs;
    doc["ctrlIntcn"]      = _config.ctrlIntcn;
    doc["ctrlA1ie"]       = _config.ctrlA1ie;
    doc["ctrlA2ie"]       = _config.ctrlA2ie;
#endif
    return ModClassJson.jsonFileSaveDoc(CONFIG_FILE_DS3231, doc);
}

// ====================================================================
// Терминальные команды модуля
// ====================================================================

void ds3231CmdAlarm() {
    // Показать статус сработавших будильников (флаги A1F/A2F регистра Status)
    uint8_t stat = ModClassDs3231.getStatusReg();
    DEBUGDS3231("DS3231 Status=0x%02X A1F=%d A2F=%d\r\n",
                stat, (stat & 0x01) ? 1 : 0, (stat & 0x02) ? 1 : 0);
    if (stat & 0x01) { DEBUGDS3231("DS3231: Alarm 1 fired\r\n"); }
    if (stat & 0x02) { DEBUGDS3231("DS3231: Alarm 2 fired\r\n"); }
    if (!(stat & 0x03)) { DEBUGDS3231("DS3231: no alarm fired\r\n"); }
#if defined(ESP32)
    // Время последнего срабатывания (сохранённое в RAM).
    if (ModClassDs3231.getLastAlarm1Time() != 0) {
        String s; _formatAlarmStamp(ModClassDs3231.getLastAlarm1Time(), s);
        DEBUGDS3231("DS3231: Alarm 1 last fired: %s\r\n", s.c_str());
    }
    if (ModClassDs3231.getLastAlarm2Time() != 0) {
        String s; _formatAlarmStamp(ModClassDs3231.getLastAlarm2Time(), s);
        DEBUGDS3231("DS3231: Alarm 2 last fired: %s\r\n", s.c_str());
    }
    if (ModClassDs3231.getLastAlarm1Time() == 0 && ModClassDs3231.getLastAlarm2Time() == 0) {
        DEBUGDS3231("DS3231: no alarm fired since boot\r\n");
    }
#endif
}

void ds3231CmdSqw() {
    // Управление GPIO мониторинга SQW/INT#
#if defined(ESP32)
    String arg1 = term.getNext();
    DEBUGDS3231("ds-sqw arg=%s\r\n", arg1.c_str());
    if (arg1 == "on" || arg1 == "1") {
        ModClassDs3231.sqwGpioInit();
        ModClassDs3231._config.sqwEnabled = true;
        ModClassDs3231.saveConfig();
        Serial.println("SQW monitoring ON");
        return;
    }
    if (arg1 == "off" || arg1 == "0") {
        ModClassDs3231._config.sqwEnabled = false;
        ModClassDs3231.sqwGpioStop();
        ModClassDs3231.saveConfig();
        Serial.println("SQW monitoring OFF");
        return;
    }
    if (arg1 == "poll") {
        ModClassDs3231._config.sqwMode = DS3231_SQW_MODE_POLLING;
        ModClassDs3231.saveConfig();
        ModClassDs3231.sqwGpioReinit();
        Serial.println("SQW mode: polling");
        return;
    }
    if (arg1 == "int") {
        ModClassDs3231._config.sqwMode = DS3231_SQW_MODE_INTERRUPT;
        ModClassDs3231.saveConfig();
        ModClassDs3231.sqwGpioReinit();
        Serial.println("SQW mode: interrupt");
        return;
    }
    // По умолчанию печатаем состояние
    Serial.printf("SQW enabled=%d mode=%s level=%d pin D%d\r\n",
                  ModClassDs3231._config.sqwEnabled ? 1 : 0,
                  (ModClassDs3231._config.sqwMode == DS3231_SQW_MODE_INTERRUPT) ? "interrupt" : "polling",
                  ModClassDs3231._config.sqwLevelActive ? 1 : 0,
                  DS3231_SQW_PIN);
    Serial.printf("SQW raw=%d\r\n", digitalRead(DS3231_SQW_PIN));
#endif
#if defined(ESP8266)
    Serial.println("SQW GPIO available only on ESP32");
#endif
}

void ds3231CmdSqr() {
    // Показать/записать биты Control (0x0E) — режим выхода SQW
#if defined(ESP32)
    String arg1 = term.getNext();
    if (arg1 == "bbsqw") {
        ModClassDs3231._config.ctrlBbsqw = true;
        ModClassDs3231._config.ctrlIntcn = false;
        ModClassDs3231.saveConfig();
        ModClassDs3231._applyCtrlBits();
        Serial.println("SQW output: BBSQW=1 (меандр)");
        return;
    }
    if (arg1 == "intcn") {
        ModClassDs3231._config.ctrlIntcn = true;
        ModClassDs3231._config.ctrlBbsqw = false;
        ModClassDs3231.saveConfig();
        ModClassDs3231._applyCtrlBits();
        Serial.println("SQW output: INTCN=1 (INT# по будильникам)");
        return;
    }
    uint8_t ctrl = ModClassDs3231._readReg(0x0E);
    Serial.printf("Control=0x%02X BBSQW=%d RS=%d INTCN=%d A1IE=%d A2IE=%d\r\n",
                  ctrl,
                  (ctrl & 0x40) ? 1 : 0,
                  (ctrl >> 3) & 3,
                  (ctrl & 0x04) ? 1 : 0,
                  (ctrl & 0x01) ? 1 : 0,
                  (ctrl & 0x02) ? 1 : 0);
#endif
#if defined(ESP8266)
    Serial.println("SQW register control available only on ESP32");
#endif
}

void ds3231TerminalRegister() {
    term.addCommand("ds-alarm", ds3231CmdAlarm);
    term.addCommand("ds-sqw",   ds3231CmdSqw);
    term.addCommand("ds-sqr",   ds3231CmdSqr);
}

// ====================================================================
// Версионные методы
// ====================================================================

String CLASS_MODULE_DS3231::getVersionStr() {
    return String(MODULE_DS3231_VERSION);
}

String CLASS_MODULE_DS3231::getGeneratedTime() {
    return String(MODULE_DS3231_GENERATED_TIME);
}

String CLASS_MODULE_DS3231::getCommitDateStr() {
    return String(MODULE_DS3231_COMMIT_DATE_STR);
}

void CLASS_MODULE_DS3231::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGDS3231("%s\r\n", __FUNCTION__);
    String values = "";
    values += "ds3231version|" + getVersionStr()    + "|div\n";
    values += "ds3231gentime|" + getGeneratedTime() + "|div\n";
    values += "ds3231gendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
