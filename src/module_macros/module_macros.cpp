#include "core_web/FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"
#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#include "module_macros.h"
#include "common/common.h"
#include "module_macros_version.h"
#include "core_sys/eertos.h"

#include "common/TimeLib.h"

CLASS_MODULE_MACROS module_macros(false);
CLASS_MODULE_MACROS::CLASS_MODULE_MACROS(bool _in) {
    dumb = _in;
    _ruleCount = 0;
    _lastFiredAt = 0;
}

// Forward declarations — свободные функции, используемые логикой модуля
static bool cronFieldParse(const String& token, uint8_t maxVal, CronField& f);
static bool cronFieldMatch(uint8_t val, const CronField& f);

#if defined(ESP32)
void CLASS_MODULE_MACROS::setFs(fs::LittleFSFS* fs)
#endif
#if defined(ESP8266)
void CLASS_MODULE_MACROS::setFs(FS* fs)
#endif
{
    _fs = fs;
}

// ============================================================
// begin()
// ============================================================
void CLASS_MODULE_MACROS::begin() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    defaultConfig();
    if (loadConfig() == false) { saveConfig(); }

    // Сценарий: если файла нет — создаём пример, затем разбираем
    if (_config.scenarioFile.length() == 0) { _config.scenarioFile = SCENARIO_FILE_DFLT; }
    if (_fs->exists(_config.scenarioFile) == false) { writeDefaultScenario(); }
    parseScenario();

    TerminalRegisterModule(macroTerminalRegister);

    // Периодическая проверка cron-расписаний (раз в секунду)
    SetTimerTask(CLASS_MODULE_MACROS::tick, 1000);
}

void CLASS_MODULE_MACROS::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_MODULE_MACROS::web_Init() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    // AJAX — получение списка правил и статуса
    ESPHTTPServer.on("/macros/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    // AJAX — перечитать файл сценария
    ESPHTTPServer.on("/macros/reload", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleReload(request);
    });

    // AJAX — запустить правило вручную
    ESPHTTPServer.on("/macros/run", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleRun(request);
    });

    // AJAX — сохранить настройки (enabled, файл сценария)
    ESPHTTPServer.on("/macros/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSave(request);
    });

    // Версия модуля
    ESPHTTPServer.on("/macros/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_MODULE_MACROS::handleInfo(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    String values = "";
    values += "mac_enabled|"    + String(_config.enabled ? "checked" : "") + "|chk\n";
    values += "mac_scenfile|"   + _config.scenarioFile                      + "|input\n";
    values += "mac_count|"      + String(_ruleCount)                        + "|div\n";

    String timeDate = "NTP not synced";
    if (NTP.getLastNTPSync() > 0) { timeDate = NTP.getTimeDateString(); }
    values += "mac_ntp|" + timeDate + "|div\n";

    String last = "--";
    if (_lastFiredAt > 0) {
        time_t t = (time_t)_lastFiredAt;
        char buf[24];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour(t), minute(t), second(t));
        last = _lastFiredName + " (" + String(buf) + ")";
    }
    values += "mac_last|" + last + "|div\n";

    for (uint8_t i = 0; i < _ruleCount; i++) {
        String p = "mac_" + String(i) + "_";
        String typeStr = "cron";
        if (_rules[i].type == MACRO_TRIG_BUTTON) { typeStr = "button"; }
        if (_rules[i].type == MACRO_TRIG_TERM)   { typeStr = "term"; }

        String trig = _rules[i].id;
        if (_rules[i].type == MACRO_TRIG_CRON) { trig = _rules[i].cronExpr; }

        values += p + "type|"   + typeStr            + "|div\n";
        values += p + "trig|"   + trig               + "|div\n";
        values += p + "action|" + _rules[i].action   + "|div\n";
    }

    request->send(200, "text/plain", values);
}

void CLASS_MODULE_MACROS::handleReload(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    parseScenario();
    request->send(200, "text/plain", "OK");
}

void CLASS_MODULE_MACROS::handleRun(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    if (request->hasArg("id")) {
        String id = urldecode(request->arg("id"));
        id.trim();
        if (fireById(id)) { request->send(200, "text/plain", "OK"); return; }
        request->send(200, "text/plain", "Not found");
        return;
    }
    request->send(200, "text/plain", "Missing id");
}

