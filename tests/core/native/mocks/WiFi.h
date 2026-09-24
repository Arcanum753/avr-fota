#ifndef _MOCK_WIFI_H
#define _MOCK_WIFI_H

#include <stdint.h>
#include <vector>
#include <string>
#include "Arduino.h"
#include "IPAddress.h"

typedef enum {
    WIFI_OFF = 0,
    WIFI_STA = 1,
    WIFI_AP = 2,
    WIFI_AP_STA = 3
} WiFiMode_t;

typedef enum {
    WL_NO_SHIELD = 255,
    WL_IDLE_STATUS = 0,
    WL_NO_SSID_AVAIL = 1,
    WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3,
    WL_CONNECT_FAILED = 4,
    WL_CONNECTION_LOST = 5,
    WL_DISCONNECTED = 6
} wl_status_t;

#ifndef WIFI_SCAN_RUNNING
#define WIFI_SCAN_RUNNING (-1)
#endif
#ifndef WIFI_SCAN_FAILED
#define WIFI_SCAN_FAILED  (-2)
#endif

#ifndef WIFI_DISCONNECT_REASON_AUTH_FAIL
#define WIFI_DISCONNECT_REASON_AUTH_FAIL 2
#endif

typedef int WiFiEventHandler;
typedef int WiFiEventId_t;

struct WiFiEventStationModeConnected { String ssid; uint8_t bssid[6]; uint8_t channel; };
struct WiFiEventStationModeDisconnected { String ssid; uint8_t bssid[6]; uint8_t reason; };
struct WiFiEventStationModeGotIP { IPAddress ip; IPAddress mask; IPAddress gw; };

struct MockWifiAp {
    String ssid;
    int    rssi = -50;
    String bssid = "00:00:00:00:00:00";
    int    channel = 1;
    int    encryption = 0;
};

class MockWiFi {
public:
    // --- Управляемый сценарий тестов ---
    void reset();
    void setStatus(int st) { _status = st; }
    void setScanResult(int result) { _scanResult = result; }
    void addNetwork(const String& ssid, int rssi = -50, int enc = 0);
    int  restartCount() const { return _restartCount; }
    void noteRestart() { _restartCount++; }
    void setLocalIP(const IPAddress& ip) { _localIP = ip; }
    void setSoftAPIP(const IPAddress& ip) { _softAPIP = ip; }
    int  softAPdisconnectCount() const { return _softAPDisconnectCount; }
    int  modeSetCount() const { return _modeSetCount; }
    WiFiMode_t lastMode() const { return _mode; }

    // --- Arduino WiFi API ---
    int status() { return _status; }
    bool isConnected() { return _status == WL_CONNECTED; }
    void mode(WiFiMode_t m) { _mode = m; _modeSetCount++; }
    WiFiMode_t getMode() { return _mode; }
    void begin(const char* ssid = "", const char* pass = "") { (void)ssid; (void)pass; }
    void disconnect() { _status = WL_DISCONNECTED; }
    bool softAP(const char* ssid) { _apSsid = ssid ? ssid : ""; return true; }
    bool softAP(const char* ssid, const char* pass) { (void)pass; return softAP(ssid); }
    bool softAP(const String& ssid) { return softAP(ssid.c_str()); }
    bool softAP(const String& ssid, const String& pass) { (void)pass; return softAP(ssid.c_str()); }
    bool softAPdisconnect(bool wifioff = false) { (void)wifioff; _softAPDisconnectCount++; return true; }
    IPAddress softAPIP() { return _softAPIP; }
    String softAPSSID() { return _apSsid; }
    IPAddress localIP() { return _localIP; }
    IPAddress gatewayIP() { return IPAddress(); }
    IPAddress dnsIP() { return IPAddress(); }
    int RSSI() { return -50; }
    int RSSI(int i) { return (i >= 0 && (size_t)i < _nets.size()) ? _nets[i].rssi : 0; }
    String SSID(int i) { return (i >= 0 && (size_t)i < _nets.size()) ? _nets[i].ssid : String(""); }
    String BSSIDstr(int i) { return (i >= 0 && (size_t)i < _nets.size()) ? _nets[i].bssid : String(""); }
    int channel(int i) { return (i >= 0 && (size_t)i < _nets.size()) ? _nets[i].channel : 0; }
    int encryptionType(int i) { return (i >= 0 && (size_t)i < _nets.size()) ? _nets[i].encryption : 0; }
    bool isHidden(int) { return false; }
    int scanNetworks(bool async = false) { return async ? _scanResult : (int)_nets.size(); }
    int scanComplete() { return _scanResult; }
    void scanDelete() {}
    void macAddress(uint8_t* mac) { for (int i = 0; i < 6; i++) mac[i] = (uint8_t)i; }

private:
    int      _status = WL_DISCONNECTED;
    WiFiMode_t _mode = WIFI_OFF;
    int      _scanResult = 0;
    int      _restartCount = 0;
    int      _softAPDisconnectCount = 0;
    int      _modeSetCount = 0;
    String   _apSsid;
    IPAddress _localIP;
    IPAddress _softAPIP;
    std::vector<MockWifiAp> _nets;
};

extern MockWiFi WiFi;

#endif // _MOCK_WIFI_H
