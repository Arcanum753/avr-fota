#include "core_web/FSWebServerLib.h"

#include <string.h>

#include "core_json/core_json.h"

#include "core_state.h"
#include "common_module.h"
#include "core_state_version.h"
#include "core_sys/eertos.h"

// ============================================================
// Глобальные объекты
// ============================================================

CLASS_CORE_STATE core_state;

// Имена режимов ядра (индексы соответствуют CoreMode).
static const char* const coreModeNames[CORE_MODE_COUNT] = {
    "init",
    "normal",
    "ota",
    "fs_update",
    "prog",
    "test",
};

// ============================================================
// Вспомогательные преобразования значений
// ============================================================

static int64_t busNumeric(const BusValue& v) {
    switch (v.kind) {
        case BusValue::BOOL: return v.b ? 1 : 0;
        case BusValue::I32:  return v.i;
        case BusValue::F32:  return (int64_t)v.f;
        case BusValue::TIME: return v.t;
        case BusValue::ENUM: return v.i;
        case BusValue::STR:  return (int64_t)v.s.toInt();
        default:             return 0;
    }
}

static String busText(const BusValue& v) {
    switch (v.kind) {
        case BusValue::BOOL: return v.b ? "true" : "false";
        case BusValue::I32:  return String(v.i);
        case BusValue::F32:  return ns_core_state::formatF32(v.f);
        case BusValue::TIME: return String((long)v.t);
        case BusValue::ENUM: return String(v.i);
        case BusValue::STR:  return v.s;
        default:             return "";
    }
}

