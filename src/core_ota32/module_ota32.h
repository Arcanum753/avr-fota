#ifndef _MODOTA_h
#define _MODOTA_h

#include "main.h"
#include <ESPAsyncWebServer.h>
#ifdef DEBUG_OTA
#define DEBUGOTASER Serial
#define DEBUGOTA(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGOTA(...)
#endif

#ifdef DEBUG_OTALOAD
#define DEBUGLOAD(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOAD(...)
#endif

#define OTA_STR_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_STR_FILENAME_FILESYSTEM         "spiffs.bin"
#define OTA_STR_FILESYSTEM                  "FILESYSTEM"
#define OTA_STR_FIRMWARE                    "FIRMWARE"
#define OTA_STR_UNSUPPORTED                 "UNSUPPORTED"
#define OTA_STR_NAMEMATCH                   "MATCHED"
#define OTA_STR_NAMEDIFF                    "UNMATCHED"

enum UpdateTypeFile {
       FILE_TYPE_UNSUPPORTED = -1
    ,  FILE_TYPE_FIRMWARE = 0
    ,  FILE_TYPE_FILESYSTEM = 1
  };

struct fileCompareResult {
    int8_t  nameMatch;
    int8_t  majorDiff;
    int16_t minorDiff;
    int32_t dateDiff;
    int32_t buildDiff;
    UpdateTypeFile fileType;
    uint8_t isDebug;
};

class MODULE_OTA_CLASS {
public:
    MODULE_OTA_CLASS (bool _in);

#if defined(ESP32)
    fs::SPIFFSFS* _fs;
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    FS* _fs;
    void setFs(FS* fs);
#endif

    void begin(String _hostname, String _password);
    void webInit();
    void loopHandler();
    
    void html_md5_set(AsyncWebServerRequest *request);
    void html_filename_check(AsyncWebServerRequest *request);
    int8_t fileNameCheck(String filename, fileCompareResult* result);
    void html_fileuploadProgress(AsyncWebServerRequest *request);
    void updateFileExecute(AsyncWebServerRequest *request);

protected: 
    uint16_t fileUpadedpercent = 0;
    bool dumb = false;
    uint32_t _updateFileSize = 0;
    String _browserFileMD5 = "";
    String _updateFileName = "";

private:
    void html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    bool isValidFilename(const String& filename);
    bool ConfigureOTA(String _hostname, String _password);
    void html_chipinfo(AsyncWebServerRequest *request);
    uint16_t percentLoadedPrev;
    uint32_t maxSketchSpace;
    void prepareSizesForUpdate();
    UpdateTypeFile typeOTAfile;
    uint32_t freeSketchSpace;
};

extern MODULE_OTA_CLASS modOtaClass;

#endif