void CLASS_MODULE_MACROS::handleSave(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (request->hasArg("enabled")) {
        _config.enabled = (request->arg("enabled") == "true");
    }
    if (request->hasArg("scenfile")) {
        String f = urldecode(request->arg("scenfile"));
        f.trim();
        if (f.length() > 0) {
            if (f[0] != '/') { f = "/" + f; }
            _config.scenarioFile = f;
        }
    }

    saveConfig();
    parseScenario();
    request->send(200, "text/plain", "OK");
}

// ============================================================
// Конфиг
// ============================================================

void CLASS_MODULE_MACROS::defaultConfig() {
    _config.enabled = true;
    _config.scenarioFile = SCENARIO_FILE_DFLT;
}

bool CLASS_MODULE_MACROS::loadConfig() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_MACROS, doc) == false) { return false; }

    _config.enabled      = doc["enabled"].as<bool>();
    _config.scenarioFile = doc["scenarioFile"].as<String>();
    if (_config.scenarioFile.length() == 0) { _config.scenarioFile = SCENARIO_FILE_DFLT; }

    DEBUGMACROS("enabled: %d, scenarioFile: %s\r\n", _config.enabled, _config.scenarioFile.c_str());
    return true;
}

bool CLASS_MODULE_MACROS::saveConfig() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_MACROS, doc);
    doc["enabled"]      = _config.enabled;
    doc["scenarioFile"] = _config.scenarioFile;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_MACROS, doc);
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_MODULE_MACROS::getVersionStr() {
    return String(MODULE_MACROS_VERSION);
}

String CLASS_MODULE_MACROS::getGeneratedTime() {
    return String(MODULE_MACROS_GENERATED_TIME);
}

String CLASS_MODULE_MACROS::getCommitDateStr() {
    return String(MODULE_MACROS_COMMIT_DATE_STR);
}

void CLASS_MODULE_MACROS::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    String values = "";
    values += "macrosversion|" + getVersionStr()    + "|div\n";
    values += "macrosgentime|" + getGeneratedTime() + "|div\n";
    values += "macrosgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// Публичное API

uint8_t CLASS_MODULE_MACROS::getRuleCount() {
    return _ruleCount;
}

// Вывод списка правил в терминал
void CLASS_MODULE_MACROS::printRules() {
    Serial.printf("[MACRO] rules: %d, enabled: %d, file: %s\r\n",
                  _ruleCount,
                  _config.enabled ? 1 : 0,
                  _config.scenarioFile.c_str());
    for (uint8_t i = 0; i < _ruleCount; i++) {
        const char* typeStr = "cron";
        if (_rules[i].type == MACRO_TRIG_BUTTON) { typeStr = "button"; }
        if (_rules[i].type == MACRO_TRIG_TERM)   { typeStr = "term"; }
        String trig = (_rules[i].type == MACRO_TRIG_CRON) ? _rules[i].cronExpr : _rules[i].id;
        Serial.printf("  [%d] %s(%s) -> %s\r\n", i, typeStr, trig.c_str(), _rules[i].action.c_str());
    }
}

// Перечитать файл сценария
bool CLASS_MODULE_MACROS::reloadScenario() {
    parseScenario();
    return true;
}

// Запустить правило по id (button/term)
bool CLASS_MODULE_MACROS::fireById(const String& id) {
    if (id.length() == 0) { return false; }
    for (uint8_t i = 0; i < _ruleCount; i++) {
        if (_rules[i].type != MACRO_TRIG_CRON && _rules[i].id == id) {
            fireRule(i);
            return true;
        }
    }
    DEBUGMACROS("[MACRO] rule '%s' not found\r\n", id.c_str());
    return false;
}

// Запустить правила по типу триггера и токену (term)
bool CLASS_MODULE_MACROS::fireByType(uint8_t type, const String& id) {
    if (id.length() == 0) { return false; }
    bool fired = false;
    for (uint8_t i = 0; i < _ruleCount; i++) {
        if (_rules[i].type == type && _rules[i].id == id) {
            fireRule(i);
            fired = true;
        }
    }
    return fired;
}