// Приводит значение к целевому типу ресурса.
static BusValue busCoerce(const BusValue& v, BusValue::Kind k) {
    BusValue r;
    r.kind = k;
    switch (k) {
        case BusValue::BOOL:
            r.b = (busNumeric(v) != 0);
            break;
        case BusValue::I32:
        case BusValue::ENUM:
            r.i = (int32_t)busNumeric(v);
            break;
        case BusValue::F32:
            r.f = (float)busNumeric(v);
            break;
        case BusValue::TIME:
            r.t = busNumeric(v);
            break;
        case BusValue::STR:
            r.s = busText(v);
            break;
        default:
            break;
    }
    return r;
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

    ESPHTTPServer.on("/state/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Контекст регистрации/вызова
// ============================================================
int CLASS_CORE_STATE::findNs(const char* ns, bool create) {
    if (ns == nullptr || ns[0] == 0) { return -1; }
    for (uint8_t i = 0; i < _nsCount; i++) {
        if (strcmp(_ns[i].name, ns) == 0) { return i; }
    }
    if (create && _nsCount < CORE_STATE_MAX_NS) {
        uint8_t id = _nsCount;
        strncpy(_ns[id].name, ns, CORE_STATE_NS_NAME_LEN - 1);
        _ns[id].name[CORE_STATE_NS_NAME_LEN - 1] = 0;
        _ns[id].id = id;
        _ns[id].privileged = false;
        _nsCount++;
        return id;
    }
    return -1;
}

void CLASS_CORE_STATE::setNamespace(const char* ns) {
    _currentNs = (int8_t)findNs(ns, true);
    _privileged = (_currentNs >= 0) ? _ns[_currentNs].privileged : false;
}

void CLASS_CORE_STATE::clearNamespace() {
    _currentNs = -1;
    _privileged = false;
}

void CLASS_CORE_STATE::setPrivileged(bool on) {
    _privileged = on;
    if (_currentNs >= 0) { _ns[_currentNs].privileged = on; }
}

// ============================================================
// Регистрация ресурсов
// ============================================================
void CLASS_CORE_STATE::makeFullName(char* out, const char* name) {
    if (name == nullptr) { out[0] = 0; return; }
    if (_currentNs < 0 || ns_core_state::nameHasDot(name)) {
        strncpy(out, name, CORE_STATE_NAME_LEN - 1);
        out[CORE_STATE_NAME_LEN - 1] = 0;
        return;
    }
    snprintf(out, CORE_STATE_NAME_LEN, "%s.%s", _ns[_currentNs].name, name);
}

int CLASS_CORE_STATE::findRes(const char* name) {
    if (name == nullptr) { return -1; }
    for (uint16_t i = 0; i < _resCount; i++) {
        if (strcmp(_res[i].name, name) == 0) { return (int)i; }
    }
    return -1;
}

int CLASS_CORE_STATE::findFunc(const char* name) {
    int idx = findRes(name);
    if (idx < 0) { return -1; }
    return _res[idx].isFunc ? idx : -1;
}

bool CLASS_CORE_STATE::regStateAs(const char* full_name, BusValue::Kind kind,
                                  const char* desc, bool writable) {
    int idx = findRes(full_name);
    if (idx < 0) {
        if (_resCount >= CORE_STATE_MAX_RES) { return false; }
        idx = _resCount++;
        BusRes& r = _res[idx];
        strncpy(r.name, full_name, CORE_STATE_NAME_LEN - 1);
        r.name[CORE_STATE_NAME_LEN - 1] = 0;
        // Разбиение на namespace/поле по первой точке.
        const char* dot = strchr(r.name, '.');
        if (dot != nullptr) {
            size_t nlen = (size_t)(dot - r.name);
            if (nlen >= CORE_STATE_NS_LEN) { nlen = CORE_STATE_NS_LEN - 1; }
            memcpy(r.ns, r.name, nlen);
            r.ns[nlen] = 0;
            strncpy(r.field, dot + 1, CORE_STATE_FIELD_LEN - 1);
        } else {
            strncpy(r.field, r.name, CORE_STATE_FIELD_LEN - 1);
        }
        r.ownerId = (_currentNs >= 0) ? (uint8_t)_currentNs : 255;
    }

    // При обновлении существующего ресурса не сбрасываем поля функции/события:
    // regEnum() создаёт состояние, а regFunc()/regEvent() дополняют его тем же
    // именем (e7.mode = ENUM-состояние + sync-функция записи).
    BusRes& r = _res[idx];
    r.kind = kind;
    if (desc != nullptr) { r.desc = desc; }
    r.writable = writable;
    return true;
}

bool CLASS_CORE_STATE::regState(const char* name, BusValue::Kind kind,
                                const char* desc, bool writable) {
    char full[CORE_STATE_NAME_LEN];
    makeFullName(full, name);
    return regStateAs(full, kind, desc, writable);
}

bool CLASS_CORE_STATE::regEnum(const char* name, int count, const char* const* values,
                               const char* desc) {
    char full[CORE_STATE_NAME_LEN];
    makeFullName(full, name);
    if (regStateAs(full, BusValue::ENUM, desc, true) == false) { return false; }
    int idx = findRes(full);
    BusRes& r = _res[idx];
    r.enumCount = 0;
    for (int i = 0; i < count && i < CORE_STATE_MAX_ENUM; i++) {
        r.enumVals[i] = values[i];
        r.enumCount++;
    }
    r.value = BusValue::en(0);
    return true;
}

bool CLASS_CORE_STATE::regEvent(const char* name, const char* desc) {
    char full[CORE_STATE_NAME_LEN];
    makeFullName(full, name);
    int idx = findRes(full);
    if (idx < 0) {
        if (regStateAs(full, BusValue::NONE, desc, false) == false) { return false; }
        idx = findRes(full);
    }
    _res[idx].isEvent = true;
    return true;
}

bool CLASS_CORE_STATE::regFunc(const char* name, const char* sig, const char* desc,
                               BusCb fn, void* user) {
    (void)sig;
    char full[CORE_STATE_NAME_LEN];
    makeFullName(full, name);
    int idx = findRes(full);
    if (idx < 0) {
        if (regStateAs(full, BusValue::NONE, desc, false) == false) { return false; }
        idx = findRes(full);
    } else if (desc != nullptr && _res[idx].desc == nullptr) {
        _res[idx].desc = desc;
    }
    BusRes& r = _res[idx];
    r.isFunc = true;
    r.async = false;
    r.fn = fn;
    r.afn = nullptr;
    r.user = user;
    return true;
}

bool CLASS_CORE_STATE::regFuncAsync(const char* name, const char* sig, const char* desc,
                                    BusAsyncCb fn, void* user, uint32_t timeout_ms) {
    (void)sig;
    char full[CORE_STATE_NAME_LEN];
    makeFullName(full, name);
    int idx = findRes(full);
    if (idx < 0) {
        if (regStateAs(full, BusValue::NONE, desc, false) == false) { return false; }
        idx = findRes(full);
    } else if (desc != nullptr && _res[idx].desc == nullptr) {
        _res[idx].desc = desc;
    }
    BusRes& r = _res[idx];
    r.isFunc = true;
    r.async = true;
    r.fn = nullptr;
    r.afn = fn;
    r.user = user;
    r.timeoutMs = timeout_ms;
    return true;
}

bool CLASS_CORE_STATE::regFuncCode(const char* func_name, int code, const char* meaning) {
    int idx = findFunc(func_name);
    if (idx < 0) { return false; }
    BusRes& r = _res[idx];
    if (r.codeCount >= CORE_STATE_MAX_CODES) { return false; }
    r.codes[r.codeCount].code = code;
    r.codes[r.codeCount].meaning = meaning;
    r.codeCount++;
    return true;
}

// ============================================================
// Чтение значений
// ============================================================
bool CLASS_CORE_STATE::has(const char* name) {
    return findRes(name) >= 0;
}

bool CLASS_CORE_STATE::getBool(const char* name, bool def) {
    int idx = findRes(name);
    if (idx < 0 || _res[idx].kind == BusValue::NONE) { return def; }
    return busNumeric(_res[idx].value) != 0;
}

int32_t CLASS_CORE_STATE::getInt(const char* name, int32_t def) {
    int idx = findRes(name);
    if (idx < 0 || _res[idx].kind == BusValue::NONE) { return def; }
    return (int32_t)busNumeric(_res[idx].value);
}

float CLASS_CORE_STATE::getF32(const char* name, float def) {
    int idx = findRes(name);
    if (idx < 0 || _res[idx].kind == BusValue::NONE) { return def; }
    const BusValue& v = _res[idx].value;
    switch (v.kind) {
        case BusValue::F32:  return v.f;
        case BusValue::BOOL: return v.b ? 1.0f : 0.0f;
        default:             return (float)busNumeric(v);
    }
}

String CLASS_CORE_STATE::getStr(const char* name, const String& def) {
    int idx = findRes(name);
    if (idx < 0 || _res[idx].kind == BusValue::NONE) { return def; }
    return _res[idx].value.s;
}

int64_t CLASS_CORE_STATE::getTime(const char* name, int64_t def) {
    int idx = findRes(name);
    if (idx < 0 || _res[idx].kind == BusValue::NONE) { return def; }
    return busNumeric(_res[idx].value);
}

int CLASS_CORE_STATE::info(const char* name, BusResInfo& out) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    BusRes& r = _res[idx];
    out.name = r.name;
    out.kind = r.kind;
    out.desc = r.desc;
    out.writable = r.writable;
    out.isEvent = r.isEvent;
    out.isFunc = r.isFunc;
    out.async = r.async;
    out.timeoutMs = r.timeoutMs;
    out.enumCount = r.enumCount;
    for (uint8_t i = 0; i < r.enumCount; i++) { out.enumVals[i] = r.enumVals[i]; }
    return BUS_OK;
}

BusValue CLASS_CORE_STATE::valueToKind(const BusValue& v, BusValue::Kind k) {
    return busCoerce(v, k);
}

// ============================================================
// Запись значений
// ============================================================
int CLASS_CORE_STATE::writeRes(int idx, const BusValue& v, bool checkAccess) {
    BusRes& r = _res[idx];
    // Чистые функции/события (kind == NONE) не являются значениями.
    // Ресурс-состояние с функцией записи (kind != NONE, isFunc) допускает запись значения.
    if (r.kind == BusValue::NONE) { return BUS_ERR_BAD_TYPE; }
    if (checkAccess && !r.writable) { return BUS_ERR_READONLY; }

    if (checkAccess && !_privileged && _currentNs >= 0
        && r.ownerId != 255 && r.ownerId != (uint8_t)_currentNs) {
        DEBUGSTATE("access: write %s from %s (owner %s)\r\n",
                   r.name,
                   _ns[_currentNs].name,
                   (r.ownerId < _nsCount) ? _ns[r.ownerId].name : "?");
    }

    BusValue coerced = busCoerce(v, r.kind);
    if (r.kind == BusValue::ENUM && r.enumCount > 0) {
        if (coerced.i < 0 || coerced.i >= (int32_t)r.enumCount) {
            return BUS_ERR_BAD_VALUE;
        }
    }
    r.value = coerced;

    char evt[CORE_STATE_NAME_LEN];
    snprintf(evt, sizeof(evt), "state.%s", r.name);
    queueEvent(evt, r.value);
    return BUS_OK;
}

int CLASS_CORE_STATE::setBool(const char* name, bool value) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, BusValue::bo(value), true);
}

