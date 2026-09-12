#include "main.h"
#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#include <avr/pgmspace.h>
#endif

#include <DNSServer.h>

#include "core_web/FSWebServerLib.h"

#include "core_wifi/core_wifi.h"
#include "core_sys/core_sys.h"
#include "core_ntp/core_ntp.h"
#include "core_led/core_led.h"
#include "core_state/core_state.h"
#include "core_sys/eertos.h"

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#ifdef MODULE_OTACLIENT
#include "module_otaclient/module_otaclient.h"
#endif

// ============================================================
// ГЛОБАЛЬНЫЕ ОБЪЕКТЫ И ПЕРЕМЕННЫЕ
// ============================================================

DNSServer 		dnsServer;

static const char patWifiScan[]     PROGMEM = "*.*.*.*.";
static const char patWifiDisc[]     PROGMEM = "*.........";
static const char patWifiAP[]       PROGMEM = "*.*.*......";
static const char patWifiConn[]     PROGMEM = "*.*..";
static const char patWifiErr[]      PROGMEM = "*.*.*";

// ============================================================
// КОНКРЕТНАЯ ЛОГИКА МОДУЛЯ
// ============================================================

void CLASS_CORE_WIFI::s_secondTick() {
	SetTimerTask(&CLASS_CORE_WIFI::s_secondTick, 1000);
	core_wifi.secondTick();
}

void CLASS_CORE_WIFI::secondTick() {
	_stateSeconds++;

	if (_suppressDisc > 0) { _suppressDisc--; }

	// Периодический сброс счётчиков неудачных попыток раз в 60 секунд —
	// независимо от состояния (раньше привязка к connectionTimout не срабатывала в AP)
	if (_stateSeconds % 60 == 0) {
		bool anyBlocked = false;
		for (int i = 0; i < 4; i++) {
			if (_wifiFailCount[i] >= MAX_WIFI_FAIL_COUNT) { anyBlocked = true; break; }
		}
		if (anyBlocked) {
			DEBUG_WIFI("Periodic reset of wifi fail counters\n");
			resetWifiFailCounters();
		}
	}

	if (wifiStatus == FS_STAT_APMODE) {
		//DNS captive
		dnsServer.processNextRequest();
		apTick();
		return;
	}

	// Отложенный вход в AP из WiFi-события выполняем в контексте loop
	if (_enterApPending) {
		_enterApPending = false;
		enterApWait();
		return;
	}

	if (wifiStatus == FS_STAT_CONNECTED) {
		ledMacrosWifiConnected();
		return;
	}
	if (wifiStatus == FS_STAT_CONNECTING) {
		staTick();
		return;
	}
}

// AP «живёт» максимум _wifiAPLifeTime минут без активности:
// клиент не подключился, либо висит без трафика. Затем — скан сети.
void CLASS_CORE_WIFI::apTick() {
	if (_wifiAPLifeTime == 0) {
		leaveApToScan();
		return;
	}

	// Любая HTTP-активность клиента (notifyApClientActivity из сервера) продлевает AP
	if (_apClientActivity) {
		_apClientActivity = false;
		_apUptime = 0;
		ledMacrosWifiAP();
		return;
	}

	if ((uint32_t)++_apUptime >= (uint32_t)_wifiAPLifeTime * 60) {
		DEBUG_WIFI("AP idle %lu sec without activity. Leaving AP to scan.\r\n", (unsigned long)_wifiAPLifeTime * 60);
		leaveApToScan();
		return;
	}
	ledMacrosWifiAP();
}

void CLASS_CORE_WIFI::leaveApToScan() {
	DEBUG_WIFI("AP -> STA scan\r\n");
	dnsServer.stop();
	_suppressDisc = 3;   // события от переключения режимов игнорируем
	_ignoreDisconnect = true;
	WiFi.softAPdisconnect(true);
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	_nextStaScanAt = _stateSeconds;
	WiFi.scanNetworks(true);
	ledMacrosWifiScan();
}

// Вход в «AP в ожидании клиента». При _wifiAPLifeTime == 0 AP не включается —
// остаёмся в STA и периодически пересканируем сеть.
void CLASS_CORE_WIFI::enterApWait() {
	if (wifiStatus == FS_STAT_APMODE) {
		_apUptime = 0;
		return;
	}
	if (_wifiAPLifeTime == 0) {
		wifiStatus = FS_STAT_CONNECTING;
		WifiScan = WF_STAT_SCANING;
		connectionTimout = 0;
		_apUptime = 0;
		_apClientActivity = false;
		_nextStaScanAt = _stateSeconds;
		return;
	}
	configureWifiAP();
}

