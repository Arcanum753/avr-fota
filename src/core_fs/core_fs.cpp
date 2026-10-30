#include "core_web/FSWebServerLib.h"


#include "core_state/core_state.h"
#include "core_sys/core_sys.h"

#include "core_fs.h"
#include "common/common.h"
#include "common_module.h"
#include "version.h"
#include "core_fs_version.h"

CLASS_CORE_FS core_fs;

// ============================================================
// Глобальный объект и инициализация
// ============================================================

CLASS_CORE_FS::CLASS_CORE_FS() {}

void CLASS_CORE_FS::setFs(CoreFs* fs) {
    _fs = fs;
}

void CLASS_CORE_FS::begin() {
    DEBUGFS(__FUNCTION__); DEBUGFS("\r\n");
}

void CLASS_CORE_FS::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ============================================================
// web_Init()
// ============================================================

void CLASS_CORE_FS::web_Init() {
    DEBUGFS(__FUNCTION__); DEBUGFS("\r\n");

    ESPHTTPServer.on("/fs/info", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleInfo(request);
    });

    ESPHTTPServer.on("/fs/list", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleList(request);
    });

    ESPHTTPServer.on("/fs/download", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleDownload(request);
    });

    ESPHTTPServer.on("/fs/create", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleCreate(request);
    });

    ESPHTTPServer.on("/fs/mkdir", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleMkdir(request);
    });

    ESPHTTPServer.on("/fs/rename", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleRename(request);
    });

    ESPHTTPServer.on("/fs/delete", HTTP_POST, [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        handleDelete(request);
    });

    ESPHTTPServer.on("/fs/upload", HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (!ESPHTTPServer.checkAuth(request)) {
                resetUpload();
                return request->requestAuthentication();
            }
            handleUploadDone(request);
        },
        ArUploadHandlerFunction(),
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len,
               size_t index, size_t total) {
            handleUploadChunk(request, data, len, index, total);
        });

    ESPHTTPServer.on("/fs/ver", [this](AsyncWebServerRequest* request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        html_ver_get(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_FS::sendError(AsyncWebServerRequest* request, int code, const char* text) {
    request->send(code, "text/plain", text ? text : "ERROR");
}

void CLASS_CORE_FS::sendText(AsyncWebServerRequest* request, const String& body) {
    request->send(200, "text/plain", body);
}

static void sendJsonBody(AsyncWebServerRequest* request, const String& body) {
    request->send(200, "application/json", body);
}

bool CLASS_CORE_FS::normalizeArg(AsyncWebServerRequest* request, const char* name, String& out) {
    if (!request->hasArg(name)) { return false; }
    out = ns_core_fs::fsNormalizePath(request->arg(name));
    return ns_core_fs::fsPathIsSafe(out);
}

bool CLASS_CORE_FS::requireWrite(AsyncWebServerRequest* request) {
    if (_fs == nullptr) {
        sendError(request, 503, "FS NOT READY");
        return false;
    }
    if (!ns_core_fs::fsWriteAllowed(core_state.getMode())) {
        sendError(request, 409, "BUSY");
        return false;
    }
    return true;
}

void CLASS_CORE_FS::handleInfo(AsyncWebServerRequest* request) {
    if (_fs == nullptr) { return sendError(request, 503, "FS NOT READY"); }

    uint32_t total = 0, used = 0;
    if (!ns_core_fs::fsBuildInfo(*_fs, total, used)) {
        return sendError(request, 500, "FS INFO FAILED");
    }
    uint32_t freeBytes = (total > used) ? (total - used) : 0;
    uint32_t pct = (total > 0) ? (uint32_t)(((uint64_t)used * 100ULL) / total) : 0;

    String json;
    json.reserve(260);
    json += "{\"api\":";
    json += String(CORE_FS_API_VERSION);
    json += ",\"fsType\":\"littlefs\",\"total\":";
    json += String(total);
    json += ",\"used\":";
    json += String(used);
    json += ",\"free\":";
    json += String(freeBytes);
    json += ",\"usedPct\":";
    json += String(pct);
    json += ",\"blockSize\":";
    json += String((uint32_t)CORE_FS_BLOCK_SIZE);
    json += ",\"maxUpload\":";
    json += String((uint32_t)CORE_FS_MAX_UPLOAD);
    json += ",\"fsVersion\":\"";
    json += escapeJson(core_sys.getFsVersionStr());
    json += "\",\"buildEnv\":\"";
#ifdef BUILD_ENV
    json += escapeJson(String(BUILD_ENV));
#endif
    json += "\"}";
    request->send(200, "application/json", json);
}

void CLASS_CORE_FS::handleList(AsyncWebServerRequest* request) {
    if (_fs == nullptr) { return sendError(request, 503, "FS NOT READY"); }

    FsEntry* entries = new FsEntry[CORE_FS_MAX_ENTRIES];
    if (entries == nullptr) { return sendError(request, 503, "LOW MEMORY"); }

    size_t count = 0;
    bool truncated = false;
    ns_core_fs::fsCollectEntries(*_fs, entries, CORE_FS_MAX_ENTRIES, count, truncated);

    String json;
    json.reserve(1024 + count * 64);
    json += "{\"count\":";
    json += String((uint32_t)count);
    json += ",\"truncated\":";
    json += truncated ? "true" : "false";
    json += ",\"entries\":[";
    for (size_t i = 0; i < count; i++) {
        if (i) { json += ','; }
        json += "{\"path\":\"";
        json += escapeJson(entries[i].path);
        json += "\",\"type\":\"";
        json += entries[i].dir ? "dir" : "file";
        json += "\",\"size\":";
        json += String(entries[i].size);
        json += ",\"mtime\":";
        json += String(entries[i].mtime);
        json += "}";
    }
    json += "]}";

    delete[] entries;
    sendJsonBody(request, json);
}

void CLASS_CORE_FS::handleDownload(AsyncWebServerRequest* request) {
    if (_fs == nullptr) { return sendError(request, 503, "FS NOT READY"); }

    String path;
    if (!normalizeArg(request, "path", path)) {
        return sendError(request, 400, "BAD PATH");
    }

    File file = _fs->open(path, "r");
    if (!file) { return sendError(request, 404, "NOT FOUND"); }
    if (file.isDirectory()) {
        file.close();
        return sendError(request, 400, "IS DIRECTORY");
    }
    file.close();

    AsyncWebServerResponse* response =
        request->beginResponse(*_fs, path, "application/octet-stream", true);
    if (response == nullptr) { return sendError(request, 500, "OPEN FAILED"); }
    request->send(response);
}

void CLASS_CORE_FS::handleCreate(AsyncWebServerRequest* request) {
    if (!requireWrite(request)) { return; }

    String path;
    if (!normalizeArg(request, "path", path)) {
        return sendError(request, 400, "BAD PATH");
    }
    if (ns_core_fs::fsIsSecretPath(path)) { return sendError(request, 409, "EXISTS"); }
    if (_fs->exists(path)) { return sendError(request, 409, "EXISTS"); }

    if (!ns_core_fs::fsCreateFile(*_fs, path)) {
        return sendError(request, 500, "CREATE FAILED");
    }
    sendText(request, "OK");
}

void CLASS_CORE_FS::handleMkdir(AsyncWebServerRequest* request) {
    if (!requireWrite(request)) { return; }

    String path;
    if (!normalizeArg(request, "path", path)) {
        return sendError(request, 400, "BAD PATH");
    }
    if (_fs->exists(path)) { return sendError(request, 409, "EXISTS"); }
    if (!ns_core_fs::fsCreateDir(*_fs, path)) {
        return sendError(request, 500, "MKDIR FAILED");
    }
    sendText(request, "OK");
}

void CLASS_CORE_FS::handleRename(AsyncWebServerRequest* request) {
    if (!requireWrite(request)) { return; }

    String from, to;
    if (!normalizeArg(request, "from", from) || !normalizeArg(request, "to", to)) {
        return sendError(request, 400, "BAD PATH");
    }
    if (ns_core_fs::fsIsSecretPath(from) || ns_core_fs::fsIsSecretPath(to)) {
        return sendError(request, 403, "FORBIDDEN");
    }
    if (!_fs->exists(from)) { return sendError(request, 404, "NOT FOUND"); }
    if (_fs->exists(to)) { return sendError(request, 409, "EXISTS"); }
    if (!ns_core_fs::fsRenamePath(*_fs, from, to)) {
        return sendError(request, 500, "RENAME FAILED");
    }
    sendText(request, "OK");
}

void CLASS_CORE_FS::handleDelete(AsyncWebServerRequest* request) {
    if (!requireWrite(request)) { return; }

    String path;
    if (!normalizeArg(request, "path", path)) {
        return sendError(request, 400, "BAD PATH");
    }
    if (ns_core_fs::fsIsSecretPath(path)) { return sendError(request, 403, "FORBIDDEN"); }
    if (!_fs->exists(path)) { return sendError(request, 404, "NOT FOUND"); }

    bool recursive = request->hasArg("recursive") && request->arg("recursive") == "1";
    bool notEmpty = false;
    if (!ns_core_fs::fsRemovePath(*_fs, path, recursive, notEmpty)) {
        if (notEmpty) { return sendError(request, 409, "NOT EMPTY"); }
        return sendError(request, 500, "DELETE FAILED");
    }
    sendText(request, "OK");
}

void CLASS_CORE_FS::resetUpload() {
    if (_uploadFile) { _uploadFile.close(); }
    _uploadActive = false;
    _uploadOpened = false;
    _uploadError = 0;
    _uploadPath = "";
    _uploadSize = 0;
}

bool CLASS_CORE_FS::uploadPrepare(AsyncWebServerRequest* request) {
    if (_uploadActive) {
        _uploadError = 409;
        return false;
    }
    _uploadActive = true;

    if (_fs == nullptr) {
        _uploadError = 503;
        return false;
    }
    if (!ns_core_fs::fsWriteAllowed(core_state.getMode())) {
        _uploadError = 409;
        return false;
    }

    String path;
    if (!normalizeArg(request, "path", path)) {
        _uploadError = 400;
        return false;
    }
    _uploadPath = path;

    File existing = _fs->open(path, "r");
    if (existing) {
        if (existing.isDirectory()) {
            existing.close();
            _uploadError = 400;
            return false;
        }
        existing.close();
    }

    uint32_t total = 0, used = 0;
    if (!ns_core_fs::fsBuildInfo(*_fs, total, used)) {
        _uploadError = 500;
        return false;
    }
    uint32_t freeBytes = (total > used) ? (total - used) : 0;
    uint32_t available = (freeBytes > CORE_FS_FREE_RESERVE)
                         ? (freeBytes - CORE_FS_FREE_RESERVE) : 0;

    size_t declared = request->contentLength();
    if (declared > CORE_FS_MAX_UPLOAD) {
        _uploadError = 413;
        return false;
    }
    if (declared > 0 && declared > available) {
        _uploadError = 507;
        return false;
    }

    _uploadFile = _fs->open(path, "w");
    if (!_uploadFile) {
        _uploadError = 500;
        return false;
    }
    _uploadOpened = true;
    _uploadSize = 0;
    _uploadError = 0;
    return true;
}

void CLASS_CORE_FS::handleUploadChunk(AsyncWebServerRequest* request, uint8_t* data,
                                      size_t len, size_t index, size_t total) {
    if (!ESPHTTPServer.checkAuth(request)) {
        _uploadActive = true;
        _uploadError = 401;
        return;
    }

    if (index == 0) {
        uploadPrepare(request);
    }
    if (!_uploadActive || _uploadError != 0) { return; }
    if (len == 0) { return; }

    uint32_t next = _uploadSize + (uint32_t)len;
    int error = 0;
    if (next > CORE_FS_MAX_UPLOAD) { error = 413; }
    if (total > 0 && index + len > total) { error = 413; }

    if (error == 0 && _uploadFile.write(data, len) != len) { error = 500; }

    if (error != 0) {
        _uploadError = error;
        if (_uploadFile) { _uploadFile.close(); }
        if (_uploadOpened && _uploadPath.length() > 0) { _fs->remove(_uploadPath); }
        _uploadOpened = false;
        return;
    }
    _uploadSize = next;
}

void CLASS_CORE_FS::handleUploadDone(AsyncWebServerRequest* request) {
    if (!_uploadActive) {
        // Пустое тело: создаём 0-byte файл.
        if (!uploadPrepare(request)) {
            int code = _uploadError;
            resetUpload();
            if (code == 413) { return sendError(request, 413, "TOO LARGE"); }
            if (code == 507) { return sendError(request, 507, "NO SPACE"); }
            if (code == 409) { return sendError(request, 409, "BUSY"); }
            if (code == 400) { return sendError(request, 400, "BAD PATH"); }
            return sendError(request, code == 401 ? 401 : 500, "OPEN FAILED");
        }
    }

    int code = _uploadError;
    if (_uploadFile) { _uploadFile.close(); }

    if (code != 0) {
        if (_uploadOpened && _uploadPath.length() > 0) { _fs->remove(_uploadPath); }
        resetUpload();
        if (code == 401) { return request->requestAuthentication(); }
        if (code == 413) { return sendError(request, 413, "TOO LARGE"); }
        if (code == 507) { return sendError(request, 507, "NO SPACE"); }
        if (code == 409) { return sendError(request, 409, "BUSY"); }
        if (code == 400) { return sendError(request, 400, "BAD PATH"); }
        if (code == 503) { return sendError(request, 503, "FS NOT READY"); }
        return sendError(request, 500, "WRITE FAILED");
    }

    resetUpload();
    sendText(request, "OK");
}

// ============================================================
// Версия
// ============================================================

String CLASS_CORE_FS::getVersionStr() {
    return String(CORE_FS_VERSION_STR);
}

String CLASS_CORE_FS::getGeneratedTime() {
    return String(CORE_FS_GENERATED_TIME);
}

String CLASS_CORE_FS::getCommitDateStr() {
    return String(CORE_FS_COMMIT_DATE_STR);
}

void CLASS_CORE_FS::html_ver_get(AsyncWebServerRequest* request) {
    String values;
    values += "fsversion|" + getVersionStr() + "|div\n";
    values += "fsgentime|" + getGeneratedTime() + "|div\n";
    values += "fsgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}