int CLASS_CORE_STATE::setInt(const char* name, int32_t value) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, BusValue::i32(value), true);
}

int CLASS_CORE_STATE::setF32(const char* name, float value) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, BusValue::f32(value), true);
}

int CLASS_CORE_STATE::setStr(const char* name, const String& value) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, BusValue::str(value), true);
}

int CLASS_CORE_STATE::setTime(const char* name, int64_t value) {
    int idx = findRes(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, BusValue::tm(value), true);
}

int CLASS_CORE_STATE::mode(const char* module_namespace, int new_mode) {
    if (module_namespace == nullptr) { return BUS_ERR_BAD_TYPE; }
    char full[CORE_STATE_NAME_LEN];
    snprintf(full, sizeof(full), "%s.mode", module_namespace);
    int idx = findRes(full);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    if (new_mode < 0) { return _res[idx].value.i; }

    int f = findFunc(full);
    if (f >= 0 && !_res[f].async) {
        BusValue a = BusValue::i32(new_mode);
        BusValue res;
        return _res[f].fn(_res[f].user, 1, &a, res);
    }
    return writeRes(idx, BusValue::en(new_mode), true);
}

int CLASS_CORE_STATE::signal(const char* sig, const BusValue& v) {
    int idx = findRes(sig);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    return writeRes(idx, v, false);
}

