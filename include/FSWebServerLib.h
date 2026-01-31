// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h



#include "main.h"

#include <WiFiClient.h>
#include <TimeLib.h>
#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Ticker.h>

#define CONNECTION_LED -1// Connection LED pin (Built in). -1 to disable
#define AP_ENABLE_BUTTON -1//5 // Button pin to enable AP during startup for configuration. -1 to disable

#define AP_ENABLE_TIMEOUT 60 // (Seconds, max 255) If the device can not connect to WiFi it will switch to AP mode after this time. -1 to disable

#define JSON_STR_LEN    512
// #define HIDE_CONFIG

#define FILENAME_LENGHT    64



#define CONFIG_FILE_SYS             "/config_sys.json"


#define USER_CONFIG_FILE            "/userconfig.json"
#define GENERIC_CONFIG_FILE         "/genericconfig.json"
#define SECRET_FILE                 "/secret.json"

#define JSON_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> jsoncallback
#define REST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> restcallback
#define POST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> postcallback


#define AVRSERVERSTR_UPLOADBEGIN "upload begin\n"


#define OTA_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_FILENAME_FILESYSTEM         "spiffs.bin"
#define OTA_FIRMWARE                    "FIRMWARE"
#define OTA_FILESYSTEM                  "FILESYSTEM"
#define OTA_UNSUPPORTED                 "UNSUPPORTED"

#define HTML_INDEX  "index.html"

enum UpdateTypeFile {
        UNSUPPORTED = 0
    ,   FIRMWARE
    ,   FILESYSTEM
  };

typedef struct {
    String deviceName;
    String deviceSerial;
    String deviceType;
} strSysConfig;


typedef struct {
    String icao;
} strMetarConfig;



typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
} strHTTPAuth;




const char Page_ConfigRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring Wifi.
)=====";

const char Page_IndexRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_GeneralSys[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/system.html">
Please Wait....Configuring.
)=====";

const char Page_GeneralUdp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/udp.html">
Please Wait....Configuring.
)=====";



const char Page_GeneralPrj[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/project.html">
Please Wait....Configuring.
)=====";

const char Page_AvrRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/avr.html">
Please Wait....Configuring.
)=====";

void flashLED(int pin, int times, int delayTime) ;


class AsyncFSWebServer : public AsyncWebServer {
public:
    AsyncFSWebServer(uint16_t port);
#if ESP32
    void begin(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
#endif
    void handle();
	const String getHostName();
	AsyncFSWebServer& setJSONCallback(JSON_CALLBACK_SIGNATURE);
	AsyncFSWebServer& setRESTCallback(REST_CALLBACK_SIGNATURE);
	AsyncFSWebServer& setPOSTCallback(POST_CALLBACK_SIGNATURE);
	void setUSERVERSION(String Version);

	bool save_user_config(String name, String value);
	bool load_user_config(String name, String &value);
	bool save_user_config(String name, int value);
	bool load_user_config(String name, int &value);
	bool save_user_config(String name, float value);
	bool load_user_config(String name, float &value);
	bool save_user_config(String name, long value);
	bool load_user_config(String name, long &value);
	static String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)

    //Clear the configuration data (not the user config!) and optional reset the device
    //Clear the user configuration data (not the Wifi config!) and optional reset the device
    void clearUserConfig(bool reset);
    void serialShowInfo();
    void showDBG();

    strSysConfig    _sysConfig; // SYS configuration
    

    strMetarConfig    _metarConfig; // METAR configuration

    String getMetar();
    String FilesListGet() ;

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
   
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";
    
    
    public:
    AsyncEventSource _evs = AsyncEventSource("/events");
  
    
private:

    // gpio
    void  gpioGetArgs(AsyncWebServerRequest *request);

public:
    bool save_jsonDoc(const JsonDocument& jsonDoc, const String& file);
    bool load_jsonDoc(const String& file, JsonDocument& jsonDoc);
    
    //sys
    bool load_config_Sys();
    bool save_configSys();
    void defaultConfigSys();

private:

    // bool load_generic_config()
    bool loadHTTPAuth();
    bool saveHTTPAuth();
    void ConfigureOTA(String password);
    void serverInit();

public:
    bool checkAuth(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    //void handleFileRead_edit_html(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);

    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
private:
    void send_system_version_values_html(AsyncWebServerRequest *request);
    void send_device_values_html(AsyncWebServerRequest *request);
    void send_project_configuration_values_html(AsyncWebServerRequest *request);

    void send_information_values_html(AsyncWebServerRequest *request);
    void get_system_configuration_html(AsyncWebServerRequest *request);
    void get_project_configuration_html(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void set_wwwauth_configuration(AsyncWebServerRequest *request);
    void send_update_firmware_values_html(AsyncWebServerRequest *request);
    void setUpdateMD5(AsyncWebServerRequest *request);
    void uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void updateFileExecute (AsyncWebServerRequest *request) ;
	void handle_rest_config(AsyncWebServerRequest *request);
	void post_rest_config(AsyncWebServerRequest *request);
public:    
    void restart_esp();
    void clearConfig(bool reset);
private:

    uint32_t maxSketchSpace   ;
    uint32_t freeSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint16_t percentLoadedPrev ;

 //   static String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
    static unsigned char h2int(char c);
    public:
    static boolean checkRange(String Value);
    private:
    
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h
