// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h

#if defined(ARDUINO) && ARDUINO >= 100
    #include "Arduino.h"
#else
    #include "WProgram.h"
#endif

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <TimeLib.h>
#include "NtpClientLib.h"
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

#include <Ticker.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#include "main.h"

//#define RELEASE  // Comment to enable debug output

#define DBG_OUTPUT_PORT Serial

#ifndef RELEASE
#define DEBUGLOG(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOG(...)
#endif

#define DBG_HADLEFILEWXIST  0

#if (DBG_HADLEFILEWXIST > 0 )
#define DEBUGLOGFH(...) DBG_OUTPUT_PORT.printf(__VA_ARGS__)
#else
#define DEBUGLOGFH(...)
#endif


#define CONNECTION_LED -1// Connection LED pin (Built in). -1 to disable
#define AP_ENABLE_BUTTON -1//5 // Button pin to enable AP during startup for configuration. -1 to disable

#define AP_ENABLE_TIMEOUT 60 // (Seconds, max 255) If the device can not connect to WiFi it will switch to AP mode after this time. -1 to disable

#define JSON_STR_LEN    512
// #define HIDE_CONFIG
#define CONFIG_FILE     "/config_general.json"

#define WIFI_CONFIG_FILE0 "/config_wifi0.json"
#define WIFI_CONFIGS    1

#if (USE_RESERV_WIFI > 0)
#define WIFI_CONFIG_FILE1 "/config_wifi1.json"
#define WIFI_CONFIG_FILE2 "/config_wifi2.json"
#define WIFI_CONFIG_FILE3 "/config_wifi3.json"
#define WIFI_CONFIGS    4
#endif

#define USER_CONFIG_FILE "/userconfig.json"
#define GENERIC_CONFIG_FILE "/genericconfig.json"
#define SECRET_FILE "/secret.json"

#define JSON_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> jsoncallback
#define REST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> restcallback
#define POST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> postcallback


#define AVRSERVERSTR_UPLOADBEGIN "upload begin\n"

typedef struct {
    String deviceName;
    String deviceSerial;
    String ntpServerName0;
    String ntpServerName1;
    String ntpServerName2;
    long updateNTPTimeEvery;
    long timezone;
    bool daylight;
} strGeneralConfig;

typedef struct {
    String ssid;
    String password;
    IPAddress  ip;
    IPAddress  netmask;
    IPAddress  gateway;
    IPAddress  dns;
    bool dhcp;
} strWifiConfig;


typedef struct {
    String APssid = "esp8266_ap"; // ChipID is appended to this name
    String APpassword = "12345678";
    bool APenable = false; // AP disabled by default
} strApConfig;

typedef struct {
    bool auth;
    String wwwUsername;
    String wwwPassword;
} strHTTPAuth;

typedef enum {
    FS_STAT_CONNECTING
    , FS_STAT_CONNECTED
    
    , FS_STAT_APMODE
    , FS_STAT_DISCONNECTED
} enWifiStatus;

typedef enum {
    FS_STAT_SCANING,
    FS_STAT_SCANED,
    WF_STAT_NONEED
} enWifiScan;

class AsyncFSWebServer : public AsyncWebServer {
public:
    AsyncFSWebServer(uint16_t port);
    void begin(FS* fs);
    void handle();
    const char* getHostName();

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
    void clearConfig(bool reset);
    //Clear the user configuration data (not the Wifi config!) and optional reset the device
    void clearUserConfig(bool reset);
    void serialShowInfo();

private:
	JSON_CALLBACK_SIGNATURE;
	REST_CALLBACK_SIGNATURE;
	POST_CALLBACK_SIGNATURE;

    void NTPbeginReserv ();
    void NTPHandler(NTPSyncEvent_t event);

protected:
    strGeneralConfig    _generalConfig; // General configuration
    strWifiConfig       _wifiConfig;    //  WiFi configuration
    strApConfig         _apConfig;      // Static AP config settings
    strHTTPAuth         _httpAuth;

    char                _strWifi0[30];
    char                _strWifi1[30];
    char                _strWifi2[30];
    char                _strWifi3[30];
    int                 _ntpserveer;

    FS* _fs;
    long wifiDisconnectedSince = 0;
    String _browserMD5 = "";
    uint32_t _updateSize = 0;
    bool updateTimeFromNTP = false;

	WiFiEventHandler onStationModeConnectedHandler, onStationModeDisconnectedHandler, onStationModeGotIPHandler ;

    //uint currentWifiStatus;
    enWifiStatus wifiStatus;
    enWifiScan WifiScan;
    uint8_t connectionTimout;

    Ticker _secondTk;
    bool _secondFlag;

    AsyncEventSource _evs = AsyncEventSource("/events");

    void sendTimeData();
    
    // all about avr;
    String _hexfileProg;
    String _hexfileCheck;
    String _hexFileUploadStatus;
    int  handleHexFileUpload( String filename, size_t index, uint8_t *data, size_t len, bool final);
    void handleHexFileUploadStatus(AsyncWebServerRequest *request);
    void avrCheckFile(AsyncWebServerRequest *request);
    void avrGetInfo(AsyncWebServerRequest *request);
    void avrProg(AsyncWebServerRequest *request);
    void avrProgRollback(AsyncWebServerRequest *request);
    void avrProgStatus(AsyncWebServerRequest *request) ;
    void avrFusesRead(AsyncWebServerRequest *request) ;
    void avrWebFusesWrite(AsyncWebServerRequest *request) ;
    
    

    bool load_configGeneral();
    bool save_configGeneral();
    void defaultConfigGeneral();

    bool load_configWifi(int _in);
    bool save_configWifi(int _in);
    void defaultConfigWifi(int _in);
	
    // bool load_generic_config()
    bool loadHTTPAuth();
    bool saveHTTPAuth();
    void configureWifiAP();
    int scanWifi();
    void configureWifi();
    void ConfigureOTA(String password);
    void serverInit();

    void onWiFiConnected(WiFiEventStationModeConnected data);
	void onWiFiDisconnected(WiFiEventStationModeDisconnected data);
	void onWiFiConnectedGotIP(WiFiEventStationModeGotIP data);

    static void s_secondTick(void* arg);

    String getMacAddress();

    bool checkAuth(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    //void handleFileRead_edit_html(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);
    
    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void send_general_configuration_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_values_html(AsyncWebServerRequest *request, int _index);
    void send_connection_state_values_html(AsyncWebServerRequest *request);
    void send_information_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_html(AsyncWebServerRequest *request);
    void get_general_configuration_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_html(AsyncWebServerRequest *request);
    void restart_esp(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void send_wwwauth_configuration_html(AsyncWebServerRequest *request);
    void send_update_firmware_values_html(AsyncWebServerRequest *request);
    void setUpdateMD5(AsyncWebServerRequest *request);
    void updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
	void handle_rest_config(AsyncWebServerRequest *request);
	void post_rest_config(AsyncWebServerRequest *request);


 //   static String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
    static unsigned char h2int(char c);
    static boolean checkRange(String Value);
    uint8_t hex2bin (uint8_t h) ;
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h
