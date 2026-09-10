
#ifndef _MODWIFI_h
#define _MODWIFI_h


#include "main.h"

#include "mod_context.h"

#ifdef DEBUGLOG_WIFI
#define DEBUGLOGWIFI(...) DBG_MOD("[C_WIFI] ", __VA_ARGS__)
#else
#define DEBUGLOGWIFI(...)
#endif


const char Page_ConfigRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring Wifi.
)=====";


#define WIFI_CONFIG_FILE_NAME       "config_wifi"


// #define WIFI_CONFIGS    4 // TODO 

#define AP_ENABLE_TIMEOUT 60 // (Seconds, max 255) If the device can not connect to WiFi it will switch to AP mode after this time. -1 to disable

// Максимальное количество неудачных попыток подключения к одному SSID
// после которого SSID временно пропускается
#define MAX_WIFI_FAIL_COUNT 3



#define WIFI_CONFIG_FILE0           "/config_wifi0.json"
#define WIFI_CONFIG_FILE1           "/config_wifi1.json"
#define WIFI_CONFIG_FILE2           "/config_wifi2.json"
#define WIFI_CONFIG_FILE3           "/config_wifi3.json"
#define WIFI_CONFIG_SYS             "/config_wifi.json"





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


typedef enum {
    FS_STAT_CONNECTING
    , FS_STAT_CONNECTED
    , FS_STAT_APMODE
    , FS_STAT_DISCONNECTED
    , FS_STAT_RESET
    , FS_STAT_WRONGPASSWORDS
} enWifiStatus;

typedef enum {
    WF_STAT_SCANING,
    WF_STAT_SCANED,
    WF_SCAN_NO_NEED
} enWifiScan;



class  CLASS_CORE_WIFI    {
    public:
    #if ESP32
    fs::LittleFSFS*               _fs;
    #elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
    #endif
    #if defined(ESP32)  
    void begin(fs::LittleFSFS* fs);
    #elif defined(ESP8266)
    void begin(FS* fs) ;                        // esp8266/esp32 flash file system
    #endif

    void begin(ModContext& ctx);

    #if defined(ESP32)
    // WiFiEventId_t eventID;
    WiFiEventId_t onStationModeConnectedHandler
    , onStationModeDisconnectedHandler
    , onStationModeGotIPHandler;
    #elif defined(ESP8266)
    WiFiEventHandler onStationModeConnectedHandler, onStationModeDisconnectedHandler, onStationModeGotIPHandler ;
    #endif
    
    strWifiConfig       _wifiConfig;    //  WiFi configuration
    strApConfig         _apConfig;      // Static AP config settings
    char                _strWifi0[40];
    char                _strWifi1[40];
    char                _strWifi2[40];
    char                _strWifi3[40];
    long                wifiDisconnectedSince = 0;
    enWifiStatus        wifiStatus = FS_STAT_CONNECTING;
    enWifiScan WifiScan = WF_STAT_SCANING;
    uint16_t connectionTimout;
    bool _secondFlag;
    uint8_t             _wifiFailCount[4] = {0, 0, 0, 0}; // Счётчики неудачных попыток для каждого SSID
    uint16_t            _wifiScanTime;  // бюджет попытки подключения в STA, минуты (scanTime = _wifiScanTime*60)
    uint16_t            _wifiAPLifeTime; // «жизнь» AP: сколько минут AP ждёт/обслуживает клиента без активности, затем скан; 0 = AP не включать
    volatile uint16_t   _apUptime = 0;      // секунды AP без активности (таймер «жизни» AP)
    volatile bool       _apClientActivity = false;
    void notifyApClientActivity() { if (wifiStatus == FS_STAT_APMODE) { _apClientActivity = true; } }
    void notifyRestart() { wifiStatus = FS_STAT_RESET; }
    
    static void s_secondTick();
    void web_Init();
    String getMacAddress();
    int scanWifi();
    bool load_configWifi(int _in);
    bool save_configWifi(int _in);
    void defaultConfigWifi(int _in);
    bool load_configWifiSys();
    bool save_configWifiSys();
    void defaultConfigWifiSys();
    void startDNSCaptive();
    void configureWifiAP();
    void configureWifi();
    
    void wifiSsidSetPSWDwrong(String _str) ;
    void resetWifiFailCounters(); // Сброс всех счётчиков неудачных попыток
    void send_network_configuration_values_html(AsyncWebServerRequest *request, int _index);
    void send_info_values_html(AsyncWebServerRequest *request);
    void send_network_configuration_html(AsyncWebServerRequest *request);
    void send_scanwifi(AsyncWebServerRequest *request) ;
    void send_scanwifi_trigger(AsyncWebServerRequest *request);
    String buildNetworksJson() ;
    #if ESP32
    void onWiFiConnected        ();
	void onWiFiDisconnected     (WiFiEventInfo_t info);
	void onWiFiConnectedGotIP   ();
    #elif defined(ESP8266)
    void onWiFiConnected        (WiFiEventStationModeConnected      data);
	void onWiFiDisconnected     (WiFiEventStationModeDisconnected   data);
	void onWiFiConnectedGotIP   (WiFiEventStationModeGotIP          data);
    #endif
private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    void send_slot_json(AsyncWebServerRequest *request, int slot);
    void save_slot_json(AsyncWebServerRequest *request, int slot);
    void handle_slot_post(AsyncWebServerRequest *request, int slot);
    void handle_slot_upload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
    void send_wifi_sysconf_json(AsyncWebServerRequest *request);
    void handle_wifi_sysconf_post(AsyncWebServerRequest *request);
    // --- Ежесекундный автомат состояния Wi-Fi (исполняется в контексте loop) ---
    void secondTick();          // шаг автомата (раз в секунду)
    void apTick();              // шаг в состоянии AP (ожидание/обслуживание клиента)
    void staTick();             // шаг в состоянии STA (скан/попытка подключения)
    void enterApWait();         // вход в AP-ожидание (или STA-реконнект при _wifiAPLifeTime==0)
    void leaveApToScan();       // выход из AP и запуск скана сети
    void rescanSoon();          // разрешить пересканирование в ближайший тик
    bool anySlotFree();         // есть ли слот с _wifiFailCount < MAX_WIFI_FAIL_COUNT
protected: 
    int32_t scanTime = 1;       // бюджет попытки подключения в STA (сек), 0/отрицательное -> внутренний дефолт
    uint32_t _stateSeconds = 0;     // счётчик секунд автомата (независимо от состояния)
    bool     _enterApPending = false; // запрос входа в AP из WiFi-события (обрабатывается в loop)
    bool     _ignoreDisconnect = false; // подавление событий от собственных WiFi.disconnect()
    uint8_t  _suppressDisc = 0;  // кол-во секунд, в течение которых события отключения игнорируются
    uint32_t _nextStaScanAt = 0; // тик, с которого можно снова запускать скан (пауза при отсутствии сети)
};


extern CLASS_CORE_WIFI core_wifi; 

// ============================================================
// Кассета светодиодной индикации статуса Wi-Fi
// ============================================================
void ledMacrosWifiScan();
void ledMacrosWifiDisconnect();
void ledMacrosWifiAP();
void ledMacrosWifiConnecting();
void ledMacrosWifiError();
void ledMacrosWifiConnected();



#endif // _MODWIFI_h