// Срабатывание правила: результат выводится в терминал
void CLASS_MODULE_MACROS::fireRule(uint8_t idx) {
    MacroRule& r = _rules[idx];

    char stamp[24];
    time_t t = now();
    snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d", hour(t), minute(t), second(t));

    const char* typeStr = "cron";
    if (r.type == MACRO_TRIG_BUTTON) { typeStr = "button"; }
    if (r.type == MACRO_TRIG_TERM)   { typeStr = "term"; }

    Serial.printf("[MACRO] %s %s(%s) -> %s\r\n",
                  stamp, typeStr,
                  (r.type == MACRO_TRIG_CRON) ? r.cronExpr.c_str() : r.id.c_str(),
                  r.action.c_str());

    _lastFiredName = (r.type == MACRO_TRIG_CRON) ? r.cronExpr : r.id;
    _lastFiredAt = (uint32_t)t;
}

// ============================================================
// Сценарий: файл -> правила
// ============================================================

// Создать файл сценария по умолчанию (если его ещё нет)
void CLASS_MODULE_MACROS::writeDefaultScenario() {
    DEBUGMACROS("%s: creating default scenario %s\r\n", __FUNCTION__, _config.scenarioFile.c_str());
    File f = _fs->open(_config.scenarioFile, "w");
    if (!f) {
        DEBUGMACROS("%s: cannot create %s\r\n", __FUNCTION__, _config.scenarioFile.c_str());
        return;
    }
    f.print("# Example scenario for module_macros (prototype)\r\n");
    f.print("# Triggers: cron <6-field expression> : <action>\r\n");
    f.print("#           button <name> : <action>\r\n");
    f.print("#           term <word> : <action>\r\n");
    f.print("# cron: <sec> <min> <hour> <dom> <month> <dow> (5 fields - no seconds)\r\n");
    f.print("# The fired action is printed to the serial terminal.\r\n");
    f.print("\r\n");
    f.print("cron */10 * * * * * : Cron fired: every 10 seconds\r\n");
    f.print("cron 0 * * * * * : Cron fired: every minute\r\n");
    f.print("button test : Button test pressed on the web page\r\n");
    f.print("term hello : Got 'hello' message from the terminal\r\n");
    f.close();
}

// Разобрать файл сценария построчно
void CLASS_MODULE_MACROS::parseScenario() {
    DEBUGMACROS("%s: %s\r\n", __FUNCTION__, _config.scenarioFile.c_str());
    _ruleCount = 0;

    File f = _fs->open(_config.scenarioFile, "r");
    if (!f) {
        DEBUGMACROS("%s: cannot open %s\r\n", __FUNCTION__, _config.scenarioFile.c_str());
        return;
    }

    while (f.available() && _ruleCount < MACRO_MAX_RULES) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) { continue; }
        if (line[0] == '#') { continue; }

        MacroRule rule;
        if (parseRuleLine(line, rule)) {
            _rules[_ruleCount++] = rule;
        } else {
            DEBUGMACROS("%s: skip line: %s\r\n", __FUNCTION__, line.c_str());
        }
    }
    f.close();

    DEBUGMACROS("%s: %d rules loaded\r\n", __FUNCTION__, _ruleCount);
}

