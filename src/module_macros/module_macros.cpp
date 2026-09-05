#include "core_web/FSWebServerLib.h"

#include "core_ntp/NtpClientLib.h"
#include "core_json/core_json.h"
#include "core_terminal/core_terminal.h"
#include "core_terminal/ErriezSerialTerminal.h"

#include "module_macros.h"
#include "common/common.h"
#include "common/TimeLib.h"
#include "module_macros_version.h"
#include "core_sys/eertos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

CLASS_MODULE_MACROS module_macros(false);

CLASS_MODULE_MACROS::CLASS_MODULE_MACROS(bool _in) {
    dumb = _in;
    _fileCount = 0;
    _metaRev = 0;
    _scriptRev = 0;
    _lastScriptRev = 0;
    _ntpWasSynced = false;
    _evIn = 0;
    _evOut = 0;
    _fs = NULL;
}

// Forward declarations — свободные функции, используемые логикой модуля
static String macroJsonEscape(const String& s);
static String macroFileBaseName(const String& pathOrName);

// Периодическая 1-сек задача и терминальный обработчик объявлены здесь,
// чтобы их можно было использовать до определений в конце файла
void macroTickTask();
void macroCmd();

// ============================================================
// setFs()
// ============================================================
#if defined(ESP32)
void CLASS_MODULE_MACROS::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
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

    ensureMacrosDir();
    reconcileList();

    TerminalRegisterModule(macroTerminalRegister);

    // Первичная сборка интерпретаторов запущенных файлов выполняется в setup()
    // (main-loop контекст, до старта веб-сервера), чтобы tick не делал это дважды.
    _scriptRev = 1;
    rebuildScripts();
    _lastScriptRev = _scriptRev;

    // Периодическая задача исполнительной машины (раз в секунду)
    SetTimerTask(macroTickTask, 1000);
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

    // AJAX — список файлов-сценариев (JSON)
    ESPHTTPServer.on("/macros/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleList(request);
    });

    // AJAX — действия по сценариям (только GET, состояние передаётся в query)
    ESPHTTPServer.on("/macros/create", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleCreate(request);
    });

    // AJAX — удалить файл(ы)
    ESPHTTPServer.on("/macros/delete", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleDelete(request);
    });

    // AJAX — переименовать файл
    ESPHTTPServer.on("/macros/rename", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleRename(request);
    });

    // AJAX — запустить/остановить файл
    ESPHTTPServer.on("/macros/state", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleState(request);
    });

    // AJAX — изменить приоритет (информационно)
    ESPHTTPServer.on("/macros/prio", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handlePrio(request);
    });

    // AJAX — перечитать файл(ы)
    ESPHTTPServer.on("/macros/reload", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleReload(request);
    });

    // AJAX — внешнее событие (button/term) — резерв для будущих модулей
    ESPHTTPServer.on("/macros/fire", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFire(request);
    });

    // Версия модуля
    ESPHTTPServer.on("/macros/ver", HTTP_GET, [this](AsyncWebServerRequest *request) {
        this->html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_MODULE_MACROS::handleList(AsyncWebServerRequest *request) {

    String json = "{\"enabled\":";
    json += (_config.enabled ? "true" : "false");
    json += ",\"ntp\":";
    json += (NTP.getLastNTPSync() > 0) ? "1" : "0";
    json += ",\"files\":[";

    // Сортировка по приоритету (0 - высший), затем по имени
    uint8_t order[MACRO_MAX_FILES];
    for (uint8_t i = 0; i < _fileCount; i++) { order[i] = i; }
    for (uint8_t i = 0; i < _fileCount; i++) {
        for (uint8_t j = i + 1; j < _fileCount; j++) {
            bool less = (_files[order[j]].prio < _files[order[i]].prio);
            if (!less && _files[order[j]].prio == _files[order[i]].prio) {
                less = (_files[order[j]].name < _files[order[i]].name);
            }
            if (less) {
                uint8_t tmp = order[i];
                order[i] = order[j];
                order[j] = tmp;
            }
        }
    }

    for (uint8_t n = 0; n < _fileCount; n++) {
        MacroFile& f = _files[order[n]];
        if (n > 0) { json += ","; }

        size_t size = 0;
        File sf = _fs->open(f.name, "r");
        if (sf) {
            size = sf.size();
            sf.close();
        }

        json += "{\"name\":\"";
        json += macroJsonEscape(macroFileBaseName(f.name));
        json += "\",\"prio\":";
        json += String(f.prio);
        json += ",\"run\":";
        json += (f.run ? "true" : "false");
        json += ",\"size\":";
        json += String((uint32_t)size);
        json += ",\"created\":";
        json += String(f.created);
        json += ",\"active\":";
        json += (f.active ? "true" : "false");
        json += ",\"err\":\"";
        json += macroJsonEscape(f.err);
        json += "\"}";
    }

    json += "]}";
    request->send(200, "application/json", json);
}

void CLASS_MODULE_MACROS::handleCreate(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    String tmpl = MACRO_DEFAULT_NAME;
    if (request->hasArg("name")) {
        String n = request->arg("name");
        n.trim();
        if (n.length() > 0) {
            if (!n.endsWith(".tcl")) { n += ".tcl"; }
            if (!nameOk(n)) { request->send(200, "text/plain", "ERR: bad name"); return; }
            tmpl = n;
        }
    }

    String fullPath;
    if (createNewFile(tmpl, fullPath)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "ERR: cannot create");
    }
}

void CLASS_MODULE_MACROS::handleDelete(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (!request->hasArg("name")) { request->send(200, "text/plain", "ERR: no name"); return; }

    String list = request->arg("name");
    bool ok = true;
    int start = 0;
    while (start <= (int)list.length()) {
        int comma = list.indexOf(',', start);
        String base = (comma < 0) ? list.substring(start) : list.substring(start, comma);
        base.trim();
        if (base.length() > 0) {
            if (deleteFileEntry(base) == false) { ok = false; }
        }
        if (comma < 0) { break; }
        start = comma + 1;
    }
    request->send(200, "text/plain", ok ? "OK" : "ERR: partial delete");
}

void CLASS_MODULE_MACROS::handleRename(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (!request->hasArg("old") || !request->hasArg("new")) {
        request->send(200, "text/plain", "ERR: no args");
        return;
    }
    String oldBase = request->arg("old");
    String newBase = request->arg("new");
    oldBase.trim();
    newBase.trim();
    if (!newBase.endsWith(".tcl")) { newBase += ".tcl"; }

    if (renameFileEntry(oldBase, newBase)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "ERR: rename failed");
    }
}

