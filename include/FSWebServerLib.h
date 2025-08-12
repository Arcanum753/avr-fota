// FSWebServerLib.h

#ifndef _FSWEBSERVERLIB_h
#define _FSWEBSERVERLIB_h

#if defined(ARDUINO) && ARDUINO >= 100
    #include "Arduino.h"
#else
    #include "WProgram.h"
#endif

#include "main.h"

#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESPAsyncWebServer.h>
#if defined(ESP32)
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif
#include <Ticker.h>
#include <ArduinoOTA.h>

#include <ArduinoJson.h>



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

#define UDP_BROADCAST_PORT_DFLT 40000
#define UDP_BROADCAST_TIME_DFLT 5
#define UDP_BROADCAST_KEYWORD_DFLT "Ave_Omnissiah"

#define CONNECTION_LED -1// Connection LED pin (Built in). -1 to disable
#define AP_ENABLE_BUTTON -1//5 // Button pin to enable AP during startup for configuration. -1 to disable

#define AP_ENABLE_TIMEOUT 60 // (Seconds, max 255) If the device can not connect to WiFi it will switch to AP mode after this time. -1 to disable

#define JSON_STR_LEN    512
// #define HIDE_CONFIG


#define WIFI_CONFIG_FILE_NAME "config_wifi"
#define WIFI_CONFIGS    4

//WIFI_CONFIG_FILEx is deprecated option

#define WIFI_CONFIG_FILE0 "/config_wifi0.json"
#if (USE_RESERV_WIFI > 0)
#define WIFI_CONFIG_FILE1           "/config_wifi1.json"
#define WIFI_CONFIG_FILE2           "/config_wifi2.json"
#define WIFI_CONFIG_FILE3           "/config_wifi3.json"
#endif

#define CONFIG_FILE_METAR             "/config_metar.json"

#define CONFIG_FILE_SYS             "/config_sys.json"
#define CONFIG_FILE_NTP             "/config_ntp.json"
#define CONFIG_FILE_UDP             "/config_udp.json"
//#define CONFIG_FILE_PRJ                 "/config_prj.json"



#define USER_CONFIG_FILE            "/userconfig.json"
#define GENERIC_CONFIG_FILE         "/genericconfig.json"
#define SECRET_FILE                 "/secret.json"

#define JSON_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> jsoncallback
#define REST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> restcallback
#define POST_CALLBACK_SIGNATURE std::function<void(AsyncWebServerRequest *request)> postcallback


#define AVRSERVERSTR_UPLOADBEGIN "upload begin\n"

#define NTPSERVER_DFLT0 "pool.ntp.org";
#define NTPSERVER_DFLT1 "0.ru.pool.ntp.org";
#define NTPSERVER_DFLT2 "0.gentoo.pool.ntp.org";

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
    String ntpServerName0;
    String ntpServerName1;
    String ntpServerName2;
    long updateNTPTimeEvery;
    long timezone;
    bool daylight;
} strNtpConfig;

typedef struct {
    uint16_t udpPortTx;
    uint16_t udpPortRx;
    uint16_t udpTimeOut;
    String keyword;
} strUdpConfig;

typedef struct {
    String icao;
} strMetarConfig;

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
    , FS_STAT_RESET
} enWifiStatus;

typedef enum {
    WF_STAT_SCANING,
    WF_STAT_SCANED,
    WF_SCAN_NO_NEED
} enWifiScan;


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
    void clearConfig(bool reset);
    //Clear the user configuration data (not the Wifi config!) and optional reset the device
    void clearUserConfig(bool reset);
    void serialShowInfo();
    void showDBG();
    String      udpJsonBroadcast();
    uint16_t    getUpdPortTx() ;
    uint16_t    getUpdPortRx() ;
    uint16_t    getudpTimeOut() ;
    String      getudpKeyword();
    strSysConfig    _sysConfig; // SYS configuration
    strNtpConfig    _ntpConfig; // NTP configuration
    strUdpConfig    _udpConfig; // UDP configuration
    strMetarConfig    _metarConfig; // METAR configuration

private:
	JSON_CALLBACK_SIGNATURE;
	REST_CALLBACK_SIGNATURE;
	POST_CALLBACK_SIGNATURE;

    void ntpBegin ();
    void ntpBeginReserv ();
    void ntpHandler(NTPSyncEvent_t event);

