#ifndef _MTPMOD_h
#define _MTPMOD_h

#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>


#define CONFIG_FILE_NTP             "/config_ntp.json"



#define NTPSERVER_DFLT0 "pool.ntp.org";
#define NTPSERVER_DFLT1 "0.ru.pool.ntp.org";
#define NTPSERVER_DFLT2 "0.gentoo.pool.ntp.org";

typedef struct {
    String ntpServerName0;
    String ntpServerName1;
    String ntpServerName2;
    long updateNTPTimeEvery;
    long timezone;
    bool daylight;
} strNtpConfig;


class  NTPMOD_CLASS    {
    public:
    NTPMOD_CLASS (bool _in);
    #if defined(ESP32)  
    void begin(fs::SPIFFSFS* fs);
    #elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
    #endif
    #if ESP32
    fs::SPIFFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    
    
    strNtpConfig    _ntpConfig; // NTP configuration
    int                 _ntpserveer;
    void ntpBegin ();
    void webInit();
    void ntpBeginReserv ();
    void ntpHandler(NTPSyncEvent_t event);

    bool load_config_NTP();
    
    //ntp
    bool save_configNTP();
    void defaultConfigNTP();

    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_html(AsyncWebServerRequest *request);




protected: 
    bool  dumb = false;
};

extern NTPMOD_CLASS ntpModClass;





#endif // _MTPMOD_h