#ifndef _MODOTA_h
#define _MODOTA_h

#include "main.h"

#ifdef DEBUG_OTA
#define DEBUGOTASER Serial
#define DEBUGOTA(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGOTA(...)
#endif


#define OTA_STR_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_STR_FILENAME_FILESYSTEM         "spiffs.bin"
#define OTA_STR_FILESYSTEM                  "FILESYSTEM"
#define OTA_STR_FIRMWARE                    "FIRMWARE"
#define OTA_STR_UNSUPPORTED                 "UNSUPPORTED"
#define OTA_STR_NAMEMATCH                   "MATCHED"
#define OTA_STR_NAMEDIFF                   "DIFFERENCE"


// #define FILE_TYPE_UNKNOWN -1
// #define FILE_TYPE_FIRMWARE 0
// #define FILE_TYPE_FILESYSTEM 1

enum UpdateTypeFile {
       FILE_TYPE_UNSUPPORTED = -1
    ,  FILE_TYPE_FIRMWARE = 0
    ,  FILE_TYPE_FILESYSTEM = 1
  };


// Структура для результатов сравнения
struct fileCompareResult {
    int8_t  nameMatch;      // -1 - не совпадает, 1 - совпадает
    int8_t  majorDiff;      // мажорная версия
    int16_t  coreDiff;      // разница в версии ядра (0 если равно, >0 если у файла больше, <0 если у файла меньше)
    int16_t  moduleDiff;    // разница в версии модуля
    int32_t  buildDiff;     // разница в номере сборки 
    UpdateTypeFile   fileType;      // -1 неизвестно, 0 прошивка, 1 ФС
};


class  MODULE_OTA_CLASS    {
public:
    MODULE_OTA_CLASS (bool _in);

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
    
    void html_filename_check(AsyncWebServerRequest *request);
    
    int8_t fileNameCheck (String filename, fileCompareResult* result) ;
    
    void html_md5_set(AsyncWebServerRequest *request);
    void uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void updateFileExecute (AsyncWebServerRequest *request) ;




protected: 
    bool  dumb = false;
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";


    bool ConfigureOTA( String _hostname, String _password) ;
    uint16_t percentLoadedPrev ;
    uint32_t maxSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint32_t freeSketchSpace   ;


};

extern MODULE_OTA_CLASS modOtaClass;




#endif // _MODOTA_h
