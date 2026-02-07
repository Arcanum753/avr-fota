#ifndef _MODULENTP_h
#define _MODULENTP_h

#include "main.h"
#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>



#ifdef DEBUG_NTP
#define DEBUGNTP(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGNTP(...)
#endif


#define CONFIG_FILE_NTP             "/config_ntp.json"


#define NTPSERVER_DFLT0 "pool.ntp.org";
#define NTPSERVER_DFLT1 "0.ru.pool.ntp.org";
#define NTPSERVER_DFLT2 "0.gentoo.pool.ntp.org";
#define NTP_TIMEOUT 5000 // milliseconds

const char Page_GeneralNtp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/ntp.html">
Please Wait....Configuring.
)=====";

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
    
    void begin ();
    void ntpOnConnected ();
    void ntpOnDisconected ();
    void webInit();
    
    void ntpSwitchReserv ();
    void ntpOnSyncHandler(NTPSyncEvent_t event);
    
    //CFG
    bool load_config_NTP();
    bool save_configNTP();
    void defaultConfigNTP();
    
    // WEB
    
    void send_NTP_info_html(AsyncWebServerRequest *request) ;
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_html(AsyncWebServerRequest *request);
    void sendTimeData();
    
    
    
    protected: 
    bool updateTimeFromNTP  = false;
    int _ntpServerCount         = 0;
    String _ntpServerNow = "";
    bool  dumb = false;
};

extern NTPMOD_CLASS modNtpClass;





#endif // _MODULENTP_h