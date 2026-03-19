// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h



#include "main.h"

#include <WiFiClient.h>
#include <TimeLib.h>

#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <SPIFFS.h>
#endif

#if defined(ESP8266)
#include <FS.h>
#endif

#include <Ticker.h>


#define JSON_STR_LEN    512
// #define HIDE_CONFIG

#define FILENAME_LENGHT    64

#define CONFIG_FILE_SYS             "/config_sys.json"
#define SECRET_FILE                 "/secret.json"


#define JSON_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> jsoncallback
#define REST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> restcallback
#define POST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> postcallback


#define AVRSERVERSTR_UPLOADBEGIN "upload begin\n"

#define HTML_INDEX  "index.html"


typedef struct {
    String deviceName;
    String deviceSerial;
    int16_t wifiScanTime;
    uint16_t wifiAPLifeTime;
} strSysConfig;


typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
} strHTTPAuth;


const char Page_IndexRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_GeneralSys[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/system.html">
Please Wait....Configuring.
)=====";

const char Page_GeneralPrj[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/project.html">
Please Wait....Configuring.
)=====";



String getContentType(String filename, AsyncWebServerRequest *request);

class AsyncFSWebServer : public AsyncWebServer {
public:
    AsyncFSWebServer(uint16_t port);
#if ESP32
    void begin(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
#endif
	const String getHostName();
    void serialShowAbout();
    String getResetReason() ;
    strSysConfig    _sysConfig; // SYS configuration

private:
	JSON_CALLBACK_SIGNATURE;
	REST_CALLBACK_SIGNATURE;
	POST_CALLBACK_SIGNATURE;
public:
	strHTTPAuth         _httpAuth;

protected:
#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif
    public:
    AsyncEventSource _evs = AsyncEventSource("/events");
   private:
    // gpio
    void  gpioGetArgs(AsyncWebServerRequest *request);
public:
    //sys
    bool load_config_Sys();
    bool save_configSys();
    void defaultConfigSys();
private:
    // bool load_generic_config()
    bool loadHTTPAuth();
    bool saveHTTPAuth();
    void serverInit();

public:
    bool checkAuth(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    uint16_t configSys_ApTimeGet()   ;
    int16_t  configSys_ScanTimeGet() ;

    
private:
    void html_version_info(AsyncWebServerRequest *request);
    void html_system_Load(AsyncWebServerRequest *request);
    void send_project_configuration_values_html(AsyncWebServerRequest *request);

    void html_send_chipinfo(AsyncWebServerRequest *request);
    void html_system_Save(AsyncWebServerRequest *request);
    void get_project_configuration_html(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void set_wwwauth_configuration(AsyncWebServerRequest *request);

	void handle_rest_config(AsyncWebServerRequest *request);
	void post_rest_config(AsyncWebServerRequest *request);
public:    
    void restart_esp();
    
private:


    


    public:
    
    private:
    
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h
