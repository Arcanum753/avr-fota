
#include "FSWebServerLib.h"
#include "core_editor.h"
#include "main.h"
#include "core_editor_version.h"

CLASS_CORE_EDITOR core_editor;

CLASS_CORE_EDITOR::CLASS_CORE_EDITOR() {}

#if defined(ESP32)
void CLASS_CORE_EDITOR::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
void CLASS_CORE_EDITOR::setFs(FS* fs)
#endif
{
    _fs = fs;
}

// ============================================================
// begin()
// ============================================================
void CLASS_CORE_EDITOR::begin() {
    DEBUGEDIT(__FUNCTION__); DEBUGEDIT("\r\n");
}

void CLASS_CORE_EDITOR::begin(ModContext& ctx) {
    _fs = ctx.fs;
    begin();
}

// ============================================================
// web_Init()
// ============================================================
void CLASS_CORE_EDITOR::web_Init() {
    DEBUGEDIT(__FUNCTION__); DEBUGEDIT("\r\n");

    ESPHTTPServer.on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFileList(request);
    });

    ESPHTTPServer.on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        if (!ESPHTTPServer.handleFileRead("/edit.html", request)) {
            request->send(404, "text/plain", "FileNotFound");
        }
    });

    ESPHTTPServer.on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFileCreate(request);
    });

    ESPHTTPServer.on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFileDelete(request);
    });

    ESPHTTPServer.on("/edit", HTTP_POST,
        [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); },
        [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            this->handleFileUpload(request, filename, index, data, len, final);
        });

    ESPHTTPServer.on("/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

    ESPHTTPServer.on("/fsinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        this->handleFsInfo(request);
    });
}

// ============================================================
// Веб-обработчики
// ============================================================
void CLASS_CORE_EDITOR::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File fsUploadFile;
    static size_t fileSize = 0;

    if (!index) {
        fileSize = 0;
        DEBUGEDIT("handleFileUpload Name: %s\r\n", filename.c_str());
        if (!filename.startsWith("/")) filename = "/" + filename;
        if (filename.indexOf("..") >= 0) {
            request->send(403, "text/plain", "FORBIDDEN");
            return;
        }
        fsUploadFile = _fs->open(filename, "w");
        DEBUGEDIT("First upload part.\r\n");
    }

    if (fsUploadFile) {
        if (fileSize + len > MAX_UPLOAD_SIZE) {
            DEBUGEDIT("Upload exceeds max size. Aborting.\r\n");
            fsUploadFile.close();
            _fs->remove(filename);
            fileSize = 0;
            return;
        }
        if (fsUploadFile.write(data, len) != len) {
            DEBUGEDIT("Write error during upload.\r\n");
        } else {
            fileSize += len;
        }
    }

    if (final) {
        if (fsUploadFile) { fsUploadFile.close(); }
        DEBUGEDIT("handleFileUpload Size: %u\n", fileSize);
        fileSize = 0;
    }
}

void CLASS_CORE_EDITOR::handleFileList(AsyncWebServerRequest *request) {
    if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }
    String path = request->arg("dir");
    DEBUGEDIT("handleFileList: %s\r\n", path.c_str());
    String output = "[";

#ifdef ESP32
    File root = _fs->open(path);
    File file = root.openNextFile();
    while (file) {
        if (output != "[") { output += ','; }
        bool isDir = file.isDirectory();
        output += "{\"type\":\"";
        output += (isDir) ? "dir" : "file";
        output += "\",\"name\":\"";
        output += escapeJsonStr(String(file.name()));
        output += "\",\"size\":";
        output += isDir ? "0" : String(file.size());
        output += "}";
        file = root.openNextFile();
    }
#endif
#ifdef ESP8266
    Dir dir = _fs->openDir(path);
    while (dir.next()) {
        File entry = dir.openFile("r");
        if (!entry) { continue; }
        if (output != "[") { output += ','; }
        bool isDir = false;
        output += "{\"type\":\"";
        output += (isDir) ? "dir" : "file";
        output += "\",\"name\":\"";
        String entryName = String(entry.name());
        if (entryName.startsWith("/")) entryName = entryName.substring(1);
        output += escapeJsonStr(entryName);
        output += "\",\"size\":";
        output += String(entry.size());
        output += "}";
        entry.close();
    }
#endif

    output += "]";
    DEBUGEDIT("%s\r\n", output.c_str());
    request->send(200, "text/json", output);
}

void CLASS_CORE_EDITOR::handleFileCreate(AsyncWebServerRequest *request) {
    if (request->args() == 0) { return request->send(500, "text/plain", "BAD ARGS"); }
    String path = request->arg(0U);
    DEBUGEDIT("handleFileCreate: %s\r\n", path.c_str());
    if (path == "/")             { return request->send(500, "text/plain", "BAD PATH"); }
    if (path.indexOf("..") >= 0) { return request->send(403, "text/plain", "FORBIDDEN"); }
    if (_fs->exists(path))       { return request->send(500, "text/plain", "FILE EXISTS"); }
    File file = _fs->open(path, "w");
    if (file) { file.close(); }
    else      { return request->send(500, "text/plain", "CREATE FAILED"); }
    request->send(200, "text/plain", "");
}

void CLASS_CORE_EDITOR::handleFileDelete(AsyncWebServerRequest *request) {
    if (request->args() == 0) { return request->send(500, "text/plain", "BAD ARGS"); }
    String path = request->arg(0U);
    DEBUGEDIT("handleFileDelete: %s\r\n", path.c_str());
    if (path == "/")             { return request->send(500, "text/plain", "BAD PATH"); }
    if (path.indexOf("..") >= 0) { return request->send(403, "text/plain", "FORBIDDEN"); }
    if (!_fs->exists(path))      { return request->send(404, "text/plain", "FileNotFound"); }
    _fs->remove(path);
    request->send(200, "text/plain", "");
}

void CLASS_CORE_EDITOR::handleFsInfo(AsyncWebServerRequest *request) {
    DEBUGEDIT("%s\n\r", __FUNCTION__);
    size_t totalBytes = 0;
    size_t usedBytes = 0;
#if defined(ESP32)
    totalBytes = _fs->totalBytes();
    usedBytes = _fs->usedBytes();
#endif
#if defined(ESP8266)
    FSInfo fi;
    if (_fs->info(fi)) {
        totalBytes = fi.totalBytes;
        usedBytes = fi.usedBytes;
    }
#endif
    size_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
    String json = "{\"total\":";
    json += String(totalBytes);
    json += ",\"used\":";
    json += String(usedBytes);
    json += ",\"free\":";
    json += String(freeBytes);
    json += "}";
    request->send(200, "application/json", json);
}

// ============================================================
// Версионные методы
// ============================================================
String CLASS_CORE_EDITOR::getVersionStr() {
    return String(CORE_EDITOR_VERSION);
}

String CLASS_CORE_EDITOR::getGeneratedTime() {
    return String(CORE_EDITOR_GENERATED_TIME);
}

String CLASS_CORE_EDITOR::getCommitDateStr() {
    return String(CORE_EDITOR_COMMIT_DATE_STR);
}

void CLASS_CORE_EDITOR::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGEDIT("%s\n\r", __FUNCTION__);
    String values = "";
    values += "edtversion|" + getVersionStr()    + "|div\n";
    values += "edtgentime|" + getGeneratedTime() + "|div\n";
    values += "edtgendate|" + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

String CLASS_CORE_EDITOR::escapeJsonStr(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s.charAt(i);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
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