// Разобрать одну строку сценария: "<триггер> : <действие>"
bool CLASS_MODULE_MACROS::parseRuleLine(const String& line, MacroRule& rule) {
    int colon = line.indexOf(':');
    if (colon < 0) { return false; }

    String trig = line.substring(0, colon);
    String action = line.substring(colon + 1);
    trig.trim();
    action.trim();
    if (action.length() == 0) { return false; }

    rule.action = action;
    rule.lastFire = 0;
    memset(rule.cron, 0, sizeof(rule.cron));
    rule.hasSeconds = false;
    rule.cronExpr = "";

    // cron <выражение> : <действие>
    if (trig.startsWith("cron ")) {
        String expr = trig.substring(5);
        expr.trim();
        if (expr.length() == 0) { return false; }

        // Делим выражение на слова (поля)
        String fields[6];
        uint8_t n = 0;
        int startIdx = 0;
        while (startIdx <= expr.length() && n < 6) {
            int sp = expr.indexOf(' ', startIdx);
            String w = (sp < 0) ? expr.substring(startIdx) : expr.substring(startIdx, sp);
            w.trim();
            if (w.length() > 0) { fields[n++] = w; }
            if (sp < 0) { break; }
            startIdx = sp + 1;
        }

        // 5 полей: мин час день месяц день-нед; 6 полей: + секунды
        static const uint8_t maxVals[6] = {59, 59, 23, 31, 12, 6};
        uint8_t fieldCount = n;
        if (fieldCount != 5 && fieldCount != 6) {
            DEBUGMACROS("%s: bad cron field count (%d): %s\r\n", __FUNCTION__, n, expr.c_str());
            return false;
        }
        rule.hasSeconds = (fieldCount == 6);
        rule.cronExpr = expr;
        rule.type = MACRO_TRIG_CRON;

        uint8_t offset = rule.hasSeconds ? 0 : 1;
        for (uint8_t i = 0; i < fieldCount; i++) {
            if (cronFieldParse(fields[i], maxVals[offset + i], rule.cron[offset + i]) == false) {
                DEBUGMACROS("%s: bad cron field '%s'\r\n", __FUNCTION__, fields[i].c_str());
                return false;
            }
        }
        return true;
    }

    // button <имя> : <действие>
    if (trig.startsWith("button ")) {
        String id = trig.substring(7);
        id.trim();
        if (id.length() == 0) { return false; }
        rule.type = MACRO_TRIG_BUTTON;
        rule.id = id;
        return true;
    }

    // term <слово> : <действие>
    if (trig.startsWith("term ")) {
        String id = trig.substring(5);
        id.trim();
        if (id.length() == 0) { return false; }
        rule.type = MACRO_TRIG_TERM;
        rule.id = id;
        return true;
    }

    return false;
}

// ============================================================
// Cron: парсинг и проверка совпадения
// ============================================================

// Разобрать одно поле cron-выражения. Поддерживает: *, */n, число, a-b, a-b/n, список через запятую
static bool cronFieldParse(const String& token, uint8_t maxVal, CronField& f) {
    f.count = 0;
    f.maxVal = maxVal;

    int startIdx = 0;
    while (startIdx <= token.length()) {
        int commaIdx = token.indexOf(',', startIdx);
        String part = (commaIdx < 0) ? token.substring(startIdx) : token.substring(startIdx, commaIdx);
        part.trim();

        if (part.length() > 0) {
            if (f.count >= CRON_FIELD_MAX_RANGES) { return false; }

            uint8_t from = 0, to = maxVal, step = 1;
            int slashIdx = part.indexOf('/');
            String rangePart = part;
            if (slashIdx >= 0) {
                step = (uint8_t)part.substring(slashIdx + 1).toInt();
                if (step == 0) { return false; }
                rangePart = part.substring(0, slashIdx);
                rangePart.trim();
            }

            if (rangePart == "*") {
                from = 0;
                to = maxVal;
            } else {
                int dashIdx = rangePart.indexOf('-');
                if (dashIdx >= 0) {
                    from = (uint8_t)rangePart.substring(0, dashIdx).toInt();
                    to = (uint8_t)rangePart.substring(dashIdx + 1).toInt();
                } else {
                    from = (uint8_t)rangePart.toInt();
                    to = from;
                }
                if (from > maxVal) { from = maxVal; }
                if (to > maxVal) { to = maxVal; }
            }

            f.from[f.count] = from;
            f.to[f.count] = to;
            f.step[f.count] = step;
            f.count++;
        }

        if (commaIdx < 0) { break; }
        startIdx = commaIdx + 1;
    }
    return (f.count > 0);
}

// Совпадение значения с полем cron (поддерживает «ночные» диапазоны from > to)
static bool cronFieldMatch(uint8_t val, const CronField& f) {
    for (uint8_t i = 0; i < f.count; i++) {
        if (f.from[i] <= f.to[i]) {
            if (val >= f.from[i] && val <= f.to[i] && ((val - f.from[i]) % f.step[i]) == 0) { return true; }
        } else {
            // Ночной диапазон from..max, 0..to — разворачиваем в непрерывную последовательность
            // от from (позиция 0) до to, чтобы шаг отсчитывался от начала диапазона.
            uint8_t len = (uint8_t)(f.maxVal - f.from[i] + f.to[i] + 1);
            if (val >= f.from[i]) {
                uint8_t pos = (uint8_t)(val - f.from[i]);
                if (pos < len && (pos % f.step[i]) == 0) { return true; }
            } else if (val <= f.to[i]) {
                uint8_t pos = (uint8_t)(val + f.maxVal - f.from[i] + 1);
                if (pos < len && (pos % f.step[i]) == 0) { return true; }
            }
        }
    }
    return false;
}

