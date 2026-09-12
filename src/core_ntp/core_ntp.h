#ifndef _MODULENTP_h
#define _MODULENTP_h

#include "main.h"
#include <WiFiClient.h>
#include "common/TimeLib.h"

#include "mod_context.h"


#include "core_ntp/NtpClientLib.h"
#include "core_ntp_types.h"



#ifdef DEBUG_NTP
#define DEBUGNTP(...) DBG_MOD("[C_NTP] ", __VA_ARGS__)
#else
#define DEBUGNTP(...)
#endif


const char Page_GeneralNtp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/ntp.html">
Please Wait....Configuring.
)=====";

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