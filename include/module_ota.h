#ifndef _MODOTA_h
#define _MODOTA_h



class  MODULE_OTA_CLASS    {
    public:
    MODULE_OTA_CLASS (bool _in);
    #if defined(ESP32)  
    void begin(fs::SPIFFSFS* fs);
    #elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
    #endif


    protected: 
    bool  dumb = false;
    
};

extern MODULE_OTA_CLASS ModOtaClass;




#endif // _MODOTA_h
