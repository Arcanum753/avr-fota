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


#define OTA_STR_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_STR_FILENAME_FILESYSTEM         "littlefs.bin"
#define OTA_STR_FILESYSTEM                  "FILESYSTEM"
#define OTA_STR_FIRMWARE                    "FIRMWARE"
#define OTA_STR_UNSUPPORTED                 "UNSUPPORTED"
#define OTA_STR_NAMEMATCH                   "MATCHED"
#define OTA_STR_NAMEDIFF                    "UNMATCHED"

// Разделители имён прошивок/ФС в именах файлов: <ENV>-FIRMWARE-<версия>.bin
#define OTA_STR_SEPARATOR_FIRMWARE          "-FIRMWARE-"
#define OTA_STR_SEPARATOR_FILESYSTEM        "-FILESYS-"

// FS version comparison results
#define FS_VERSION_COMPARE_SAME             "SAME"
#define FS_VERSION_COMPARE_NEWER            "NEWER"
#define FS_VERSION_COMPARE_OLDER            "OLDER"
#define FS_VERSION_COMPARE_MISSING          "NO_JSON"
#define FS_VERSION_COMPARE_ERROR            "ERROR"

enum UpdateTypeFile {
       FILE_TYPE_UNSUPPORTED = -1
    ,  FILE_TYPE_FIRMWARE = 0
    ,  FILE_TYPE_FILESYSTEM = 1
  };


// Structure for file comparison results (MAJOR.MINOR.DATE.BUILD format)
struct fileCompareResult {
    int8_t  nameMatch;      // -1 not checked, 0 not match, 1 match
    int8_t  majorDiff;      // major version difference
    int16_t minorDiff;      // minor version difference (increments on commits)
    int64_t dateDiff;       // date difference (YYYYMMDDHHMM as number)
    int32_t buildDiff;      // build number difference
    UpdateTypeFile fileType; // file type
    uint8_t isDebug;        // 1 if build number present in filename
    // FS version comparison (for filesystem updates)
    int8_t fsVersionCompare;   // compare result against existing FS or firmware
    int64_t fsCurrentDate;     // current FS date (or firmware date)
    int32_t fsCurrentBuild;    // current FS build (or firmware build)
    int32_t fsCurrentMajor;    // current FS major version
    int32_t fsCurrentMinor;    // current FS minor version
};


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

// ============================================================
// Кассета светодиодной индикации обновления
// (общая для core_ota и module_otaclient)
// ============================================================
void ledMacrosUpdateFirmware();
void ledMacrosUpdateFilesystem();
void ledMacrosUpdateError();

// Global flag: set to true when FS has been ended by OTA update process
// Prevents double _fs->end() in restart_esp() which causes crash (LoadStoreAlignmentCause)
extern bool _ota_fsEndCalled;

#endif // _MODOTA_h