void CLASS_CORE_WIFI::rescanSoon() {
	_nextStaScanAt = _stateSeconds; // ближайший тик начнёт скан
}

bool CLASS_CORE_WIFI::anySlotFree() {
	for (int i = 0; i < 4; i++) {
		if (_wifiFailCount[i] < MAX_WIFI_FAIL_COUNT) { return true; }
	}
	return false;
}

// STA: скан сети из конфигов / попытка подключения
void CLASS_CORE_WIFI::staTick() {
	// Идёт попытка подключения (WiFi.begin вызван) — контролируем бюджет
	if (WifiScan == WF_SCAN_NO_NEED) {
		uint32_t budget = (scanTime > 0) ? (uint32_t)scanTime : WIFI_CONNECT_BUDGET_SEC;
		if ((uint32_t)++connectionTimout >= budget) {
			DEBUG_WIFI("Connect budget expired. Back to AP wait.\r\n");
			enterApWait();
		}
		return;
	}

	// Пауза между повторными сканами (STA-режим без AP, сеть не находится)
	if (_stateSeconds < _nextStaScanAt) {
		ledMacrosWifiScan();
		return;
	}

	int st = WiFi.scanComplete();
	if (st == WIFI_SCAN_RUNNING) {
		// Защита от «зависшего» скана: если скан не завершается дольше порога —
		// перезапускаем через AP-ожидание или повторный скан
		if ((uint32_t)++connectionTimout >= WIFI_SCAN_STUCK_SEC) {
			DEBUG_WIFI("Scan stuck %lu sec. Restarting.\r\n", (unsigned long)WIFI_SCAN_STUCK_SEC);
			connectionTimout = 0;
			WiFi.scanDelete();
			if (_wifiAPLifeTime > 0) {
				enterApWait();
			} else {
				WiFi.mode(WIFI_STA);
				_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
				ledMacrosWifiScan();
			}
		} else {
			ledMacrosWifiScan();
		}
		return;
	}
	if (st == WIFI_SCAN_FAILED) {
		connectionTimout = 0;
		WiFi.scanNetworks(true);	// перезапуск скана
		ledMacrosWifiScan();
		return;
	}
	if (st < 0) {
		ledMacrosWifiScan();
		return;
	}

	// Скан завершён
	connectionTimout = 0;
	int slot = scanWifi();
	WiFi.scanDelete();
	if (slot < 0) {
		// Сети из конфигов нет (или все SSID заблокированы счётчиками неудач)
		if (_wifiAPLifeTime > 0) {
			enterApWait();
		} else {
			_nextStaScanAt = _stateSeconds + WIFI_RESCAN_PAUSE_SEC;
			ledMacrosWifiScan();
		}
		return;
	}

	// Найдена сеть из конфигов — подключаемся
	load_configWifi(slot);
	WifiScan = WF_SCAN_NO_NEED;
	connectionTimout = 0;
	DEBUG_WIFI("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
	WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	ledMacrosWifiConnecting();
}

void CLASS_CORE_WIFI::startDNSCaptive() {
    // Перехватываем все DNS запросы и направляем на IP точки доступа
    dnsServer.start(53, "*", WiFi.softAPIP());
    DEBUG_WIFI("DNS captive portal started on port 53\n");
}

void CLASS_CORE_WIFI::configureWifiAP() {
	DEBUG_WIFI(__PRETTY_FUNCTION__);	DEBUG_WIFI("\r\n");
	core_ntp.ntpOnDisconected();
#if defined(MODULE_UDP)
		module_udp.stop();	// always stop!
#endif
	String APname = core_sys.getHostName();
	_suppressDisc = 3;   // события от собственного отключения STA игнорируем
	_ignoreDisconnect = true;
	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_APMODE;
	if (core_sys.httpAuthEnabled()) {
		WiFi.softAP(APname, core_sys.getHttpPassword());
		DEBUG_WIFI("AP Pass enabled: %s \r\n", core_sys.getHttpPassword().c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUG_WIFI("AP Pass disabled \r\n");
	}
	startDNSCaptive();
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 5, 250);	}
	DEBUG_WIFI("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
	_apUptime = 0;
	_apClientActivity = false;
	ledSetSteady(false);
	ledMacrosWifiAP();	// вход в AP-режим
}

int CLASS_CORE_WIFI::scanWifi() {
	int _scanNum = -1;

	// Выбор SSID из завершённого скана в порядке приоритета (сначала слот 3, потом 2, 1, 0).
	// Пропускаем SSID, у которых превышен лимит неудачных попыток.
	int nets = WiFi.scanComplete();
	if (nets <= 0) { return -1; }

	for (int i = 0; i < nets && _scanNum < 0; ++i) {
		if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[3] < MAX_WIFI_FAIL_COUNT){ _scanNum = 3; }
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[2] < MAX_WIFI_FAIL_COUNT){ _scanNum = 2; }
		}
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[1] < MAX_WIFI_FAIL_COUNT){ _scanNum = 1; }
		}
	}
	if (_scanNum < 0) {
		for (int i = 0; i < nets && _scanNum < 0; ++i) {
			if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[0] < MAX_WIFI_FAIL_COUNT){ _scanNum = 0; }
		}
	}
	return _scanNum;
}