protected:



    strWifiConfig       _wifiConfig;    //  WiFi configuration
    strApConfig         _apConfig;      // Static AP config settings
    strHTTPAuth         _httpAuth;

    char                _strWifi0[40];
    char                _strWifi1[40];
    char                _strWifi2[40];
    char                _strWifi3[40];
    int                 _ntpserveer;

#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif
    long wifiDisconnectedSince = 0;
    String _browserFileMD5 = "";
    uint32_t _updateFileSize = 0;
    String _updateFileName = "";
    bool updateTimeFromNTP = false;


    #if defined(ESP32)
    // WiFiEventId_t eventID;
    WiFiEventId_t onStationModeConnectedHandler
        , onStationModeDisconnectedHandler
        , onStationModeGotIPHandler;
    #elif defined(ESP8266)
	WiFiEventHandler onStationModeConnectedHandler, onStationModeDisconnectedHandler, onStationModeGotIPHandler ;
    #endif



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

    // gpio
    void  gpioGetArgs(AsyncWebServerRequest *request);

    //udp
    void udpTest(AsyncWebServerRequest *request) ;

    bool save_jsonDoc(const JsonDocument& jsonDoc, const String& file);
    bool load_jsonDoc(const String& file, JsonDocument& jsonDoc);
    //metar
    bool load_config_metar();
    bool save_config_metar();
    void default_config_metar();

    //sys
    bool load_config_Sys();
    bool save_configSys();
    void defaultConfigSys();

    //ntp
    bool load_config_NTP();
    bool save_configNTP();
    void defaultConfigNTP();

    //udp
    bool load_config_UDP();
    bool save_configUDP();
    void defaultConfigUDP();

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

    #if ESP32
    void onWiFiConnected        ();
	void onWiFiDisconnected     ();
	void onWiFiConnectedGotIP   ();
    #elif defined(ESP8266)
    void onWiFiConnected        (WiFiEventStationModeConnected      data);
	void onWiFiDisconnected     (WiFiEventStationModeDisconnected   data);
	void onWiFiConnectedGotIP   (WiFiEventStationModeGotIP          data);
    #endif


    static void s_secondTick(void* arg);

    String getMacAddress();

    bool checkAuth(AsyncWebServerRequest *request);
    void handleFileList(AsyncWebServerRequest *request);
    //void handleFileRead_edit_html(AsyncWebServerRequest *request);
    bool handleFileRead(String path, AsyncWebServerRequest *request);
    void handleFileCreate(AsyncWebServerRequest *request);
    void handleFileDelete(AsyncWebServerRequest *request);

    void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void send_system_configuration_values_html(AsyncWebServerRequest *request);
    void send_device_values_html(AsyncWebServerRequest *request);

    void send_udp_configuration_values_html(AsyncWebServerRequest *request);

    void send_project_configuration_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_values_html(AsyncWebServerRequest *request, int _index);
    void send_connection_state_values_html(AsyncWebServerRequest *request);
    void send_information_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_values_html(AsyncWebServerRequest *request);
    void send_NTP_configuration_html(AsyncWebServerRequest *request);
    void send_network_configuration_html(AsyncWebServerRequest *request);

    void get_system_configuration_html(AsyncWebServerRequest *request);
    void get_udp_configuration_html(AsyncWebServerRequest *request);
    void get_project_configuration_html(AsyncWebServerRequest *request);

    void restart_esp();
    void send_wwwauth_configuration_values_html(AsyncWebServerRequest *request);
    void set_wwwauth_configuration(AsyncWebServerRequest *request);
    void send_update_firmware_values_html(AsyncWebServerRequest *request);
    void setUpdateMD5(AsyncWebServerRequest *request);
    void uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void updateFileExecute (AsyncWebServerRequest *request) ;
	void handle_rest_config(AsyncWebServerRequest *request);
	void post_rest_config(AsyncWebServerRequest *request);

    uint32_t maxSketchSpace   ;
    uint32_t freeSketchSpace   ;
    void prepareSizesForUpdate();
    UpdateTypeFile  typeOTAfile;
    uint16_t percentLoadedPrev ;

 //   static String urldecode(String input); // (based on https://code.google.com/p/avr-netino/)
    static unsigned char h2int(char c);
    static boolean checkRange(String Value);
    uint8_t hex2bin (uint8_t h) ;
};

extern AsyncFSWebServer ESPHTTPServer;

#endif // _FSWEBSERVERLIB_h