void CLASS_MODULE_MACROS::handleState(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (!request->hasArg("name") || !request->hasArg("on")) {
        request->send(200, "text/plain", "ERR: no args");
        return;
    }
    String base = request->arg("name");
    base.trim();
    bool on = (request->arg("on") == "1");

    if (setFileRun(base, on)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "ERR: not found");
    }
}

void CLASS_MODULE_MACROS::handlePrio(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (!request->hasArg("name") || !request->hasArg("dir")) {
        request->send(200, "text/plain", "ERR: no args");
        return;
    }
    String base = request->arg("name");
    base.trim();
    int8_t delta = (int8_t)request->arg("dir").toInt();

    if (setFilePrio(base, delta)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "ERR: not found");
    }
}

void CLASS_MODULE_MACROS::handleReload(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (request->hasArg("name")) {
        String base = request->arg("name");
        base.trim();
        int idx = findFile(MACROS_DIR_RE + base);
        if (idx >= 0) {
            // Пересборка произойдёт в tick (перезапуск файла)
            _scriptRev++;
            request->send(200, "text/plain", "OK");
            return;
        }
        request->send(200, "text/plain", "ERR: not found");
        return;
    }
    reloadAll();
    request->send(200, "text/plain", "OK");
}

void CLASS_MODULE_MACROS::handleFire(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);

    if (!request->hasArg("token")) {
        request->send(200, "text/plain", "ERR: no token");
        return;
    }
    uint8_t type = MACRO_ENT_TERM;
    if (request->hasArg("type")) {
        String t = request->arg("type");
        if (t == "button") { type = MACRO_ENT_BUTTON; }
        else if (t == "term") { type = MACRO_ENT_TERM; }
    }
    String token = request->arg("token");
    if (fireToken(type, token)) {
        request->send(200, "text/plain", "OK");
    } else {
        request->send(200, "text/plain", "ERR: queue full");
    }
}

