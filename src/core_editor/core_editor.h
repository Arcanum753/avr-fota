#ifndef _MODULE_EDITOR_h
#define _MODULE_EDITOR_h
#include "main.h"


#ifdef DEBUG_EDITOR
#define DEBUGEDIT(...) DBG_MOD("[C_EDITOR] ", __VA_ARGS__)
#else
#define DEBUGEDIT(...)
#endif

#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#include "mod_context.h"




class CLASS_CORE_EDITOR {
public:
    CLASS_CORE_EDITOR();
#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif
    void begin();
    void web_Init();
    void begin(ModContext& ctx);

    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

private:
    String escapeJsonStr(const String& s);
    void handleFileList(AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    void handleFsInfo(AsyncWebServerRequest *request);
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void html_ver_get(AsyncWebServerRequest *request);

protected:
    static const size_t MAX_UPLOAD_SIZE = 1024 * 1024; // 1MB
#if ESP32
    fs::LittleFSFS* _fs;
#elif defined(ESP8266)
    FS* _fs;
#endif
};

extern CLASS_CORE_EDITOR core_editor;




#endif // _MODULE_EDITOR_h
