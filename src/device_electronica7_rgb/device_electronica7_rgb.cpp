#include "core_web/FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"

#include "device_electronica7_rgb.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "device_electronica7_rgb_version.h"
#include "core_sys/eertos.h"

// ============================================================
// Глобальные объекты и переменные
// ============================================================

CLASS_DEVICE_E7RGB device_electronica7_rgb(false);

CLASS_DEVICE_E7RGB::CLASS_DEVICE_E7RGB(bool _in) {
    dumb = _in;
    _fs = NULL;
    _lastMinute = 0xFF;
    _ntpWasSynced = false;
    _forceRedraw = false;
    _pendingReinit = false;
    _pendingSave = false;
    _pendingApply = false;
    _pendingDataPin = -1;
}

// Forward-объявления свободных функций, используемых в шаблонном блоке
void e7rgbSecondTask();
static String e7TimeHhMm();
static uint32_t e7HexStringToUint32(const String& hexStr);
static String e7HexColor(uint32_t c);
static bool e7FontPathOk(const String& p);

// ============================================================
// setFs()
// ============================================================
void CLASS_DEVICE_E7RGB::setFs(fs::LittleFSFS* fs) {
    _fs = fs;
}

// ============================================================
// begin()
// ============================================================
void CLASS_DEVICE_E7RGB::begin() {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);

    _lastMinute = 0xFF;
    _ntpWasSynced = false;
    _forceRedraw = false;
    _pendingReinit = false;
    _pendingSave = false;
    _pendingApply = false;

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    if (_fs != NULL) { loadFont(); }

    initMatrix();
    applyMode();

    SetTimerTask(e7rgbSecondTask, 1000);
}

void CLASS_DEVICE_E7RGB::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ============================================================
// web_Init()
// Все эндпоинты — GET (только по необходимости возможен POST,
// для этого устройства ничего требующего POST нет).
// ============================================================
void CLASS_DEVICE_E7RGB::web_Init() {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/e7rgb/save", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    ESPHTTPServer.on("/e7rgb/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/e7rgb/fonts", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFonts(request);
    });

    ESPHTTPServer.on("/e7rgb/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_DEVICE_E7RGB::handleInfo(AsyncWebServerRequest *request) {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);
    String values = "";

    values += "mode|"          + String(_config.mode)          + "|input\n";
    values += "dataPin|"       + String(_config.dataPin)       + "|input\n";
    values += "brightness|"    + String(_config.brightness)    + "|input\n";
    values += "digitsColor|"   + e7HexColor(_config.digitsColor) + "|input\n";
    values += "origin|"        + String(_config.origin)        + "|input\n";
    values += "direction|"     + String(_config.direction)     + "|input\n";
    values += "layout|"        + String(_config.layout)        + "|input\n";
    values += "fontFile|"      + _config.fontFile              + "|input\n";
    values += "manualText|"    + _config.manualText            + "|input\n";

    bool synced = (NTP.getLastNTPSync() > 0);
    values += "x_ntp_sync|"    + String(synced ? 1 : 0)        + "|div\n";
    values += "x_time|"        + (synced ? e7TimeHhMm() : String("—")) + "|div\n";

    request->send(200, "text/plain", values);
}

