#ifndef _CORE_WIFI_TYPES_h
#define _CORE_WIFI_TYPES_h

// ============================================================
// core_wifi_types.h — типы, структуры и define'ы конфигурации/логики
// ядра core_wifi. Реализация: core_wifi.cpp (шаблон) и
// core_wifi_engine.cpp (исполнительная логика).
// ============================================================

#include <Arduino.h>
#include <IPAddress.h>

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

// Пауза между повторными сканами, когда сеть не находится и AP выключена (сек)
#define WIFI_RESCAN_PAUSE_SEC       20
// Бюджет попытки подключения, если scanTime <= 0 (сек)
#define WIFI_CONNECT_BUDGET_SEC     20
// Защита от «зависшего» скана (скан не завершается) — принудительный рестарт (сек)
#define WIFI_SCAN_STUCK_SEC         60

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

#endif // _CORE_WIFI_TYPES_h
