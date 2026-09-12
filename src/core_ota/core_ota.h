#ifndef _MODOTA_h
#define _MODOTA_h

#include "main.h"

#include "mod_context.h"

#ifdef DEBUG_OTA
#define DEBUGOTASER Serial
#define DEBUGOTA(...) DBG_MOD("[C_OTA] ", __VA_ARGS__)
#else
#define DEBUGOTA(...)
#endif

#ifdef DEBUG_OTALOAD
#define DEBUGLOAD(...) DBG_MOD("[C_OTA] ", __VA_ARGS__)
#else
#define DEBUGLOAD(...)
#endif


#include "core_ota_types.h"
#include "core_ota_led.h"


class  CLASS_CORE_OTA    {
public:

#if ESP32
    fs::LittleFSFS*               _fs = nullptr;
#elif defined(ESP8266)
    FS*                         _fs = nullptr;                  // esp8266/esp32 flash file system
#endif

#if ESP32
    void setFs(fs::LittleFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs);
#endif

    void begin(String _hostname, String _password);
    void begin(ModContext& ctx);
    void register_resources();
    virtual void web_Init();
    void loop();
    
    void html_md5_set(AsyncWebServerRequest *request);
    
    void html_filename_check(AsyncWebServerRequest *request);
    int8_t fileNameCheck (String filename, fileCompareResult* result) ;
    
    // New: compare file version with current FS JSON or firmware
    int8_t compareWithCurrentFsVersion(fileCompareResult* result, const String& filename);
    
    // FS management helpers (reduces code duplication with module_otaclient)
    void fsEnd();
    void fsRemount();
    
    // Version comparison helper: compares diffs and returns -1, 0, or 1
    static int8_t compareVersionDiffs(int32_t majorDiff, int32_t minorDiff, int64_t dateDiff, int32_t buildDiff);
    
    void html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void html_fileuploadProgress(AsyncWebServerRequest *request);
    
    void updateFileExecute (AsyncWebServerRequest *request) ;
    
    // Регистрация общих маршрутов (выделена из web_Init для переопределения в субмодуле)
    void registerCommonRoutes();
    virtual void registerCustomRoutes() {}

    virtual String getVersionStr();
    virtual String getGeneratedTime();
    virtual String getCommitDateStr();
    virtual void  html_ver_get(AsyncWebServerRequest *request);

protected: 
    uint16_t fileUpadedpercent = 0;
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";

    bool isValidFilename(const String& filename);
    bool ConfigureOTA( String _hostname, String _password) ;
    uint16_t percentLoadedPrev ;
    uint32_t maxSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint32_t freeSketchSpace   ;
    
};

extern CLASS_CORE_OTA core_ota;

// Global flag: set to true when FS has been ended by OTA update process
// Prevents double _fs->end() in restart_esp() which causes crash (LoadStoreAlignmentCause)
extern bool _ota_fsEndCalled;

#endif // _MODOTA_h