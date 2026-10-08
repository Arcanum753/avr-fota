#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_json/core_json.h"

#include "core_state.h"
#include "common_module.h"

// ============================================================
// Глобальные объекты
// ============================================================

CLASS_CORE_STATE core_state;

// Имена режимов ядра (определены в core_state_engine.cpp).
extern const char* const coreModeNames[CORE_MODE_COUNT];

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
        if (r.kind == BusValue::ENUM) {
            // r.name — полное имя (wifi.mode), тот же источник, что и o["full"].
            int32_t vi = r.value.i;
            o["value"] = vi;
            const char* vn = enumNameByIndex(r.name, vi);
            if (vn != nullptr) { o["valueName"] = vn; }
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

// Данные страницы управления модулями: не-привилегированные namespace с режимом/описанием.
uint8_t CLASS_CORE_STATE::modulesInfo(BusModuleInfo* out, uint8_t max) {
    if (out == nullptr || max == 0) { return 0; }
    uint8_t n = 0;
    for (uint8_t i = 0; i < _nsCount && n < max; i++) {
        // Ядро — не модуль управления: у него нет режима off/auto/macro.
        // В список модулей для центральной страницы не попадает.
        if (_ns[i].privileged) { continue; }

        BusModuleInfo& info = out[n];
        info.name = _ns[i].name;
        info.privileged = false;
        info.prio = _ns[i].prio;
        info.mode = -1;

        char full[CORE_STATE_NAME_LEN];
        snprintf(full, sizeof(full), "%s.mode", _ns[i].name);
        int mi = findRes(full);
        if (mi >= 0 && _res[mi].kind == BusValue::ENUM) {
            info.mode = _res[mi].value.i;
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
        info.desc = desc;
        n++;
    }
    return n;
}

// Смена режима модуля off/auto/macro с валидацией (страница управления модулями).
int CLASS_CORE_STATE::moduleMode(const char* module_namespace, int m) {
    if (module_namespace == nullptr) { return BUS_ERR_BAD_TYPE; }
    int id = findNs(module_namespace, false);
    if (id < 0) { return BUS_ERR_NOT_FOUND; }
    if (_ns[id].privileged) { return BUS_ERR_READONLY; }

    char full[CORE_STATE_NAME_LEN];
    snprintf(full, sizeof(full), "%s.mode", module_namespace);
    int mi = findRes(full);
    if (mi < 0 || _res[mi].kind != BusValue::ENUM) { return BUS_ERR_NOT_SUPPORTED; }
    if (m < 0 || m > 2) { return BUS_ERR_BAD_VALUE; }

    return mode(module_namespace, m);
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

// Версия страницы управления модулями отдаётся module_macros (/macros/ver).
