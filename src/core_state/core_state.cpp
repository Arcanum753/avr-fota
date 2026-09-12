#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_json/core_json.h"

#include "core_state.h"
#include "common_module.h"
#include "core_state_version.h"

// ============================================================
// Глобальные объекты
// ============================================================

CLASS_CORE_STATE core_state;

// Имена режимов ядра (определены в core_state_engine.cpp).
extern const char* const coreModeNames[CORE_MODE_COUNT];

// Собран ли Lua-модуль (для опции macro в UI и проверок).
static bool modulesMacrosAvailable() {
#if defined(MODULE_MACROS)
    return true;
#endif
#if !defined(MODULE_MACROS)
    return false;
#endif
}

// ============================================================
// begin()
// ============================================================
void CLASS_CORE_STATE::begin(ModContext& ctx) {
    DEBUGSTATE("%s\r\n", __FUNCTION__);

    _fs = ctx.fs;

    defaultConfigState();
    if (loadConfigState() == false) { saveConfigState(); }

    _mode = CORE_MODE_INIT;
    _modeSinceMs = millis();
}

// ============================================================
// register_resources()
// ============================================================
void CLASS_CORE_STATE::register_resources() {
    DEBUGSTATE("%s\r\n", __FUNCTION__);

    setNamespace("system");
    setPrivileged(true);

    regEnum("mode", CORE_MODE_COUNT, coreModeNames, "system mode");
    regEvent("mode_changed", "system mode changed");

    clearNamespace();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_CORE_STATE::web_Init() {
    DEBUGSTATE("%s\r\n", __FUNCTION__);

    ESPHTTPServer.on("/state/catalog", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleCatalog(request);
    });

    ESPHTTPServer.on("/state/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleInfo(request);
    });

    ESPHTTPServer.on("/state/set", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleSet(request);
    });

    ESPHTTPServer.on("/state/call", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleCall(request);
    });

    // Список модулей и их режимы (центральная страница управления модулями).
    ESPHTTPServer.on("/state/modules", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleModules(request);
    });

    // Смена режима модуля: off/auto/macro.
    ESPHTTPServer.on("/state/module_mode", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleModuleMode(request);
    });

    ESPHTTPServer.on("/state/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Каталог
// ============================================================
void CLASS_CORE_STATE::catalogToJson(JsonDocument& doc) {
    JsonObject feat = doc["features"].to<JsonObject>();
#if defined(MODULE_EDITOR)
    feat["editor"] = 1;
#endif
#if !defined(MODULE_EDITOR)
    feat["editor"] = 0;
#endif

    JsonArray arr = doc["resources"].to<JsonArray>();
    for (uint16_t i = 0; i < _resCount; i++) {
        BusRes& r = _res[i];
        JsonObject o = arr.add<JsonObject>();
        o["ns"] = r.ns;
        o["name"] = r.field;
        o["full"] = r.name;
        o["kind"] = ns_core_state::kindName(r.kind);
        o["desc"] = (r.desc != nullptr) ? r.desc : "";
        o["rw"] = r.writable;
        o["e"] = r.isEvent;
        o["f"] = r.isFunc;
        o["a"] = r.async;
        if (r.enumCount > 0) {
            JsonArray en = o["enum"].to<JsonArray>();
            for (uint8_t k = 0; k < r.enumCount; k++) { en.add(r.enumVals[k]); }
        }
        if (r.codeCount > 0) {
            JsonArray codes = o["codes"].to<JsonArray>();
            for (uint8_t k = 0; k < r.codeCount; k++) {
                JsonObject c = codes.add<JsonObject>();
                c["code"] = r.codes[k].code;
                c["meaning"] = r.codes[k].meaning;
            }
        }
    }
}

// Список модулей (namespace) с их режимами — для страницы управления модулями.
void CLASS_CORE_STATE::catalogModulesToJson(JsonDocument& doc) {
    doc["macros_available"] = (modulesMacrosAvailable() ? 1 : 0);

    JsonArray mods = doc["modules"].to<JsonArray>();
    for (uint8_t i = 0; i < _nsCount; i++) {
        JsonObject o = mods.add<JsonObject>();
        o["ns"] = _ns[i].name;
        o["privileged"] = _ns[i].privileged ? 1 : 0;
        o["prio"] = _ns[i].prio;

        char full[CORE_STATE_NAME_LEN];
        snprintf(full, sizeof(full), "%s.mode", _ns[i].name);
        int mi = findRes(full);
        if (mi >= 0 && _res[mi].kind == BusValue::ENUM) {
            o["has_mode"] = 1;
            o["mode"] = _res[mi].value.i;
        } else {
            o["has_mode"] = 0;
            o["mode"] = -1;
        }

        // Описание: первый непустой desc ресурса этого namespace.
        const char* desc = "";
        for (uint16_t r = 0; r < _resCount; r++) {
            if (strcmp(_res[r].ns, _ns[i].name) == 0
                && _res[r].desc != nullptr && _res[r].desc[0] != 0) {
                desc = _res[r].desc;
                break;
            }
        }
        o["desc"] = desc;
    }
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_CORE_STATE::handleCatalog(AsyncWebServerRequest *request) {
    JsonDocument doc;
    catalogToJson(doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void CLASS_CORE_STATE::handleInfo(AsyncWebServerRequest *request) {
    if (!request->hasArg("name")) {
        request->send(400, "text/plain", "No name");
        return;
    }
    int idx = findRes(request->arg("name").c_str());
    if (idx < 0) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    BusRes& r = _res[idx];
    String out = "name|" + String(r.name) + "|input\n";
    out += "kind|" + String(ns_core_state::kindName(r.kind)) + "|input\n";
    out += "desc|" + String((r.desc != nullptr) ? r.desc : "") + "|input\n";
    out += "value|" + coreStateBusText(r.value) + "|input\n";
    request->send(200, "text/plain", out);
}

void CLASS_CORE_STATE::handleSet(AsyncWebServerRequest *request) {
    if (!request->hasArg("name") || !request->hasArg("value")) {
        request->send(400, "text/plain", "No name/value");
        return;
    }
    int idx = findRes(request->arg("name").c_str());
    if (idx < 0) {
        request->send(404, "text/plain", "Not found");
        return;
    }
    BusRes& r = _res[idx];
    String val = request->arg("value");

    BusValue arg;
    switch (r.kind) {
        case BusValue::BOOL: arg = BusValue::bo(val == "1" || val == "true"); break;
        case BusValue::F32:  arg = BusValue::f32(val.toFloat()); break;
        case BusValue::STR:  arg = BusValue::str(val); break;
        case BusValue::TIME: arg = BusValue::tm((int64_t)val.toInt()); break;
        default:             arg = BusValue::i32(val.toInt()); break;
    }

    int rc;
    if (r.isFunc && !r.async) {
        // Ресурс-состояние с функцией записи: применяем через функцию.
        BusValue res;
        rc = r.fn(r.user, 1, &arg, res);
    } else {
        rc = writeRes(idx, arg, true);
    }
    if (rc == BUS_OK) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", ns_core_state::busErrStr(rc));
    }
}

void CLASS_CORE_STATE::handleCall(AsyncWebServerRequest *request) {
    if (!request->hasArg("name")) {
        request->send(400, "text/plain", "No name");
        return;
    }
    BusValue result;
    int rc = call(request->arg("name").c_str(), 0, nullptr, result);
    if (rc == BUS_OK) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", ns_core_state::busErrStr(rc));
    }
}

void CLASS_CORE_STATE::handleModules(AsyncWebServerRequest *request) {
    JsonDocument doc;
    catalogModulesToJson(doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

void CLASS_CORE_STATE::handleModuleMode(AsyncWebServerRequest *request) {
    if (!request->hasArg("ns") || !request->hasArg("mode")) {
        request->send(200, "text/plain", "ERR: no ns/mode");
        return;
    }
    const char* ns = request->arg("ns").c_str();
    int id = findNs(ns, false);
    if (id < 0) {
        request->send(200, "text/plain", ns_core_state::busErrStr(BUS_ERR_NOT_FOUND));
        return;
    }
    if (_ns[id].privileged) {
        // Ядровые namespace переключать извне нельзя.
        request->send(200, "text/plain", ns_core_state::busErrStr(BUS_ERR_READONLY));
        return;
    }

    char full[CORE_STATE_NAME_LEN];
    snprintf(full, sizeof(full), "%s.mode", ns);
    int mi = findRes(full);
    if (mi < 0 || _res[mi].kind != BusValue::ENUM) {
        request->send(200, "text/plain", ns_core_state::busErrStr(BUS_ERR_NOT_SUPPORTED));
        return;
    }

    int m = request->arg("mode").toInt();
    if (m < 0 || m > 2) {
        request->send(200, "text/plain", ns_core_state::busErrStr(BUS_ERR_BAD_VALUE));
        return;
    }
    if (m == 2 && !modulesMacrosAvailable()) {
        request->send(200, "text/plain", ns_core_state::busErrStr(BUS_ERR_NOT_SUPPORTED));
        return;
    }

    int rc = mode(ns, m);
    if (rc == BUS_OK) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", ns_core_state::busErrStr(rc));
    }
}

// ============================================================
// Конфиг
// ============================================================
void CLASS_CORE_STATE::defaultConfigState() {
    _testTimeoutMs = 1800000UL;
    _opMaxTimeoutMs = 1800000UL;
}

bool CLASS_CORE_STATE::loadConfigState() {
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_STATE, doc) == false) { return false; }

    uint32_t testS = doc["test_timeout_s"] | 1800;
    uint32_t opS   = doc["op_timeout_s"]   | 1800;
    _testTimeoutMs = (testS > 0) ? testS * 1000UL : 1800000UL;
    _opMaxTimeoutMs = (opS > 0) ? opS * 1000UL : 1800000UL;
    return true;
}

bool CLASS_CORE_STATE::saveConfigState() {
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_STATE, doc);
    doc["test_timeout_s"] = _testTimeoutMs / 1000UL;
    doc["op_timeout_s"] = _opMaxTimeoutMs / 1000UL;
    return core_json.jsonFileSaveDoc(CONFIG_FILE_STATE, doc);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_CORE_STATE::getVersionStr() {
    return String(CORE_STATE_VERSION);
}

String CLASS_CORE_STATE::getGeneratedTime() {
    return String(CORE_STATE_GENERATED_TIME);
}

String CLASS_CORE_STATE::getCommitDateStr() {
    return String(CORE_STATE_COMMIT_DATE_STR);
}

void CLASS_CORE_STATE::html_ver_get(AsyncWebServerRequest *request) {
    String values = "";
    values += "stateversion|" + getVersionStr()    + "|div\n";
    values += "stategentime|" + getGeneratedTime() + "|div\n";
    values += "stategendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