// ============================================================
// Конфиг
// ============================================================

void CLASS_MODULE_MACROS::defaultConfig() {
    _config.enabled = true;
    _fileCount = 0;
}

bool CLASS_MODULE_MACROS::loadConfig() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    if (core_json.jsonFileLoadDoc(CONFIG_FILE_MACROS, doc) == false) { return false; }

    _config.enabled = doc["enabled"].as<bool>();

    _fileCount = 0;
    if (doc["files"].is<JsonArray>()) {
        JsonArray arr = doc["files"].as<JsonArray>();
        for (JsonObject obj : arr) {
            if (_fileCount >= MACRO_MAX_FILES) { break; }
            MacroFile& f = _files[_fileCount];
            f.name    = obj["name"].as<String>();
            f.prio    = obj["prio"].as<uint8_t>();
            f.run     = obj["run"].as<bool>();
            f.created = obj["created"].as<uint32_t>();
            if (f.name.length() == 0) { continue; }
            f.active = false;
            f.err = "";
            f.tcl = NULL;
            f.ctx.file = NULL;
            f.ctx.parsing = false;
            f.nEnts = 0;
            _fileCount++;
        }
    }

    DEBUGMACROS("enabled: %d, files: %d\r\n", _config.enabled, _fileCount);
    return true;
}

bool CLASS_MODULE_MACROS::saveConfig() {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    JsonDocument doc;
    core_json.jsonFileLoadDoc(CONFIG_FILE_MACROS, doc);
    doc["enabled"] = _config.enabled;

    JsonArray arr = doc["files"].to<JsonArray>();
    arr.clear();
    for (uint8_t i = 0; i < _fileCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]    = _files[i].name;
        obj["prio"]    = _files[i].prio;
        obj["run"]     = _files[i].run;
        obj["created"] = _files[i].created;
    }
    return core_json.jsonFileSaveDoc(CONFIG_FILE_MACROS, doc);
}

