#ifndef _CORE_WIFI_LED_h
#define _CORE_WIFI_LED_h

// ============================================================
// core_wifi_led.h — кассета светодиодной индикации статуса Wi-Fi.
// Реализация: core_wifi_engine.cpp
// ============================================================

void ledMacrosWifiScan();
void ledMacrosWifiDisconnect();
void ledMacrosWifiAP();
void ledMacrosWifiConnecting();
void ledMacrosWifiError();
void ledMacrosWifiConnected();

#endif // _CORE_WIFI_LED_h
