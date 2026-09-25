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
// Бэкофф при WIFI_SCAN_FAILED: не пытаться инициализировать драйвер каждую секунду (сек)
#define WIFI_INIT_FAIL_PAUSE_SEC    20
// Число подряд идущих ошибок init/scan до контролируемого ESP.restart()
#define WIFI_INIT_FAIL_MAX          5
// Сколько секунд STA сканирует сеть после выхода из AP, прежде чем вернуться
// в AP (если сеть так и не найдена). Даёт вернувшемуся роутеру окно для реконнекта.
#define WIFI_AP_RETRY_PHASE_SEC     60

// Режим работы Wi-Fi-модуля (ресурс wifi.mode)
#define WIFI_MODE_AUTO              0
#define WIFI_MODE_MACRO             1
// Целевое состояние Wi-Fi под управлением макросов (ресурс wifi.target)
#define WIFI_TARGET_AUTO            0
#define WIFI_TARGET_AP              1
#define WIFI_TARGET_STA             2
// Диапазоны runtime/конфигурируемых полей
#define WIFI_SCAN_RETRIES_MIN       5
#define WIFI_SCAN_RETRIES_MAX       50
#define WIFI_AP_HOLD_MIN_MAX        60   // 0..60, 0 = бесконечно
#define WIFI_AP_HOLD_DEFAULT_SEC    300  // 5 мин — ожидание при _apHoldMin == 0

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
