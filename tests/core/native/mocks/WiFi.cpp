#include "WiFi.h"

MockWiFi WiFi;

void MockWiFi::reset() {
    _status = WL_DISCONNECTED;
    _mode = WIFI_OFF;
    _scanResult = 0;
    _restartCount = 0;
    _softAPDisconnectCount = 0;
    _modeSetCount = 0;
    _apSsid = "";
    _localIP = IPAddress();
    _softAPIP = IPAddress();
    _nets.clear();
}

void MockWiFi::addNetwork(const String& ssid, int rssi, int enc) {
    MockWifiAp ap;
    ap.ssid = ssid;
    ap.rssi = rssi;
    ap.encryption = enc;
    _nets.push_back(ap);
}
