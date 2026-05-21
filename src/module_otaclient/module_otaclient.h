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

#define OTACLIENT_TIME_DFLT       5
#define OTACLIENT_POWERON         true
#define OTACLIENT_SERVER_ADDR     "192.168.88.171"
#define OTACLIENT_SERVER_PORT     8080
#define OTACLIENT_MANIFEST_PATH   "/manifest.json"

// Max entries in manifest
#define OTACLIENT_MAX_MANIFEST_ENTRIES 20

// Max retries for update download
#define OTACLIENT_MAX_RETRIES     3

// Chunk size for streaming download
#define OTACLIENT_CHUNK_SIZE      256

// Test result codes
#define OTACLIENT_TEST_IDLE       0
#define OTACLIENT_TEST_PENDING    1
#define OTACLIENT_TEST_SERVER_UNAVAIL 2
#define OTACLIENT_TEST_NO_UPDATES     3
#define OTACLIENT_TEST_UPDATE_AVAIL   4
#define OTACLIENT_TEST_UPDATING       5

typedef struct {
    uint16_t timeOut;
    bool  powerOn;
    String serverAddress;
    uint16_t serverPort;
    String manifestPath;
} strOtaClientConfig;

// Structure for a single manifest entry
struct ManifestEntry {
    String name;
    String type;     // "firmware" or "filesystem"
    size_t size;
    String md5;
};

void otaclientTimer(void);
void otaclientLoopTask(void);

class MODULE_CLASS_OTACLIENT {
public:
    MODULE_CLASS_OTACLIENT();
    
    void webInit(void);
    void begin();
    void test(AsyncWebServerRequest *request);
    void loop();
    
    void send_configuration_values_html(AsyncWebServerRequest *request);
    void get_configuration_html(AsyncWebServerRequest *request);
    
    uint16_t getTimeOut();
    bool powerOnGet();
    String serverAddressGet();
    uint16_t serverPortGet();
    String manifestPathGet();
    String jsonGet();
    uint8_t isStart();
    
    // Called from WiFi connect hook
    void onWiFiConnect();
    
    // Main update check (called from timer)
    void checkForUpdates();
    
    strOtaClientConfig _config; // OTA Client configuration
   
private:
    bool load_config();
    bool save_config();
    void defaultConfig();
    
    // Manifest download and parsing
    bool fetchManifest(ManifestEntry* entries, int& count);
    
    // Download and flash a single file
    bool downloadAndUpdate(const String& url, size_t size, const String& md5, int fileType);
    
    // Perform actual update from stream
    bool performUpdateFromStream(WiFiClient& stream, size_t size, const String& expectedMd5, int fileType);
    
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
    
    uint8_t _isStarted;
    bool _updateInProgress;
    uint8_t _updateRetries;
    uint16_t _lastReportedPercent;  // For progress reporting (5% step)
    bool _fsEnded;                  // Track if FS was ended for update
    
    // Static buffer for manifest entries (avoid stack allocation)
    static ManifestEntry _manifestEntries[OTACLIENT_MAX_MANIFEST_ENTRIES];
    
    // Test/status fields
    uint8_t _testStatusCode;       // OTACLIENT_TEST_*
    String _testStatusMessage;     // human-readable message
    String _testUpdateName;        // filename of available update
    String _testUpdateUrl;         // full URL of available update
    size_t _testUpdateSize;        // size of available update
    String _testUpdateMd5;         // MD5 of available update
    int _testUpdateFileType;       // FILE_TYPE_FIRMWARE or FILE_TYPE_FILESYSTEM
    int8_t _testCompareResult;     // combined fsVersionCompare: 1=newer, 0=same, -1=older, -2=missing
    int8_t _testFwCompareResult;   // firmware-specific compare result
    int8_t _testFsCompareResult;   // filesystem-specific compare result
    
    friend void otaclientTimer();
};

extern MODULE_CLASS_OTACLIENT otaClient;

#endif // _MODOTACLIENT_h