void CLASS_DEVICE_E7RGB::handleSave(AsyncWebServerRequest *request) {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);

    if (request->args() == 0) { request->send(400, "text/plain", "No args"); return; }

    for (uint8_t i = 0; i < request->args(); i++) {
        String name = request->argName(i);
        String val  = request->arg(i);
        DEBUGE7RGB("Arg %d: %s %s\r\n", i, name.c_str(), val.c_str());

        if (name == "mode") {
            _config.mode = (uint8_t)constrain(val.toInt(), E7_MODE_WORK, E7_MODE_MANUAL);
            continue;
        }
        if (name == "dataPin") {
            int16_t pin = (int16_t)val.toInt();
            if (pin >= 0 && pin < 34 && pin != _config.dataPin) {
                _pendingDataPin = pin;
                _pendingReinit = true;
            }
            continue;
        }
        if (name == "brightness") {
            _config.brightness = (uint8_t)constrain(val.toInt(), 0, 255);
            continue;
        }
        if (name == "digitsColor") {
            _config.digitsColor = e7HexStringToUint32(val);
            continue;
        }
        if (name == "origin") {
            _config.origin = (uint8_t)constrain(val.toInt(), 0, 3);
            continue;
        }
        if (name == "direction") {
            _config.direction = (uint8_t)constrain(val.toInt(), 0, 3);
            continue;
        }
        if (name == "layout") {
            _config.layout = (uint8_t)constrain(val.toInt(), 0, 1);
            continue;
        }
        if (name == "fontFile") {
            if (e7FontPathOk(val)) { _config.fontFile = val; }
            continue;
        }
        if (name == "manualText") {
            // Ручной режим: ровно 4 символа из {'0'..'9','-',' '}
            String out;
            for (uint16_t i = 0; i < val.length() && out.length() < 4; i++) {
                char ch = val.charAt(i);
                if ((ch >= '0' && ch <= '9') || ch == '-' || ch == ' ') { out += ch; }
            }
            while (out.length() < 4) { out += ' '; }
            _config.manualText = out;
            continue;
        }
    }

    if (_pendingReinit) {
        _pendingApply = true;
    } else {
        _pendingSave = true;
        _pendingApply = true;
    }

    request->send(200, "text/plain", "OK");
    if (_pendingApply || _pendingSave) {
        SetTask(deferredApplyTask);
    }
}

void CLASS_DEVICE_E7RGB::handleFonts(AsyncWebServerRequest *request) {
    String out = "digital7.fnt\n";   // гарантия, что список не пуст
    if (_fs != NULL) {
        _fonts.listFonts(*_fs, E7_FONT_DIR, out);
        if (out.length() == 0) { out = "digital7.fnt\n"; }
    }
    request->send(200, "text/plain", out);
}

// Отложенное применение изменений (паттерн module_rgb):
// вызывается только из main-loop, поэтому Show() здесь безопасен.
void CLASS_DEVICE_E7RGB::deferredApplyTask() {
    CLASS_DEVICE_E7RGB& d = device_electronica7_rgb;

    if (d._pendingReinit) {
        if (d._pendingDataPin < 0) { d._pendingDataPin = d._config.dataPin; }
        d._config.dataPin = d._pendingDataPin;
        d.destroyMatrix();
        d.initMatrix();
        d.saveConfig();
        d._pendingReinit = false;
        d._pendingSave = false;
        d._pendingApply = false;
        if (d._fs != NULL) { d.loadFont(); }
        d.applyMode();
        return;
    }

    if (d._pendingSave) {
        d.saveConfig();
        d._pendingSave = false;
    }
    if (d._fs != NULL) { d.loadFont(); }

    if (d._pendingApply) {
        d.applyMode();
        d._pendingApply = false;
    }
}

// ============================================================
// Конфиг
// ============================================================

void CLASS_DEVICE_E7RGB::defaultConfig() {
    _config.mode        = E7_MODE_WORK;
    _config.dataPin     = 16;
    _config.brightness  = 25;
    _config.digitsColor = 0xFF0000;
    _config.origin      = E7_ORIGIN_BOTTOM_LEFT;   // спаянная матрица: первый LED внизу слева
    _config.direction   = E7_DIR_UP;               // порядок: снизу вверх, слева направо
    _config.layout      = E7_LAYOUT_PARALLEL;      // развёртка: параллельно (по умолчанию)
    _config.fontFile    = E7_FONT_DEFAULT;
    _config.manualText  = "0000";
}