bool CLASS_MODULE_MACROS::saveMeta() {
    return saveConfig();
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_MODULE_MACROS::getVersionStr() { return String(MODULE_MACROS_VERSION); }
String CLASS_MODULE_MACROS::getGeneratedTime() { return String(MODULE_MACROS_GENERATED_TIME); }
String CLASS_MODULE_MACROS::getCommitDateStr() { return String(MODULE_MACROS_COMMIT_DATE_STR); }

void CLASS_MODULE_MACROS::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGMACROS("%s\r\n", __FUNCTION__);
    String values = "";
    values += "macrosversion|" + getVersionStr()    + "|div\n";
    values += "macrosgentime|" + getGeneratedTime() + "|div\n";
    values += "macrosgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Логика работы со списком файлов
// ============================================================

int CLASS_MODULE_MACROS::findFile(const String& name) {
    for (uint8_t i = 0; i < _fileCount; i++) {
        if (_files[i].name == name) { return i; }
    }
    return -1;
}

bool CLASS_MODULE_MACROS::nameOk(const String& name) {
    if (name.length() < 6 || name.length() > 48) { return false; }
    if (!name.endsWith(".tcl")) { return false; }
    for (uint8_t i = 0; i < name.length(); i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        if (!ok) { return false; }
    }
    return true;
}

void CLASS_MODULE_MACROS::bumpMeta() {
    _metaRev++;
    _scriptRev++;
}

String CLASS_MODULE_MACROS::readFile(const String& path) {
    File f = _fs->open(path, "r");
    if (!f) { return ""; }
    String content;
    while (f.available()) {
        content += (char)f.read();
        if (content.length() > 8192) { break; }
    }
    f.close();
    return content;
}

bool CLASS_MODULE_MACROS::writeFile(const String& path, const String& data) {
    File f = _fs->open(path, "w");
    if (!f) { return false; }
    bool ok = (f.write((const uint8_t*)data.c_str(), data.length()) == data.length());
    f.close();
    return ok;
}

bool CLASS_MODULE_MACROS::fsRenameFile(const String& oldPath, const String& newPath) {
    String data = readFile(oldPath);
    if (data.length() == 0) { return false; }
    if (writeFile(newPath, data) == false) { return false; }
    _fs->remove(oldPath);
    return true;
}

String CLASS_MODULE_MACROS::uniqueNewName(const String& tmpl) {
    String base = tmpl.substring(0, tmpl.length() - 4); // без ".tcl"
    String cand = tmpl;
    uint8_t idx = 0;
    while (_fs->exists(MACROS_DIR_RE + cand)) {
        idx++;
        cand = base + String(idx) + ".tcl";
        if (idx > 99) { break; }
    }
    return cand;
}

void CLASS_MODULE_MACROS::ensureMacrosDir() {
    // Создаём каталог сценариев (примеры .tcl поставляются в составе FS-образа:
    // web/macros/*.tcl -> /macros/*.tcl, см. python/fs_builder.py)
    if (_fs->exists(MACROS_DIR) == false) {
        _fs->mkdir(MACROS_DIR);
        DEBUGMACROS("%s: dir %s created\r\n", __FUNCTION__, MACROS_DIR);
    }
}

void CLASS_MODULE_MACROS::reconcileList() {
    // 1) Удаляем записи, файлы которых пропали
    for (int i = (int)_fileCount - 1; i >= 0; i--) {
        if (_fs->exists(_files[i].name) == false) {
            destroyScript(_files[i]);
            for (int j = i; j < (int)_fileCount - 1; j++) { _files[j] = _files[j + 1]; }
            _fileCount--;
        }
    }

    // 2) Сканируем каталог и добавляем новые .tcl
#if defined(ESP32)
    File root = _fs->open(MACROS_DIR);
    if (root) {
        File entry = root.openNextFile();
        while (entry) {
            if (entry.isDirectory() == false) {
                String base = macroFileBaseName(String(entry.name()));
                if (base.endsWith(".tcl")) {
                    if (findFile(MACROS_DIR_RE + base) < 0) { addFileEntry(base, 7, false); }
                }
            }
            entry = root.openNextFile();
        }
    }
#endif
#if defined(ESP8266)
    Dir dir = _fs->openDir(MACROS_DIR);
    while (dir.next()) {
        if (dir.isDirectory() == false) {
            String base = macroFileBaseName(dir.fileName());
            if (base.endsWith(".tcl")) {
                if (findFile(MACROS_DIR_RE + base) < 0) { addFileEntry(base, 7, false); }
            }
        }
    }
#endif

    saveConfig();
    DEBUGMACROS("%s: total %d files\r\n", __FUNCTION__, _fileCount);
}

// ============================================================
// Операции со списком (общие для HTTP и терминала)
// ============================================================

bool CLASS_MODULE_MACROS::addFileEntry(const String& base, uint8_t prio, bool run) {
    if (nameOk(base) == false) { return false; }
    String path = MACROS_DIR_RE + base;
    if (findFile(path) >= 0) { return true; }
    if (_fileCount >= MACRO_MAX_FILES) { return false; }

    MacroFile& f = _files[_fileCount];
    f.name    = path;
    f.prio    = (prio > 7) ? 7 : prio;
    f.run     = run;
    f.created = (uint32_t)now();
    f.active  = false;
    f.err     = "";
    f.tcl     = NULL;
    f.ctx.file = &f;
    f.ctx.parsing = false;
    f.nEnts   = 0;
    _fileCount++;
    return true;
}

bool CLASS_MODULE_MACROS::createNewFile(const String& base, String& fullPath) {
    if (nameOk(base) == false) { return false; }

    String cand = uniqueNewName(base);
    fullPath = MACROS_DIR_RE + cand;

    String empty;
    empty = "# New macro script (Tcl). Register cron/cond/button/term rules.\r\n";
    if (writeFile(fullPath, empty) == false) { return false; }

    if (addFileEntry(cand, 7, false) == false) {
        _fs->remove(fullPath);
        return false;
    }
    saveConfig();
    bumpMeta();
    return true;
}

bool CLASS_MODULE_MACROS::deleteFileEntry(const String& base) {
    String path = MACROS_DIR_RE + base;
    int idx = findFile(path);
    if (idx < 0) { return false; }

    if (_fs->exists(path)) { _fs->remove(path); }

    destroyScript(_files[idx]);
    for (int j = idx; j < (int)_fileCount - 1; j++) { _files[j] = _files[j + 1]; }
    _fileCount--;
    saveConfig();
    bumpMeta();
    return true;
}

bool CLASS_MODULE_MACROS::setFileRun(const String& base, bool on) {
    String path = MACROS_DIR_RE + base;
    int idx = findFile(path);
    if (idx < 0) { return false; }

    if (_files[idx].run != on) {
        _files[idx].run = on;
        if (on == false) {
            _files[idx].err = "";
            destroyScript(_files[idx]);
        }
        saveConfig();
        bumpMeta();
    }
    return true;
}

bool CLASS_MODULE_MACROS::setFilePrio(const String& base, int8_t delta) {
    String path = MACROS_DIR_RE + base;
    int idx = findFile(path);
    if (idx < 0) { return false; }

    int8_t p = (int8_t)_files[idx].prio + delta;
    if (p < 0) { p = 0; }
    if (p > 7) { p = 7; }
    _files[idx].prio = (uint8_t)p;
    saveConfig();
    return true;
}

bool CLASS_MODULE_MACROS::renameFileEntry(const String& oldBase, const String& newBase) {
    if (nameOk(newBase) == false) { return false; }
    String oldPath = MACROS_DIR_RE + oldBase;
    String newPath = MACROS_DIR_RE + newBase;
    int idx = findFile(oldPath);
    if (idx < 0) { return false; }
    if (newBase == oldBase) { return true; }
    if (_fs->exists(newPath)) { return false; }

    if (fsRenameFile(oldPath, newPath) == false) { return false; }

    _files[idx].name = newPath;
    _files[idx].err = "";
    saveConfig();
    bumpMeta();
    return true;
}

// ============================================================
// Публичное API
// ============================================================

bool CLASS_MODULE_MACROS::reloadAll() {
    bumpMeta();
    return true;
}

bool CLASS_MODULE_MACROS::fireToken(uint8_t type, const String& spec) {
    if (spec.length() == 0) { return false; }
    if (type != MACRO_ENT_TERM && type != MACRO_ENT_BUTTON) { return false; }
    uint8_t next = (uint8_t)((_evIn + 1) % MACRO_EV_QUEUE);
    if (next == _evOut) { return false; } // очередь заполнена
    _evQueue[_evIn].type = type;
    _evQueue[_evIn].spec = spec;
    _evIn = next;
    return true;
}

uint8_t CLASS_MODULE_MACROS::getFileCount() {
    return _fileCount;
}

void CLASS_MODULE_MACROS::printList() {
    Serial.printf("[MACRO] enabled: %d, files: %d\r\n", _config.enabled, _fileCount);
    for (uint8_t i = 0; i < _fileCount; i++) {
        MacroFile& f = _files[i];
        Serial.printf("  [%d] prio=%d run=%d active=%d %s (%d ent)%s%s\r\n",
                      i,
                      f.prio,
                      f.run ? 1 : 0,
                      f.active ? 1 : 0,
                      f.name.c_str(),
                      f.nEnts,
                      f.err.length() > 0 ? " err=" : "",
                      f.err.c_str());
    }
}

// ============================================================
// Периодическая задача EERTOS (раз в секунду)
// ============================================================

void macroTickTask() {
    module_macros.tickStep();
    SetTimerTask(macroTickTask, 1000);
}

// ============================================================
// Вспомогательные утилиты
// ============================================================

static String macroFileBaseName(const String& pathOrName) {
    int slash = pathOrName.lastIndexOf('/');
    if (slash >= 0) { return pathOrName.substring(slash + 1); }
    return pathOrName;
}

static String macroJsonEscape(const String& s) {
    String out;
    for (uint8_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// ============================================================
// Терминальные команды
// ============================================================

void macroCmd() {
    String arg = term.getNext();

    if (arg == "list") {
        module_macros.printList();
        return;
    }
    if (arg == "reload") {
        String name = term.getNext();
        if (name.length() == 0) {
            module_macros.reloadAll();
            Serial.println("[MACRO] reload requested");
            return;
        }
        // Перезапуск одного файла (пересборка произойдёт в tick)
        int idx = module_macros.findFile(String(MACROS_DIR_RE) + name);
        if (idx >= 0) {
            module_macros._scriptRev++;
            Serial.println("[MACRO] reload requested for " + name);
        } else {
            Serial.println("[MACRO] not found");
        }
        return;
    }
    if (arg == "run") {
        String name = term.getNext();
        if (name.length() == 0) { Serial.println("Usage: macro run <file.tcl>"); return; }
        if (module_macros.setFileRun(name, true)) { Serial.println("[MACRO] run " + name); }
        else { Serial.println("[MACRO] not found"); }
        return;
    }
    if (arg == "stop") {
        String name = term.getNext();
        if (name.length() == 0) { Serial.println("Usage: macro stop <file.tcl>"); return; }
        if (module_macros.setFileRun(name, false)) { Serial.println("[MACRO] stop " + name); }
        else { Serial.println("[MACRO] not found"); }
        return;
    }
    if (arg == "prio") {
        String name = term.getNext();
        String dir = term.getNext();
        if (name.length() == 0 || dir.length() == 0) {
            Serial.println("Usage: macro prio <file.tcl> <+1|-1>");
            return;
        }
        int8_t d = (int8_t)dir.toInt();
        if (module_macros.setFilePrio(name, d)) { Serial.println("[MACRO] prio updated"); }
        else { Serial.println("[MACRO] not found"); }
        return;
    }
    if (arg == "msg" || arg == "btn") {
        uint8_t type = (arg == "msg") ? MACRO_ENT_TERM : MACRO_ENT_BUTTON;
        const char* what = (type == MACRO_ENT_TERM) ? "term" : "button";

        // Собираем ВСЕ слова аргументов в одну строку (спецификатор с параметрами)
        String spec;
        while (true) {
            String w = term.getNext();
            if (w.length() == 0) { break; }
            if (spec.length() > 0) { spec += " "; }
            spec += w;
        }
        if (spec.length() == 0) {
            Serial.println("Usage: macro msg <term-specifier> | macro btn <button-specifier>");
            return;
        }
        if (module_macros.fireToken(type, spec)) {
            Serial.println("[MACRO] event " + String(what) + " \"" + spec + "\" queued");
        } else {
            Serial.println("[MACRO] queue full");
        }
        return;
    }

    Serial.println("Commands:");
    Serial.println("  macro list                        - show scenarios");
    Serial.println("  macro reload [file.tcl]           - re-read scenario(s)");
    Serial.println("  macro run <file.tcl>              - start scenario");
    Serial.println("  macro stop <file.tcl>             - stop scenario");
    Serial.println("  macro prio <file.tcl> <delta>     - change priority");
    Serial.println("  macro msg <word> [params...]      - fire term-rules by full match");
    Serial.println("  macro btn <name> [params...]      - fire button-rules by full match");
}

void macroTerminalRegister() {
    term.addCommand("macro", macroCmd);
}
