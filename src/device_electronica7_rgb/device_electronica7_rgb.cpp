#include "core_web/FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"

#include "device_electronica7_rgb.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "device_electronica7_rgb_version.h"
#include "core_sys/eertos.h"

#include <math.h>

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
    _animPhase = 0.0f;
    _fxLap = 0;
    for (int i = 0; i < E7_FX_MAX_COLORS; i++) { _fxOrder[i] = (uint8_t)i; }
}

// Forward-объявления свободных функций, используемых в шаблонном блоке
void e7rgbSecondTask();
void e7rgbAnimTask();
static void e7SeedRng();
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
    _fxLap = 0;
    for (int i = 0; i < E7_FX_MAX_COLORS; i++) { _fxOrder[i] = (uint8_t)i; }
    e7SeedRng();

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    if (_fs != NULL) { loadFont(); }

    initMatrix();
    applyMode();

    _animPhase = 0.0f;
    SetTimerTask(e7rgbSecondTask, 1000);
    SetTimerTask(e7rgbAnimTask, E7RGB_ANIM_MS);
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
    values += "effect|"        + String(_config.effect)        + "|input\n";
    values += "effectDir|"     + String(_config.effectDir)     + "|input\n";
    values += "digitsColor|"   + e7HexColor(_config.digitsColor) + "|input\n";
    values += "digitsColor2|"  + e7HexColor(_config.digitsColor2) + "|input\n";
    values += "animSpeed|"     + String(_config.animSpeed)     + "|input\n";
    values += "colorsCount|"   + String(_config.colorsCount)   + "|input\n";
    values += "cycleMode|"     + String(_config.cycleMode)     + "|input\n";
    for (uint8_t i = 0; i < E7_FX_MAX_COLORS; i++) {
        String pn = "palette" + String(i);
        values += pn + "|" + e7HexColor(_config.palette[i]) + "|input\n";
    }
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
        if (name == "effect") {
            _config.effect = (uint8_t)constrain(val.toInt(), E7_EFFECT_MONO, E7_EFFECT_COLORCYCLE);
            continue;
        }
        if (name == "effectDir") {
            _config.effectDir = (uint8_t)constrain(val.toInt(), 0, 5);
            continue;
        }
        if (name == "colorsCount") {
            _config.colorsCount = (uint8_t)constrain(val.toInt(), 1, E7_FX_MAX_COLORS);
            continue;
        }
        if (name == "cycleMode") {
            _config.cycleMode = (uint8_t)constrain(val.toInt(), E7_CYCLE_SEQUENTIAL, E7_CYCLE_RAINBOW);
            continue;
        }
        if (name.startsWith("palette")) {
            int idx = name.substring(7).toInt();   // palette0..palette7
            if (idx >= 0 && idx < E7_FX_MAX_COLORS) {
                _config.palette[idx] = e7HexStringToUint32(val);
            }
            continue;
        }
        if (name == "digitsColor") {
            _config.digitsColor = e7HexStringToUint32(val);
            continue;
        }
        if (name == "digitsColor2") {
            _config.digitsColor2 = e7HexStringToUint32(val);
            continue;
        }
        if (name == "animSpeed") {
            _config.animSpeed = (uint8_t)constrain(val.toInt(), 1, 50);
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
    _config.effect      = E7_EFFECT_MONO;
    _config.effectDir   = E7_FXDIR_TL;
    _config.digitsColor = 0xFF0000;
    _config.digitsColor2 = 0x0000FF;
    _config.animSpeed   = 45;
    _config.colorsCount = 4;
    _config.cycleMode   = E7_CYCLE_SEQUENTIAL;
    _config.palette[0]  = 0xFF0000;
    _config.palette[1]  = 0xFFFF00;
    _config.palette[2]  = 0x00FF00;
    _config.palette[3]  = 0x00FFFF;
    _config.palette[4]  = 0x0000FF;
    _config.palette[5]  = 0x8000FF;
    _config.palette[6]  = 0xFF8000;
    _config.palette[7]  = 0xFFFFFF;
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
    _config.effect      = (uint8_t)constrain(doc["effect"].as<int>(), E7_EFFECT_MONO, E7_EFFECT_COLORCYCLE);
    _config.effectDir   = (uint8_t)constrain(doc["effectDir"].as<int>(), 0, 5);
    _config.digitsColor = doc["digitsColor"].as<uint32_t>() & 0xFFFFFF;
    _config.digitsColor2 = doc["digitsColor2"].as<uint32_t>() & 0xFFFFFF;
    _config.animSpeed   = (uint8_t)constrain(doc["animSpeed"].as<int>(), 1, 50);
    _config.colorsCount = (uint8_t)constrain(doc["colorsCount"].as<int>(), 1, E7_FX_MAX_COLORS);
    _config.cycleMode   = (uint8_t)constrain(doc["cycleMode"].as<int>(), E7_CYCLE_SEQUENTIAL, E7_CYCLE_RAINBOW);

    if (doc["palette"].is<JsonArray>()) {
        JsonArray arr = doc["palette"].as<JsonArray>();
        for (uint8_t i = 0; i < E7_FX_MAX_COLORS; i++) {
            if (i < arr.size()) { _config.palette[i] = arr[i].as<uint32_t>() & 0xFFFFFF; }
        }
    }
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
    doc["effect"]      = _config.effect;
    doc["effectDir"]   = _config.effectDir;
    doc["digitsColor"] = _config.digitsColor;
    doc["digitsColor2"] = _config.digitsColor2;
    doc["animSpeed"]   = _config.animSpeed;
    doc["colorsCount"] = _config.colorsCount;
    doc["cycleMode"]   = _config.cycleMode;

    JsonArray palArr = doc["palette"].to<JsonArray>();
    palArr.clear();
    for (uint8_t i = 0; i < E7_FX_MAX_COLORS; i++) {
        palArr.add(_config.palette[i]);
    }

    doc.remove("cells");   // старые поля координатной сетки не сохраняем
    doc["origin"]      = _config.origin;
    doc["direction"]   = _config.direction;
    doc["layout"]      = _config.layout;
    doc["fontFile"]    = _config.fontFile;
    doc["manualText"]  = _config.manualText;

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

// ============================================================
// Помощники спецэффектов окраски
// ============================================================

static uint32_t e7HsvToRgb(float h, float s, float v) {
    while (h < 0.0f) { h += 360.0f; }
    while (h >= 360.0f) { h -= 360.0f; }
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (h < 60.0f)      { r = c; g = x; }
    else if (h < 120.0f) { r = x; g = c; }
    else if (h < 180.0f) { g = c; b = x; }
    else if (h < 240.0f) { g = x; b = c; }
    else if (h < 300.0f) { r = x; b = c; }
    else                { r = c; b = x; }
    uint8_t r8 = (uint8_t)((r + m) * 255.0f);
    uint8_t g8 = (uint8_t)((g + m) * 255.0f);
    uint8_t b8 = (uint8_t)((b + m) * 255.0f);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8;
}

static uint32_t e7LerpColor(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) { return c1; }
    if (t >= 1.0f) { return c2; }
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Простой xorshift-ГПСЧ для «случайного» порядка палитры
static uint32_t e7Rng = 0x9E3779B9;
static void e7SeedRng() {
    e7Rng = micros() ^ 0x9E3779B9;
    if (e7Rng == 0) { e7Rng = 0x12345678; }
}
static uint32_t e7Rand() {
    e7Rng ^= e7Rng << 13;
    e7Rng ^= e7Rng >> 17;
    e7Rng ^= e7Rng << 5;
    return e7Rng;
}
static void e7ShuffleOrder(uint8_t* arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(e7Rand() % (uint32_t)(i + 1));
        uint8_t t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

// Нормированная позиция (0..1) вдоль оси выбранного направления
static float e7PosOnAxis(int x, int y, uint8_t dir) {
    float fx = x / (float)(E7_WIDTH - 1);
    float fy = y / (float)(E7_HEIGHT - 1);
    switch (dir) {
        case E7_FXDIR_TL: return (fx + fy) * 0.5f;
        case E7_FXDIR_TR: return ((1.0f - fx) + fy) * 0.5f;
        case E7_FXDIR_BL: return (fx + (1.0f - fy)) * 0.5f;
        case E7_FXDIR_BR: return ((1.0f - fx) + (1.0f - fy)) * 0.5f;
        case E7_FXDIR_TB: return fy;
        case E7_FXDIR_BT: return 1.0f - fy;
    }
    return fy;
}

// Цвет пикселя (x,y) под текущим эффектом; phaseDeg — фаза анимации 0..360.
// order — текущий порядок обхода палитры (только для плавной смены цвета).
static uint32_t e7FxColor(const strE7RgbConfig& cfg, int x, int y, float phaseDeg, const uint8_t* order) {
    switch (cfg.effect) {
        case E7_EFFECT_RAINBOW: {
            float t = e7PosOnAxis(x, y, cfg.effectDir);
            float hue = fmodf(phaseDeg + t * 300.0f, 360.0f);
            return e7HsvToRgb(hue, 1.0f, 1.0f);
        }
        case E7_EFFECT_GRAD_STATIC: {
            float t = e7PosOnAxis(x, y, cfg.effectDir);
            return e7LerpColor(cfg.digitsColor, cfg.digitsColor2, t);
        }
        case E7_EFFECT_GRAD_DYNAMIC: {
            float t = e7PosOnAxis(x, y, cfg.effectDir);
            float ph = fmodf(phaseDeg, 360.0f) / 360.0f;
            float w = 0.5f - 0.5f * cosf(6.2831853f * (t - ph));
            return e7LerpColor(cfg.digitsColor, cfg.digitsColor2, w);
        }
        case E7_EFFECT_COLORCYCLE:
            // Плавная смена цвета: весь экран в одном плавно меняющемся цвете
            if (cfg.cycleMode == E7_CYCLE_RAINBOW) {
                return e7HsvToRgb(phaseDeg, 1.0f, 1.0f);
            }
            {
                uint8_t n = (cfg.colorsCount < 1) ? 1 : (cfg.colorsCount > E7_FX_MAX_COLORS ? E7_FX_MAX_COLORS : cfg.colorsCount);
                float segLen = 360.0f / (float)n;
                int seg = (int)(phaseDeg / segLen);
                if (seg >= n) { seg = n - 1; }
                float frac = phaseDeg - (float)seg * segLen;
                if (frac > segLen) { frac = segLen; }
                float t = frac / segLen;
                // плавный переход (сглаживание синусом)
                t = 0.5f - 0.5f * cosf(3.14159265f * t);
                int iFrom, iTo;
                if (cfg.cycleMode == E7_CYCLE_RANDOM && order != NULL) {
                    iFrom = order[seg];
                    iTo = order[(seg + 1) % n];
                } else {
                    iFrom = seg;
                    iTo = (seg + 1) % n;
                }
                return e7LerpColor(cfg.palette[iFrom], cfg.palette[iTo], t);
            }
        case E7_EFFECT_MONO:
        default:
            return cfg.digitsColor;
    }
}

static bool e7FxAnimated(uint8_t effect) {
    return (effect == E7_EFFECT_RAINBOW || effect == E7_EFFECT_GRAD_DYNAMIC ||
            effect == E7_EFFECT_COLORCYCLE);
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
    // Цвет каждой lit-клетки определяется текущим спецэффектом.
    for (int r = 0; r < E7_GLYPH_H; r++) {
        for (int c = 0; c < E7_GLYPH_W; c++) {
            if (mask[r * E7_GLYPH_W + c]) {
                int px = (int)x0 + c;
                int py = (E7_HEIGHT - 1) - r;
                uint32_t color = e7FxColor(_config, px, py, _animPhase, _fxOrder);
                _matrix.setPixel(px, py, color, _config.brightness);
            }
        }
    }
}

// ============================================================
// Периодическая задача анимации эффектов (main-loop).
// Двигает фазу и перерисовывает кадр для динамических эффектов
// (радуга, динамический градиент). Show() безопасен: loop-контекст.
// ============================================================
void e7rgbAnimTask() {
    CLASS_DEVICE_E7RGB& d = device_electronica7_rgb;

    if (e7FxAnimated(d._config.effect)) {
        float next = d._animPhase + (float)d._config.animSpeed;
        if (next >= 360.0f) {
            next -= 360.0f;
            d._fxLap++;
            // при «случайном» обходе палитры каждый круг — новый порядок
            if (d._config.effect == E7_EFFECT_COLORCYCLE && d._config.cycleMode == E7_CYCLE_RANDOM) {
                uint8_t n = d._config.colorsCount;
                if (n < 1) { n = 1; }
                if (n > E7_FX_MAX_COLORS) { n = E7_FX_MAX_COLORS; }
                e7ShuffleOrder(d._fxOrder, n);
            }
        }
        d._animPhase = next;
        d.applyMode();
    }

    SetTimerTask(e7rgbAnimTask, E7RGB_ANIM_MS);
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