// ============================================================
// События
// ============================================================
void CLASS_CORE_STATE::queueEvent(const char* evt, const BusValue& v) {
    if (_evCount >= CORE_STATE_EV_QUEUE) {
        DEBUGSTATE("event queue full, drop %s\r\n", evt);
        return;
    }
    BusEv& e = _evQueue[_evTail];
    strncpy(e.evt, evt, CORE_STATE_NAME_LEN - 1);
    e.evt[CORE_STATE_NAME_LEN - 1] = 0;
    e.v = v;
    _evTail = (uint8_t)((_evTail + 1) % CORE_STATE_EV_QUEUE);
    _evCount++;
}

void CLASS_CORE_STATE::emit(const char* evt, const BusValue& v) {
    queueEvent(evt, v);
}

uint32_t CLASS_CORE_STATE::on(const char* evt, BusCb cb, void* user) {
    if (evt == nullptr || cb == nullptr) { return 0; }
    for (uint8_t i = 0; i < CORE_STATE_MAX_SUBS; i++) {
        if (!_subs[i].used) {
            _subs[i].used = true;
            strncpy(_subs[i].evt, evt, CORE_STATE_NAME_LEN - 1);
            _subs[i].evt[CORE_STATE_NAME_LEN - 1] = 0;
            _subs[i].cb = cb;
            _subs[i].user = user;
            _subs[i].handle = _subNextHandle++;
            return _subs[i].handle;
        }
    }
    return 0;
}

