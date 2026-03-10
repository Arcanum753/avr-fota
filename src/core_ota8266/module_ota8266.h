#ifndef MODULE_OTA8266_H
#define MODULE_OTA8266_H

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

// Константы для совместимости с update.html
#define OTA_STR_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_STR_FILENAME_FILESYSTEM         "spiffs.bin"
#define OTA_STR_FILESYSTEM                  "FILESYSTEM"
#define OTA_STR_FIRMWARE                    "FIRMWARE"
#define OTA_STR_UNSUPPORTED                  "UNSUPPORTED"
#define OTA_STR_NAMEMATCH                    "MATCHED"
#define OTA_STR_NAMEDIFF                     "UNMATCHED"

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

class MODULE_OTA8266_CLASS {
public:
    MODULE_OTA8266_CLASS();
    
    void begin(String hostname, String password);
    void loopHandler();
    void webInit();
    
    // Методы для обработки запросов (как в ESP32 версии)
    void html_md5_set(AsyncWebServerRequest *request);
    void html_filename_check(AsyncWebServerRequest *request);
    void html_fileuploadProgress(AsyncWebServerRequest *request);
    void updateFileExecute(AsyncWebServerRequest *request);
    void html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

private:
    bool ConfigureOTA(String hostname, String password);
    bool isValidFilename(const String& filename);
    int8_t fileNameCheck(String filename, fileCompareResult* result);
    void prepareSizesForUpdate();
    
    // Переменные состояния
    bool updateStarted;
    bool uploadError;
    bool responseSent;
    uint16_t fileUpadedpercent;
    uint16_t percentLoadedPrev;
    long totalFileSize;
    uint32_t maxSketchSpace;
    uint32_t freeSketchSpace;
    uint32_t fileSize;
    String _browserFileMD5;
    String _updateFileName;
    UpdateTypeFile typeOTAfile;
};

extern MODULE_OTA8266_CLASS modOta8266;

#endif