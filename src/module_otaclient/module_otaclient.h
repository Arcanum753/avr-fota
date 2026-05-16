#ifndef _MODOTACLIENT_h
#define _MODOTACLIENT_h

#include "main.h"
#include "FSWebServerLib.h"

#ifdef DEBUG_OTACLIENT
#define DEBUGOTACLIENT(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGOTACLIENT(...)
#endif

const char Page_GeneralOtaClient[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/otaclient.html">
Please Wait....Configuring.
)=====";

#define CONFIG_FILE_OTACLIENT     "/config_otaclient.json"
#define HTML_FILE_OTACLIENT       "/otaclient.html"

#define OTACLIENT_TIME_DFLT     5
#define OTACLIENT_POWERON       true
#define OTACLIENT_SERVER_ADDR   "192.168.88.171"

typedef struct {
    uint16_t timeOut;
    bool  powerOn;
    String serverAddress;
} strOtaClientConfig;

void otaclientTimer(void);

class MODULE_CLASS_OTACLIENT {
public:
    MODULE_CLASS_OTACLIENT();
    
    void webInit(void);
    void begin();
    void test(AsyncWebServerRequest *request);
    
    void send_configuration_values_html(AsyncWebServerRequest *request);
    void get_configuration_html(AsyncWebServerRequest *request);
    
    uint16_t getTimeOut();
    bool powerOnGet();
    String serverAddressGet();
    String jsonGet();
    uint8_t isStart();
    
    strOtaClientConfig _config; // OTA Client configuration
   
private:
    bool load_config();
    bool save_config();
    void defaultConfig();
    
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
    uint8_t _isStarted;
    
    friend void otaclientTimer();
};

extern MODULE_CLASS_OTACLIENT otaClient;

#endif // _MODOTACLIENT_h