bool CLASS_DEVICE_E7RGB::loadConfig() {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_E7RGB, doc) == false) { return false; }

    _config.mode        = (uint8_t)constrain(doc["mode"].as<int>(), E7_MODE_WORK, E7_MODE_MANUAL);
    _config.dataPin     = (int16_t)constrain(doc["dataPin"].as<int>(), -1, 33);
    _config.brightness  = (uint8_t)constrain(doc["brightness"].as<int>(), 0, 255);
    _config.digitsColor = doc["digitsColor"].as<uint32_t>() & 0xFFFFFF;
    _config.origin      = (uint8_t)constrain(doc["origin"].as<int>(), 0, 3);
    _config.direction   = (uint8_t)constrain(doc["direction"].as<int>(), 0, 3);
    _config.layout      = (uint8_t)constrain(doc["layout"].as<int>(), 0, 1);

    _config.fontFile = doc["fontFile"].as<String>();
    if (e7FontPathOk(_config.fontFile) == false) { _config.fontFile = E7_FONT_DEFAULT; }

    _config.manualText = doc["manualText"].as<String>();
    if (_config.manualText.length() != 4) { _config.manualText = "0000"; }

    DEBUGE7RGB("dataPin: %d, mode: %d, brightness: %d, font: %s\r\n",
               _config.dataPin, _config.mode, _config.brightness, _config.fontFile.c_str());
    return true;
}

bool CLASS_DEVICE_E7RGB::saveConfig() {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_E7RGB, doc);

    doc["mode"]        = _config.mode;
    doc["dataPin"]     = _config.dataPin;
    doc["brightness"]  = _config.brightness;
    doc["digitsColor"] = _config.digitsColor;
    doc["origin"]      = _config.origin;
    doc["direction"]   = _config.direction;
    doc["layout"]      = _config.layout;
    doc["fontFile"]    = _config.fontFile;
    doc["manualText"]  = _config.manualText;

    doc.remove("cells");   // старые поля координатной сетки не сохраняем

    return core_json.jsonFileSaveDoc(CONFIG_FILE_E7RGB, doc);
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_DEVICE_E7RGB::getVersionStr() {
    return String(DEVICE_ELECTRONICA7_RGB_VERSION);
}

String CLASS_DEVICE_E7RGB::getGeneratedTime() {
    return String(DEVICE_ELECTRONICA7_RGB_GENERATED_TIME);
}

String CLASS_DEVICE_E7RGB::getCommitDateStr() {
    return String(DEVICE_ELECTRONICA7_RGB_COMMIT_DATE_STR);
}

void CLASS_DEVICE_E7RGB::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGE7RGB("%s\r\n", __FUNCTION__);
    String values = "";
    values += "e7rgbversion|" + getVersionStr()    + "|div\n";
    values += "e7rgbgentime|" + getGeneratedTime() + "|div\n";
    values += "e7rgbgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

static bool e7FontPathOk(const String& p) {
    if (!p.startsWith(E7_FONT_DIR_SLASH) || !p.endsWith(".fnt")) { return false; }
    // Разрешаем только безопасные символы: управляющие, '|', '#', DEL и
    // не-ASCII ломают CVT-ответ /e7rgb/info или путь в ФС.
    for (unsigned int i = 0; i < p.length(); i++) {
        char c = p.charAt(i);
        if ((unsigned char)c < 0x20 || c == 0x7F || c == '|' || c == '#' || (unsigned char)c >= 0x80) {
            return false;
        }
    }
    return true;
}

static String e7TimeHhMm() {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour(), minute());
    return String(buf);
}

static String e7HexColor(uint32_t c) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%06X", (unsigned int)(c & 0xFFFFFF));
    return String(hex);
}

static uint32_t e7HexStringToUint32(const String& hexStr) {
    if (hexStr.length() == 0) { return 0; }
    String clean = hexStr;
    clean.replace("#", "");
    clean.replace("0x", "");
    clean.replace("0X", "");
    return (uint32_t)strtoul(clean.c_str(), NULL, 16);
}

bool CLASS_DEVICE_E7RGB::loadFont() {
    if (_fs == NULL) { return false; }
    if (e7FontPathOk(_config.fontFile) == false) { _config.fontFile = E7_FONT_DEFAULT; }
    // каталог шрифтов должен существовать для записи файла
    _fs->mkdir(E7_FONT_DIR);
    if (_fonts.loadOrCreate(*_fs, _config.fontFile.c_str())) { return true; }
    _fonts.loadDefault();
    return false;
}

void CLASS_DEVICE_E7RGB::initMatrix() {
    _matrix.setTopology(_config.origin, _config.direction, _config.layout);
    _matrix.init(_config.dataPin);
}