void CLASS_CORE_STATE::off(uint32_t handle) {
    for (uint8_t i = 0; i < CORE_STATE_MAX_SUBS; i++) {
        if (_subs[i].used && _subs[i].handle == handle) {
            _subs[i].used = false;
            _subs[i].cb = nullptr;
            _subs[i].user = nullptr;
            return;
        }
    }
}

void CLASS_CORE_STATE::dispatchEvents() {
    while (_evCount > 0) {
        BusEv ev = _evQueue[_evHead];
        _evHead = (uint8_t)((_evHead + 1) % CORE_STATE_EV_QUEUE);
        _evCount--;

        for (uint8_t i = 0; i < CORE_STATE_MAX_SUBS; i++) {
            if (_subs[i].used && strcmp(_subs[i].evt, ev.evt) == 0) {
                // argv[0] — имя события, argv[1] — значение.
                BusValue args[2];
                args[0] = BusValue::str(String(ev.evt));
                args[1] = ev.v;
                BusValue out;
                _subs[i].cb(_subs[i].user, 2, args, out);
            }
        }
    }

    while (_doneCount > 0) {
        BusDone d = _doneQueue[0];
        for (uint8_t i = 0; i + 1 < _doneCount; i++) { _doneQueue[i] = _doneQueue[i + 1]; }
        _doneCount--;
        if (d.cb != nullptr) {
            BusValue args[2];
            args[0] = BusValue::i32(d.rc);
            args[1] = d.result;
            BusValue out;
            d.cb(d.cbUser, 2, args, out);
        }
    }
}

// ============================================================
// Асинхронные вызовы
// ============================================================
int CLASS_CORE_STATE::call(const char* name, int argc, BusValue args[], BusValue& result) {
    int idx = findFunc(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    if (_res[idx].async) { return BUS_ERR_BAD_TYPE; }
    if (argc > CORE_STATE_MAX_ARGS) { return BUS_ERR_BAD_ARGC; }
    return _res[idx].fn(_res[idx].user, argc, args, result);
}

int CLASS_CORE_STATE::call_async(const char* name, BusCb cb, void* user,
                                 int argc, BusValue args[]) {
    int idx = findFunc(name);
    if (idx < 0) { return BUS_ERR_NOT_FOUND; }
    if (!_res[idx].async) { return BUS_ERR_BAD_TYPE; }
    if (argc > CORE_STATE_MAX_ARGS) { return BUS_ERR_BAD_ARGC; }

    if (_asyncOwner != nullptr) {
        uint8_t cnt = 0;
        for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
            if (_async[i].used && _async[i].owner == _asyncOwner) { cnt++; }
        }
        if (cnt >= 3) { return BUS_ERR_BUSY; }
    }

    int slot = -1;
    for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
        if (!_async[i].used) { slot = i; break; }
    }
    if (slot < 0) { return BUS_ERR_BUSY; }

    BusAsync& a = _async[slot];
    a.used = true;
    a.handle = _asyncNextHandle++;
    a.fn = _res[idx].afn;
    a.user = _res[idx].user;
    a.cb = cb;
    a.cbUser = user;
    a.startMs = millis();
    a.timeoutMs = _res[idx].timeoutMs;
    a.owner = _asyncOwner;
    a.argc = argc;
    for (int i = 0; i < argc && i < CORE_STATE_MAX_ARGS; i++) { a.args[i] = args[i]; }

    int rc = a.fn(a.user, a.handle, argc, args);
    if (rc != BUS_OK) { a.used = false; }
    return rc;
}