// Поле является простым wildcard '*': count==1, 0..maxVal, шаг 1.
// Такое поле не ограничивает значение и не должно считаться «указанным».
static bool cronFieldIsAny(const CronField& f) {
    return (f.count == 1 && f.from[0] == 0 && f.to[0] == f.maxVal && f.step[0] == 1);
}

// Проверка совпадения всех полей правила cron с текущим временем
static bool macroCronMatch(const MacroRule& r, time_t t) {
    bool match = true;

    if (r.hasSeconds) {
        match &= cronFieldMatch((uint8_t)second(t), r.cron[0]);
    }
    match &= cronFieldMatch((uint8_t)minute(t), r.cron[1]);
    match &= cronFieldMatch((uint8_t)hour(t),   r.cron[2]);

    // День месяца и день недели: если заданы оба — срабатывает по совпадению любого.
    // Wildcard '*' не считается «указанным» полем (иначе OR всегда истинен).
    bool domSpecified = !cronFieldIsAny(r.cron[3]);
    bool dowSpecified = !cronFieldIsAny(r.cron[5]);
    if (domSpecified && dowSpecified) {
        match &= (cronFieldMatch((uint8_t)day(t), r.cron[3]) ||
                  cronFieldMatch((uint8_t)(weekday(t) - 1), r.cron[5]));
    } else {
        if (domSpecified) { match &= cronFieldMatch((uint8_t)day(t), r.cron[3]); }
        if (dowSpecified) { match &= cronFieldMatch((uint8_t)(weekday(t) - 1), r.cron[5]); }
    }

    match &= cronFieldMatch((uint8_t)month(t), r.cron[4]);
    return match;
}

// ============================================================
// Периодическая задача cron (EERTOS, раз в секунду)
// ============================================================

void CLASS_MODULE_MACROS::tick() {
    if (module_macros._config.enabled && NTP.getLastNTPSync() > 0) {
        time_t t = now();
        for (uint8_t i = 0; i < module_macros._ruleCount; i++) {
            MacroRule& r = module_macros._rules[i];
            if (r.type != MACRO_TRIG_CRON) { continue; }

            if (macroCronMatch(r, t)) {
                // Не срабатываем повторно в течение одного интервала:
                // для 6 полей — раз в секунду, для 5 полей — раз в минуту
                bool fired = false;
                if (r.hasSeconds) {
                    fired = ((uint32_t)t == r.lastFire);
                } else {
                    fired = (((uint32_t)t / 60) == (r.lastFire / 60));
                }
                if (!fired) {
                    r.lastFire = (uint32_t)t;
                    module_macros.fireRule(i);
                }
            }
        }
    }
    SetTimerTask(CLASS_MODULE_MACROS::tick, 1000);
}

// ============================================================
// Терминальные команды
// ============================================================

void macroCmd() {
    String arg = term.getNext();

    if (arg == "list") {
        module_macros.printRules();
        return;
    }
    if (arg == "reload") {
        module_macros.reloadScenario();
        Serial.printf("[MACRO] scenario reloaded: %d rules\r\n", module_macros.getRuleCount());
        return;
    }
    if (arg == "run") {
        String id = term.getNext();
        if (id.length() == 0) { Serial.println("Usage: macro run <id>"); return; }
        if (module_macros.fireById(id)) { Serial.println("[MACRO] fired"); }
        else { Serial.println("[MACRO] not found"); }
        return;
    }
    if (arg == "msg") {
        String word = term.getNext();
        if (word.length() == 0) { Serial.println("Usage: macro msg <word>"); return; }
        if (module_macros.fireByType(MACRO_TRIG_TERM, word)) { Serial.println("[MACRO] fired"); }
        else { Serial.println("[MACRO] not found"); }
        return;
    }

    Serial.println("Commands:");
    Serial.println("  macro list          - show rules");
    Serial.println("  macro reload        - reload scenario file");
    Serial.println("  macro run <id>      - fire rule by id");
    Serial.println("  macro msg <word>    - fire 'term' rules by word");
}

void macroTerminalRegister() {
    term.addCommand("macro", macroCmd);
}
