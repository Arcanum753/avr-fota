#ifndef _CORE_FS_H
#define _CORE_FS_H

#include "main.h"

#include "mod_context.h"
#include "core_fs_engine.h"
#include "core_fs_types.h"

#ifdef DEBUG_FS
#define DEBUGFS(...) DBG_MOD("[C_FS] ", __VA_ARGS__)
#else
#define DEBUGFS(...)
#endif

class CLASS_CORE_FS {
public:
    CLASS_CORE_FS();

    void setFs(CoreFs* fs);
    void begin();
    void begin(ModContext& ctx);
    void web_Init();

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest* request);

    void handleInfo(AsyncWebServerRequest* request);
    void handleList(AsyncWebServerRequest* request);
    void handleDownload(AsyncWebServerRequest* request);
    void handleCreate(AsyncWebServerRequest* request);
    void handleMkdir(AsyncWebServerRequest* request);
    void handleRename(AsyncWebServerRequest* request);
    void handleDelete(AsyncWebServerRequest* request);
    void handleUploadDone(AsyncWebServerRequest* request);
    void handleUploadChunk(AsyncWebServerRequest* request, uint8_t* data,
                           size_t len, size_t index, size_t total);

    bool requireWrite(AsyncWebServerRequest* request);
    bool normalizeArg(AsyncWebServerRequest* request, const char* name, String& out);
    void sendError(AsyncWebServerRequest* request, int code, const char* text);
    void sendText(AsyncWebServerRequest* request, const String& body);
    void resetUpload();
    bool uploadPrepare(AsyncWebServerRequest* request);

protected:
    CoreFs* _fs = nullptr;

    File     _uploadFile;
    bool     _uploadActive = false;
    bool     _uploadOpened = false;
    int      _uploadError = 0;
    String   _uploadPath;
    uint32_t _uploadSize = 0;
};

extern CLASS_CORE_FS core_fs;

#endif // _CORE_FS_H
