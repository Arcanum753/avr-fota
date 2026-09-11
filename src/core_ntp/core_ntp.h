#ifndef _MODULENTP_h
#define _MODULENTP_h

#include "main.h"
#include <WiFiClient.h>
#include "common/TimeLib.h"

#include "mod_context.h"


#include "core_ntp/NtpClientLib.h"



#ifdef DEBUG_NTP
#define DEBUGNTP(...) DBG_MOD("[C_NTP] ", __VA_ARGS__)
#else
#define DEBUGNTP(...)
#endif


#define CONFIG_FILE_NTP "/config_ntp.json"


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


class  CLASS_CORE_NTP    {
    public:

    #if ESP32
    fs::LittleFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    
    
    strNtpConfig    _ntpConfig; // NTP configuration
    
    void begin ();
    void begin(ModContext& ctx);
    void ntpOnConnected ();
    void ntpOnDisconected ();
    void register_resources();
    void web_Init();
    
    void ntpSwitchReserv ();
    void ntpOnSyncHandler(NTPSyncEvent_t event);
    
    //CFG
    bool load_config_NTP();
    bool save_configNTP();
    void defaultConfigNTP();
    // WEB
    
    void send_NTP_info_html(AsyncWebServerRequest *request) ;
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void html2ntp_configuration(AsyncWebServerRequest *request);
    void sendTimeData();
    
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
    
    protected: 
    bool updateTimeFromNTP  = false;
    int _ntpServerCount         = 0;
    String _ntpServerNow = "";
};

extern CLASS_CORE_NTP core_ntp;





#endif // _MODULENTP_h