#ifndef _MODOTA_h
#define _MODOTA_h

#include "main.h"

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


// Структура для результатов сравнения (новый формат MAJOR.MINOR.DATE.BUILD)
struct fileCompareResult {
    int8_t  nameMatch;      // -1 - не проверялось, 0 - не совпадает, 1 - совпадает
    int8_t  majorDiff;      // разница в мажорной версии
    int16_t minorDiff;      // разница в минорной версии (инкремент при коммитах)
    int64_t  dateDiff;       // разница в дате (YYYYMMDDHHMM как число)
    int32_t buildDiff;      // разница в номере сборки
    UpdateTypeFile fileType; // тип файла
    uint8_t isDebug;        // 1 - если есть номер билда в имени, 0 - если нет
};


class  CORE_OTA_CLASS    {
public:
    CORE_OTA_CLASS (bool _in);

#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif

#if ESP32
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif

    void begin(String _hostname, String _password);
    void webInit() ;
    void loopHandler() ;
    
    void html_md5_set(AsyncWebServerRequest *request);
    
    void html_filename_check(AsyncWebServerRequest *request);
    int8_t fileNameCheck (String filename, fileCompareResult* result) ;
    

    void html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void html_fileuploadProgress(AsyncWebServerRequest *request);
    
    void updateFileExecute (AsyncWebServerRequest *request) ;
    
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
protected: 
    uint16_t fileUpadedpercent = 0;
    bool  dumb = false;
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";

private:
    bool isValidFilename(const String& filename);
    bool ConfigureOTA( String _hostname, String _password) ;
    uint16_t percentLoadedPrev ;
    uint32_t maxSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint32_t freeSketchSpace   ;


};

extern CORE_OTA_CLASS modOtaClass;




#endif // _MODOTA_h