void CLASS_CORE_WIFI::configureWifi() { // вход в STA-режим: скан сети / подключение
	if (wifiStatus == FS_STAT_APMODE) {return;}
	DEBUG_WIFI(__PRETTY_FUNCTION__);	DEBUG_WIFI("\r\n");
	//disconnect required here
	//improves reconnect reliability
	_suppressDisc = 3;   // события от собственного disconnect() игнорируем
	_ignoreDisconnect = true;
	if (WiFi.isConnected()) {	WiFi.disconnect(); 	}
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.mode(WIFI_STA);
	_ignoreDisconnect = false;
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;
	connectionTimout = 0;
	ledSetSteady(false);	// выход из steady-on при подключении
	_apUptime = 0;
	_apClientActivity = false;
	_nextStaScanAt = _stateSeconds;
	WiFi.scanNetworks(true);
	ledMacrosWifiScan();
}

#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiConnected()
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiConnected(WiFiEventStationModeConnected data)
#endif
{
	DEBUG_WIFI("WiFi Connected: Waiting for DHCP\n\r");
	if (CONNECTION_LED >= 0) {espLedOn(); 	}	// Turn LED on
	wifiDisconnectedSince = 0;
	// Сбрасываем счётчик неудачных попыток при успешном подключении
	resetWifiFailCounters();

}