void CLASS_DEVICE_E7RGB::destroyMatrix() {
    _matrix.destroy();
}

void CLASS_DEVICE_E7RGB::applyMode() {
    if (_matrix.ready() == false) { return; }
    // Применяем актуальную топологию при каждой отрисовке: изменения
    // origin/direction/layout из веб-настроек вступают в силу сразу.
    _matrix.setTopology(_config.origin, _config.direction, _config.layout);
    if (_config.mode == E7_MODE_WORK) {
        redrawFrame();
    } else {
        renderManual();
    }
}

void CLASS_DEVICE_E7RGB::redrawFrame() {
    if (_matrix.ready() == false) { return; }
    if (NTP.getLastNTPSync() > 0) {
        renderDigits((uint8_t)hour(), (uint8_t)minute());
    } else {
        // NTP ещё не синхронизирован — прочерки («нет времени»)
        renderNoTime();
    }
}

void CLASS_DEVICE_E7RGB::renderDigits(uint8_t h, uint8_t m) {
    if (_matrix.ready() == false) { return; }
    _matrix.clear();
    uint8_t digits[4];
    digits[0] = h / 10;
    digits[1] = h % 10;
    digits[2] = m / 10;
    digits[3] = m % 10;
    for (uint8_t d = 0; d < 4; d++) {
        char ch = (char)('0' + (digits[d] % 10));
        drawGlyphAt((uint8_t)(d * E7_GLYPH_W), _fonts.glyph(ch));
    }
    _matrix.show();
}

void CLASS_DEVICE_E7RGB::renderNoTime() {
    if (_matrix.ready() == false) { return; }
    _matrix.clear();
    for (uint8_t d = 0; d < 4; d++) {
        drawGlyphAt((uint8_t)(d * E7_GLYPH_W), _fonts.glyph('-'));
    }
    _matrix.show();
}

// Ручной режим: показывает 4 символа из _config.manualText
// ('0'..'9', '-' = прочерк, ' ' = пусто) для визуальной проверки цифр/шрифта.
void CLASS_DEVICE_E7RGB::renderManual() {
    if (_matrix.ready() == false) { return; }
    _matrix.clear();
    for (uint8_t d = 0; d < 4; d++) {
        char ch = (d < _config.manualText.length()) ? _config.manualText.charAt(d) : ' ';
        if (ch == ' ') { continue; }
        drawGlyphAt((uint8_t)(d * E7_GLYPH_W), _fonts.glyph(ch));
    }
    _matrix.show();
}

void CLASS_DEVICE_E7RGB::drawGlyphAt(uint8_t x0, const uint8_t* mask) {
    if (mask == NULL) { return; }
    // Маска хранится как [ряд][колонка] значениями 0/1 (см. e7rgb_font.h);
    // ряд 0 = верх глифа -> верх панели (y = E7_HEIGHT - 1).
    for (int r = 0; r < E7_GLYPH_H; r++) {
        for (int c = 0; c < E7_GLYPH_W; c++) {
            if (mask[r * E7_GLYPH_W + c]) {
                _matrix.setPixel((int)x0 + c, (E7_HEIGHT - 1) - r,
                                 _config.digitsColor, _config.brightness);
            }
        }
    }
}

// ============================================================
// Периодическая задача (1 сек, main-loop)
// Show() вызывается только здесь или в deferredApplyTask.
// ============================================================
void e7rgbSecondTask() {
    CLASS_DEVICE_E7RGB& d = device_electronica7_rgb;

    if (d._config.mode == E7_MODE_WORK) {
        bool synced = (NTP.getLastNTPSync() > 0);
        if (synced && !d._ntpWasSynced) {
            d._ntpWasSynced = true;
            d._forceRedraw = true;
        }
        uint8_t m = (uint8_t)minute();
        if (d._forceRedraw || m != d._lastMinute) {
            d._forceRedraw = false;
            d._lastMinute = m;
            d.redrawFrame();
        }
    }

    SetTimerTask(e7rgbSecondTask, 1000);
}
