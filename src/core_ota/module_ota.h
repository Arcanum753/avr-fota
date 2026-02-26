#ifndef _MODOTA_h
#define _MODOTA_h

#include "main.h"

#ifdef DEBUG_OTA
#define DEBUGOTASER Serial
#define DEBUGOTA(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGOTA(...)
#endif


#define OTA_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_FILENAME_FILESYSTEM         "spiffs.bin"
#define OTA_FIRMWARE                    "FIRMWARE"
#define OTA_FILESYSTEM                  "FILESYSTEM"
#define OTA_UNSUPPORTED                 "UNSUPPORTED"


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

    void send_update_firmware_values_html(AsyncWebServerRequest *request);
    void setUpdateMD5(AsyncWebServerRequest *request);
    void uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void updateFileExecute (AsyncWebServerRequest *request) ;




protected: 
    bool  dumb = false;
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";


private:
    bool ConfigureOTA( String _hostname, String _password) ;
    uint16_t percentLoadedPrev ;
    uint32_t maxSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint32_t freeSketchSpace   ;


};

extern MODULE_OTA_CLASS modOtaClass;




#endif // _MODOTA_h