void CLASS_CORE_STATE::asyncComplete(uint32_t handle, int rc, const BusValue& result) {
    for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
        if (_async[i].used && _async[i].handle == handle) {
            if (_doneCount < CORE_STATE_MAX_ASYNC) {
                BusDone& d = _doneQueue[_doneCount];
                d.used = true;
                d.handle = handle;
                d.rc = rc;
                d.result = result;
                d.cb = _async[i].cb;
                d.cbUser = _async[i].cbUser;
                _doneCount++;
            }
            _async[i].used = false;
            return;
        }
    }
}

void CLASS_CORE_STATE::cancel_async(int handle) {
    for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
        if (_async[i].used && _async[i].handle == (uint32_t)handle) {
            _async[i].used = false;
            return;
        }
    }
}

void CLASS_CORE_STATE::setAsyncOwner(void* ownerToken) {
    _asyncOwner = ownerToken;
}

void CLASS_CORE_STATE::asyncCancelFor(void* ownerToken) {
    for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
        if (_async[i].used && _async[i].owner == ownerToken) {
            _async[i].used = false;
        }
    }
}

void CLASS_CORE_STATE::processAsyncTimeouts() {
    uint32_t nowMs = millis();
    for (uint8_t i = 0; i < CORE_STATE_MAX_ASYNC; i++) {
        BusAsync& a = _async[i];
        if (!a.used) { continue; }
        if (a.timeoutMs > 0 && (nowMs - a.startMs) >= a.timeoutMs) {
            uint32_t h = a.handle;
            asyncComplete(h, BUS_ERR_TIMEOUT);
        }
    }
}

// ============================================================
// Режимы
// ============================================================
int CLASS_CORE_STATE::getMode() {
    return _mode;
}

int CLASS_CORE_STATE::setMode(int m) {
    if (m < 0 || m >= CORE_MODE_COUNT) { return BUS_ERR_BAD_VALUE; }

    _mode = (uint8_t)m;
    _modeSinceMs = millis();

    if (m == CORE_MODE_TEST) {
        _opHasTimeout = true;
        _opTimeoutMs = _testTimeoutMs;
    } else if (m == CORE_MODE_OTA || m == CORE_MODE_FS_UPDATE) {
        _opHasTimeout = true;
        _opTimeoutMs = _opMaxTimeoutMs;
    } else {
        _opHasTimeout = false;
    }

    queueEvent("system.mode_changed", BusValue::en(m));
    DEBUGSTATE("mode -> %s\r\n", coreModeNames[m]);
    return BUS_OK;
}

int CLASS_CORE_STATE::requestMode(int m) {
    return requestMode(m, 50);
}

int CLASS_CORE_STATE::requestMode(int m, uint8_t prio) {
    if (m < 0 || m >= CORE_MODE_COUNT) { return BUS_ERR_BAD_VALUE; }
    if (m == _mode) { return BUS_OK; }

    bool longReq = (m == CORE_MODE_OTA || m == CORE_MODE_FS_UPDATE || m == CORE_MODE_PROG);
    bool longCur = (_mode == CORE_MODE_OTA || _mode == CORE_MODE_FS_UPDATE
                    || _mode == CORE_MODE_PROG);

    if (m == CORE_MODE_TEST && longCur) { return BUS_ERR_BUSY; }
    if (longReq && _mode == CORE_MODE_TEST) { return BUS_ERR_BUSY; }
    if (longReq && longCur) {
        if (prio > _opPrio) { _opPrio = prio; return setMode(m); }
        return BUS_ERR_BUSY;
    }

    _opPrio = prio;
    return setMode(m);
}

void CLASS_CORE_STATE::processModeTimeouts() {
    if (!_opHasTimeout) { return; }
    if ((millis() - _modeSinceMs) < _opTimeoutMs) { return; }
    DEBUGSTATE("mode timeout -> normal\r\n");
    setMode(CORE_MODE_NORMAL);
}

// ============================================================
// loop()
// ============================================================
void CLASS_CORE_STATE::loop() {
    processAsyncTimeouts();
    processModeTimeouts();
    dispatchEvents();
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
    out += "value|" + busText(r.value) + "|input\n";
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