// Do functions when we get IP.
//means we get nor,al connection 	
#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiConnectedGotIP() {
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
#endif
	ledMacrosWifiConnected();	// выход из всех wifi-морганий: steady-on

	DEBUG_WIFI("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DEBUG_WIFI("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUG_WIFI("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	wifiDisconnectedSince = 0;
	connectionTimout = 0;
	wifiStatus = FS_STAT_CONNECTED;
	_enterApPending = false;
	_suppressDisc = 0;

	core_state.signal("wifi.connected", BusValue::bo(true));
	core_state.signal("wifi.rssi", BusValue::i32((int32_t)WiFi.RSSI()));
	core_state.signal("wifi.ip", BusValue::str(WiFi.localIP().toString()));
	core_state.emit("wifi.just_connected");

#if defined(MODULE_UDP)
//udp start to listen
	module_udp.begin();
	//udp broadcast - we are online!
    if (module_udp.powerOnGet() == true ) {  broadcastSimple(); }
#endif
	core_ntp.ntpOnConnected();

#ifdef MODULE_OTACLIENT
	// Trigger OTA client check on WiFi connect (if powerOn enabled)
	module_otaclient.onWiFiConnect();
#endif

}

#if defined(ESP32)
void CLASS_CORE_WIFI::onWiFiDisconnected(WiFiEventInfo_t info) {
#endif
#if defined(ESP8266)
void CLASS_CORE_WIFI::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#endif

	// Собственные отключения (переключение режимов, restart) не обрабатываем
	if (_ignoreDisconnect || _suppressDisc > 0 || wifiStatus == FS_STAT_RESET) { return; }

#if defined(MODULE_UDP)
	module_udp.stop();	// always stop!
#endif
	core_ntp.ntpOnDisconected();
	ledSetSteady(false);	// выход из steady-on при отключении

	uint8_t reason = 0;
#if defined(ESP8266)
	reason = data.reason;
#endif
#if defined(ESP32)
	reason = info.wifi_sta_disconnected.reason;
#endif
	DEBUG_WIFI("STA disconnected, reason: %u\r\n", (unsigned)reason);

	// «Неверный пароль» определяем по точной причине из события, а не по WiFi.status().
	// NO_AP_FOUND (пропал роутер) неверным паролем НЕ считается.
	bool authFail = false;
#if defined(ESP8266)
	authFail = (reason == WIFI_DISCONNECT_REASON_AUTH_FAIL);
#endif
#if defined(ESP32)
	authFail = (reason == WIFI_REASON_AUTH_FAIL);
#endif

	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUG_WIFI("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));

	wifiStatus = FS_STAT_CONNECTING;
	connectionTimout = 0;

	core_state.signal("wifi.connected", BusValue::bo(false));
	core_state.emit("wifi.just_disconnected");

	if (authFail) {
		DEBUG_WIFI("Auth fail (wrong password?): %s\r\n", _wifiConfig.ssid.c_str());
		wifiSsidSetPSWDwrong(_wifiConfig.ssid);
		ledMacrosWifiDisconnect();
		if (anySlotFree()) {
			// Остались незаблокированные слоты — пересканируем и пробуем следующий
			WifiScan = WF_STAT_SCANING;
			rescanSoon();
		} else {
			// Все сети заблокированы — в AP, чтобы пользователь исправил пароль
			_enterApPending = true;
		}
		return;
	}

	// Роутер пропал или попытка подключения не удалась.
	// Логика: сначала AP ждёт клиента (если включена), затем скан сети.
	WifiScan = WF_STAT_SCANING;
	if (_wifiAPLifeTime > 0) {
		_enterApPending = true;
	} else {
		rescanSoon();
	}
}

void CLASS_CORE_WIFI::wifiSsidSetPSWDwrong(String _str) {
	DEBUG_WIFI("wifi ssid wrong password: %s \n", _str.c_str());
	// Вместо безвозвратного удаления SSID — инкрементируем счётчик неудач
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	_wifiFailCount[3]++; }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	_wifiFailCount[2]++; }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	_wifiFailCount[1]++; }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	_wifiFailCount[0]++; }
}

void CLASS_CORE_WIFI::resetWifiFailCounters() {
	DEBUG_WIFI("resetWifiFailCounters\n");
	for (int i = 0; i < 4; i++) {
		_wifiFailCount[i] = 0;
	}
}

String CLASS_CORE_WIFI::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}

String CLASS_CORE_WIFI::buildNetworksJson() {
    String json = "[";
    int n = WiFi.scanComplete();
    
    // НЕ запускаем WiFi.scanNetworks() из HTTP-контекста!
    // Сканирование запускается ТОЛЬКО через /wifi/scan эндпоинт
    // Если сканирование не завершено или не запущено — возвращаем пустой массив
    if (n > 0) {
        for (int i = 0; i < n; ++i) {
            if (i) json += ",";
            json += "{";
            json += "\"rssi\":" + String(WiFi.RSSI(i));
            json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
            json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
            json += ",\"channel\":" + String(WiFi.channel(i));
            json += ",\"secure\":" + String(WiFi.encryptionType(i));
#ifdef ESP8266
            json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
#endif
#ifdef ESP32
            // TODO: Add hidden network support for ESP32 if needed
#endif
            json += "}";
        }
        WiFi.scanDelete();
    }
    json += "]";
    return json;
}

// ============================================================
// Светодиодная индикация статуса Wi-Fi
// ============================================================

void ledMacrosWifiScan()			{	ledSetState(LED_PRIO_WIFI, patWifiScan, 10);  }
void ledMacrosWifiDisconnect()		{	ledSetState(LED_PRIO_WIFI, patWifiDisc, 3);  }
void ledMacrosWifiAP()				{	ledSetState(LED_PRIO_WIFI, patWifiAP, -1); }
void ledMacrosWifiConnecting()		{	ledSetState(LED_PRIO_WIFI, patWifiConn, 2); }
void ledMacrosWifiError()			{	ledSetState(LED_PRIO_WIFI, patWifiErr, 5); }
void ledMacrosWifiConnected()		{	ledSetSteady(true); ledClearState(LED_PRIO_WIFI); }
