#include "main.h"
#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#endif
#if defined(ESP8266)
#include <LittleFS.h>
#endif

#include <DNSServer.h>

#include "FSWebServerLib.h"
#include "main.h"
#include "common.h"
#include "debug.h"

#include "core_json/core_json.h"
#include "core_wifi/core_wifi.h"


#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif


#include "core_ntp/core_ntp.h"

#include "core_led/core_led.h"
#include "core_wifi_version.h"

#ifdef MODULE_OTACLIENT
#include "module_otaclient/module_otaclient.h"
#endif



CORE_CLASS_WIFI 	modWifiClass(false);
DNSServer 		dnsServer;

CORE_CLASS_WIFI :: CORE_CLASS_WIFI (bool _in) {
	 dumb = _in;
 }

void CORE_CLASS_WIFI::s_secondTick(void* arg) {
	CORE_CLASS_WIFI* self = reinterpret_cast<CORE_CLASS_WIFI*>(arg);

	//DNS captive
	if (self->wifiStatus == FS_STAT_APMODE) {	dnsServer.processNextRequest();	}
	
	// Периодический сброс счётчиков неудачных попыток (каждые 60 секунд)
	if (self->connectionTimout % 60 == 0 && self->connectionTimout > 0) {
		bool anyBlocked = false;
		for (int i = 0; i < 4; i++) {
			if (self->_wifiFailCount[i] >= MAX_WIFI_FAIL_COUNT) { anyBlocked = true; break; }
		}
		if (anyBlocked) {
			DEBUGLOGWIFI("Periodic reset of wifi fail counters\n");
			self->resetWifiFailCounters();
		}
	}

//Check connection timeout if enabled
	if (self->scanTime > 0) {
		if (self->wifiStatus == FS_STAT_CONNECTING) 	{
			if (++self->connectionTimout >= self->scanTime){
				DEBUGLOGWIFI("Connection Timeout. Switching to AP Mode.\r\n");
				self->WifiScan = WF_SCAN_NO_NEED;
				self->configureWifiAP();
				ledMacrosWifiAP();
			}
		}
		if (self->wifiStatus == FS_STAT_WRONGPASSWORDS) {
			DEBUGLOGWIFI("All passwords wrong. Switching to AP Mode.\r\n");
			self->WifiScan = WF_SCAN_NO_NEED;
			self->configureWifiAP();
			ledMacrosWifiError();
		}
		
		if (self->WifiScan == WF_STAT_SCANED)	{
			self->configureWifi();
			self->WifiScan = WF_SCAN_NO_NEED;
			ledMacrosWifiConnecting();
		}
		
		if (self->WifiScan != WF_SCAN_NO_NEED) {
			self->load_configWifi(self->scanWifi());
			ledMacrosWifiScan();
		}
		if (self->wifiStatus == FS_STAT_CONNECTED && (CONNECTION_LED >= 0) ) {  flashLEDOnConnected(); }
	}

// AP mode — scantime timeout and rescan logic
	if (self->wifiStatus == FS_STAT_APMODE && self->scanTime > 0) {
		if (++self->_apUptime >= self->scanTime) {
			if (WiFi.softAPgetStationNum() == 0) {
				DEBUGLOGWIFI("AP timeout, no clients. Re-scanning.\r\n");
				self->_apUptime = 0;
				self->WifiScan = WF_STAT_SCANING;
				self->configureWifi();
				ledMacrosWifiScan();
			} else {
				self->_apUptime = 0;
			}
		}
	}

// AP mode — client idle timeout
	if (self->wifiStatus == FS_STAT_APMODE && self->_wifiAPLifeTime > 0) {
		if (WiFi.softAPgetStationNum() > 0) {
			if (self->_apClientActivity) {
				self->_apClientIdleSec = 0;
				self->_apClientActivity = false;
			} else {
				if (++self->_apClientIdleSec >= self->_wifiAPLifeTime * 60) {
					DEBUGLOGWIFI("AP client idle timeout, disconnecting client.\r\n");
					WiFi.softAPdisconnect(true);
					self->_apClientIdleSec = 0;
				}
			}
		} else {
			self->_apClientIdleSec = 0;
		}
	}

}

#if defined(ESP32)
void CORE_CLASS_WIFI::begin(fs::LittleFSFS* fs)
#endif
#if defined(ESP8266)
    void CORE_CLASS_WIFI::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
	if (!_fs) { _fs->begin();  }// If LittleFS is not started
	connectionTimout = 0;
	String hostName = ESPHTTPServer.getHostName();
	WiFi.hostname(hostName.c_str());
	// Отключаем энергосбережение WiFi - иначе при длительном
	// простое вкладки ESP уходит в modem-sleep и сбрасывается (~5 мин)
#if defined(ESP32)
	WiFi.setSleep(false);
#endif
#if defined(ESP8266)
	WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
	if (AP_ENABLE_BUTTON >= 0) {
		// Set AP mode if AP button was pressed
		if (_apConfig.APenable) {	configureWifiAP();	}
		// Set WiFi config
		else {	configureWifi();	}
	}
	// Set WiFi config
	else {	configureWifi(); 	}
	_secondTk.attach(1.0f, &CORE_CLASS_WIFI::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second

	// Try to load configuration from file system// Load defaults if any error
	if (!load_configWifi(3)) { defaultConfigWifi(3); _apConfig.APenable = true; 	}
	if (!load_configWifi(2)) { defaultConfigWifi(2); _apConfig.APenable = true; 	}
	if (!load_configWifi(1)) { defaultConfigWifi(1); _apConfig.APenable = true; 	}
	if (!load_configWifi(0)) { defaultConfigWifi(0); _apConfig.APenable = true;		}
	if (!load_configWifiSys()) { defaultConfigWifiSys(); }
	scanTime = _wifiScanTime * MINUTES;
	DEBUGLOGWIFI("_strWifis[0] %s\r\n", _strWifi0);
	DEBUGLOGWIFI("_strWifis[1] %s\r\n", _strWifi1);
	DEBUGLOGWIFI("_strWifis[2] %s\r\n", _strWifi2);
	DEBUGLOGWIFI("_strWifis[3] %s\r\n", _strWifi3);

// Register wifi Event to control connection LED and wifi connection status
	#if defined(ESP32)
	onStationModeConnectedHandler 		= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiConnected();		},	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
	onStationModeDisconnectedHandler 	= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiDisconnected();		},	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
	onStationModeGotIPHandler 			= WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info)	{	this->onWiFiConnectedGotIP();	}, 	WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
#endif
#if defined(ESP8266)
	onStationModeConnectedHandler 		= WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) 		{	this->onWiFiConnected(data);		});
	onStationModeDisconnectedHandler 	= WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) 	{	this->onWiFiDisconnected(data);		});
	onStationModeGotIPHandler 			= WiFi.onStationModeGotIP([this](WiFiEventStationModeGotIP data) 				{	this->onWiFiConnectedGotIP(data);	});
#endif

}


bool CORE_CLASS_WIFI::load_configWifi(int _in) {
	if (_in < 0){ return false; }
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	if (!ModClassJson.jsonFileLoadSlot(filename, _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
	                                   _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns)) {
	    return false;
	}
	if (_in == 0)  sprintf(_strWifi0, "%s", _wifiConfig.ssid.c_str());
	if (_in == 1)  sprintf(_strWifi1, "%s", _wifiConfig.ssid.c_str());
	if (_in == 2)  sprintf(_strWifi2, "%s", _wifiConfig.ssid.c_str());
	if (_in == 3)  sprintf(_strWifi3, "%s", _wifiConfig.ssid.c_str());

	return true;
}


bool CORE_CLASS_WIFI::save_configWifi(int _in) {
	//flag_config = false;
	DEBUGLOGWIFI("Save config\r\n");
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	return ModClassJson.jsonFileSaveSlot(filename, _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
	                                      _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns);
}



void CORE_CLASS_WIFI::defaultConfigWifi(int _in) {
	// DEFAULT CONFIG
	_wifiConfig.ssid 		= "YOUR_DEFAULT_WIFI_SSID";
	_wifiConfig.password 	= "YOUR_DEFAULT_WIFI_PASSWD";
	_wifiConfig.dhcp 		= 1;
	_wifiConfig.ip 			= IPAddress(192, 168, 1, 4);
	_wifiConfig.netmask 	= IPAddress(255, 255, 255, 0);
	_wifiConfig.gateway 	= IPAddress(192, 168, 1, 1);
	_wifiConfig.dns 		= IPAddress(192, 168, 1, 1);
	
	save_configWifi(_in);
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}


void CORE_CLASS_WIFI::startDNSCaptive() {
    // Перехватываем все DNS запросы и направляем на IP точки доступа
    dnsServer.start(53, "*", WiFi.softAPIP());
    DEBUGLOGWIFI("DNS captive portal started on port 53\n");
}

void CORE_CLASS_WIFI::configureWifiAP() {
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	modNtpClass.ntpOnDisconected();
#if defined(MODULE_UDP)
		udpBroadcast.udpStop();	// always stop!
#endif
	String APname = ESPHTTPServer.getHostName();
	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	wifiStatus = FS_STAT_APMODE;
	if (ESPHTTPServer._httpAuth.auth) {
		WiFi.softAP(APname, ESPHTTPServer._httpAuth.wwwPassword);
		DEBUGLOGWIFI("AP Pass enabled: %s \r\n", ESPHTTPServer._httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOGWIFI("AP Pass disabled \r\n");
	}
	startDNSCaptive();
	// if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 5, 250);	}
	DEBUGLOGWIFI("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
	_apUptime = 0;
	_apClientIdleSec = 0;
	_apClientActivity = false;
}

int CORE_CLASS_WIFI::scanWifi() {
	int _scanNum = -1;

	int nets = WiFi.scanComplete();
	if (nets == WIFI_SCAN_FAILED) {	WiFi.scanNetworks(true);	}
	if (nets > 0) {
		// Ищем SSID в порядке приоритета (сначала слот 3, потом 2, 1, 0)
		// Пропускаем SSID, у которых превышен лимит неудачных попыток
		for (int i = 0; i < nets; ++i) {
			if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[3] < MAX_WIFI_FAIL_COUNT){ _scanNum = 3; }
		}
		if (_scanNum < 0) {
			for (int i = 0; i < nets; ++i) {
				if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[2] < MAX_WIFI_FAIL_COUNT){ _scanNum = 2; }
			}
		}
		if (_scanNum < 0) {
			for (int i = 0; i < nets; ++i) {
				if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[1] < MAX_WIFI_FAIL_COUNT){ _scanNum = 1; }
			}
		}
		if (_scanNum < 0) {
			for (int i = 0; i < nets; ++i) {
				if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0 && _wifiFailCount[0] < MAX_WIFI_FAIL_COUNT){ _scanNum = 0; }
			}
		}
		WiFi.scanDelete();
	}
	if (_scanNum >= 0) {	WifiScan = WF_STAT_SCANED;	}

	DEBUGLOGWIFI("timeout: %d _scanNum = %d nets = %d \r\n", (scanTime - connectionTimout), _scanNum, nets);
	return _scanNum;
}


void CORE_CLASS_WIFI::configureWifi() { // set esp8266 as wifi client
	if (wifiStatus == FS_STAT_APMODE) {return;}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	//disconnect required here
	//improves reconnect reliability
	if (WiFi.isConnected()) {	WiFi.disconnect(); 	}
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.mode(WIFI_STA);
	if (WifiScan == WF_STAT_SCANED){
		DEBUGLOGWIFI("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
		WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	}  else  {
		WiFi.scanNetworks(true);
	}
	wifiStatus = FS_STAT_CONNECTING;
//Only use wait waitForConnectResult if the timeout is not enabled to not mess with the timeout
	if (scanTime <= 0) { WiFi.waitForConnectResult(); }
	_apUptime = 0;
	_apClientIdleSec = 0;
	_apClientActivity = false;

}



#if defined(ESP32)
void CORE_CLASS_WIFI::onWiFiConnected()
#endif
#if defined(ESP8266)
void CORE_CLASS_WIFI::onWiFiConnected(WiFiEventStationModeConnected data)
#endif
{
	DEBUGLOGWIFI("WiFi Connected: Waiting for DHCP\n\r");
	if (CONNECTION_LED >= 0) {espLedOn(); 	}	// Turn LED on
	wifiDisconnectedSince = 0;
	// Сбрасываем счётчик неудачных попыток при успешном подключении
	resetWifiFailCounters();

}


// Do functions when we get IP.
//means we get nor,al connection 	
#if defined(ESP32)
void CORE_CLASS_WIFI::onWiFiConnectedGotIP() {
#endif
#if defined(ESP8266)
void CORE_CLASS_WIFI::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
#endif
	if (CONNECTION_LED >= 0) { espLedOn(); 	} // Turn LED on

	DEBUGLOGWIFI("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DEBUGLOGWIFI("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUGLOGWIFI("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	wifiDisconnectedSince = 0;
	connectionTimout = 0;
	wifiStatus = FS_STAT_CONNECTED;
#if defined(MODULE_UDP)
//udp start to listen
	udpBroadcast.begin();
	udpBroadcast.webInit();
	//udp broadcast - we are online!
    if (udpBroadcast.udpPowerOnGet() == true ) {  udpBroadcastSimple(); }
#endif
	modNtpClass.ntpOnConnected();

#ifdef MODULE_OTACLIENT
	// Trigger OTA client check on WiFi connect (if powerOn enabled)
	otaClient.onWiFiConnect();
#endif

}

#if defined(ESP32)
void CORE_CLASS_WIFI::onWiFiDisconnected() {
#endif
#if defined(ESP8266)
void CORE_CLASS_WIFI::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#endif


#if defined(MODULE_UDP)
	udpBroadcast.udpStop();	// always stop!
#endif
	modNtpClass.ntpOnDisconected();

	if (wifiStatus == FS_STAT_RESET) {return;}

DEBUGLOGWIFI(" case STA_DISCONNECTED \r\n");
#if defined(ESP8266)
	// Используем точную причину отключения из события,
	// чтобы не ловить ложные "wrong password" при временных сбоях
	if (data.reason == WIFI_DISCONNECT_REASON_AUTH_FAIL ||
		data.reason == WIFI_DISCONNECT_REASON_AUTH_EXPIRE ||
		data.reason == WIFI_DISCONNECT_REASON_AUTH_LEAVE ||
		data.reason == WIFI_DISCONNECT_REASON_NO_AP_FOUND) {
#endif
#if defined(ESP32)
	if(WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_SSID_AVAIL) {
#endif
		wifiStatus = FS_STAT_WRONGPASSWORDS;
		WifiScan = WF_SCAN_NO_NEED;
		wifiSsidSetPSWDwrong(_wifiConfig.ssid);
		WiFi.disconnect();		// anyway need it to avoid wifi logic errors
		ledMacrosWifiDisconnect()	;
	}

	// if (CONNECTION_LED >= 0) {	espLedOff();	}// Turn LED off
	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUGLOGWIFI("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;

}

void CORE_CLASS_WIFI::wifiSsidSetPSWDwrong(String _str) {
	DEBUGLOGWIFI("wifi ssid wrong password: %s \n", _str.c_str());
	// Вместо безвозвратного удаления SSID — инкрементируем счётчик неудач
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	_wifiFailCount[3]++; }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	_wifiFailCount[2]++; }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	_wifiFailCount[1]++; }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	_wifiFailCount[0]++; }
}

void CORE_CLASS_WIFI::resetWifiFailCounters() {
	DEBUGLOGWIFI("resetWifiFailCounters\n");
	for (int i = 0; i < 4; i++) {
		_wifiFailCount[i] = 0;
	}
}
 

void CORE_CLASS_WIFI::send_info_values_html(AsyncWebServerRequest *request) {
	DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	String state = "N/A";
	String Networks = "";
	if (WiFi.status() == 0) {	state = "Idle";	}
	if (WiFi.status() == 1) {	state = "NO SSID AVAILBLE";}
	if (WiFi.status() == 2) {	state = "SCAN COMPLETED";}
	if (WiFi.status() == 3) {	state = "CONNECTED";}
	if (WiFi.status() == 4) {	state = "CONNECT FAILED";}
	if (WiFi.status() == 5) {	state = "CONNECTION LOST";}
	if (WiFi.status() == 6) {	state = "DISCONNECTED";}

	String values = "";
	values += "connectionstate|" + state + "|div\n";
	
	values += "x_ssid|" 	+ (String)WiFi.SSID() + "|div\n";
	values += "x_ip|" 		+ (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + "|div\n";
	values += "x_gateway|" 	+ (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + "|div\n";
	values += "x_netmask|" 	+ (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + "|div\n";
	values += "x_mac|" 		+ getMacAddress() + "|div\n";
	values += "x_dns|" 		+ (String)WiFi.dnsIP()[0] + "." + (String)WiFi.dnsIP()[1] + "." + (String)WiFi.dnsIP()[2] + "." + (String)WiFi.dnsIP()[3] + "|div\n";

	request->send(200, "text/plain", values);
	state = "";
	values = "";
	Networks = "";
}

String CORE_CLASS_WIFI::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}




void CORE_CLASS_WIFI::send_scanwifi(AsyncWebServerRequest *request) {
    String json = buildNetworksJson();
    request->send(200, "text/json", json);
}

void CORE_CLASS_WIFI::send_scanwifi_trigger(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI(__FUNCTION__); DEBUGLOGWIFI("\r\n");
    int scanStatus = WiFi.scanComplete();
    String json = "{";
    
    if (scanStatus == WIFI_SCAN_RUNNING) {
        json += "\"status\":\"already_running\"";
        DEBUGLOGWIFI("Scan already running\n");
    } else {
        WiFi.scanNetworks(true);
        json += "\"status\":\"started\"";
        DEBUGLOGWIFI("Scan triggered\n");
    }
    
    json += "}";
    request->send(200, "application/json", json);
}

String CORE_CLASS_WIFI::buildNetworksJson() {
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


void CORE_CLASS_WIFI::send_network_configuration_html(AsyncWebServerRequest *request) {
	// DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		//bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOGWIFI("Arg %d: %s\r\n", i, request->arg(i).c_str());
			if (request->argName(i) == "ssid") 		{ _wifiConfig.ssid = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "password")	{ _wifiConfig.password = urldecode(request->arg(i)); continue; }
			if (request->argName(i) == "ip_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "ip_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.ip[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "nm_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.netmask[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_0")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_1")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_2")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "gw_3")  { if (checkRange(request->arg(i))) 	_wifiConfig.gateway[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_0") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[0] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_1") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[1] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_2") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[2] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dns_3") { if (checkRange(request->arg(i))) 	_wifiConfig.dns[3] = request->arg(i).toInt(); continue; }
			if (request->argName(i) == "dhcp")  { _wifiConfig.dhcp = true; continue; }
			if (request->argName(i) == "wificonf")  { if (checkRange(request->arg(i))) 	_saveIn = request->arg(i).toInt(); continue; }
		}
		request->send_P(200, "text/html", Page_ConfigRefresh);

		if (_saveIn == 0) {save_configWifi(0);}
		if (_saveIn == 1) {save_configWifi(1);}
		if (_saveIn == 2) {save_configWifi(2);}
		if (_saveIn == 3) {save_configWifi(3);}

	}
	else {
		DEBUGLOGWIFI(request->url().c_str());
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}
// wifi.html ^^^


void CORE_CLASS_WIFI::webInit () {
    ESPHTTPServer.on("/wifi.html", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        this->send_network_configuration_html(request);
    });

    ESPHTTPServer.on("/wifi/info", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        this->send_info_values_html(request);
    });

    // JSON-эндпоинты для слотов
    for (int i = 0; i < 4; i++) {
        String path = "/api/wifi/slot/" + String(i);
        
        // GET - получение данных слота в JSON
        ESPHTTPServer.on(path.c_str(), HTTP_GET, [this, i](AsyncWebServerRequest *request) {
            if (!ESPHTTPServer.checkAuth(request)) {
                return request->requestAuthentication();
            }
            this->send_slot_json(request, i);
        });
        
        // POST - сохранение данных слота (вызывает handle_slot_post)
        ESPHTTPServer.on(path.c_str(), HTTP_POST,
            [this, i](AsyncWebServerRequest *request) {
                this->handle_slot_post(request, i);
            },
            NULL,
            [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
                this->handle_slot_upload(request, data, len, index, total);
            }
        );
    }

    ESPHTTPServer.on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {
            return request->requestAuthentication();
        }
        this->send_scanwifi(request);
    });

    // Эндпоинт для запуска сканирования WiFi (только для страницы wifi.html)
    ESPHTTPServer.on("/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {
            return request->requestAuthentication();
        }
        this->send_scanwifi_trigger(request);
    });

    //captive
    ESPHTTPServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        modWifiClass.notifyApClientActivity();
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        modWifiClass.notifyApClientActivity();
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        modWifiClass.notifyApClientActivity();
        request->send(200, "text/plain", "Microsoft NCSI");
    });

    ESPHTTPServer.on("/wifi/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

    ESPHTTPServer.on("/wifi/sysconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); }
        send_wifi_sysconf_json(request);
    });

    ESPHTTPServer.on("/wifi/sysconf", HTTP_POST,
        [this](AsyncWebServerRequest *request) { handle_wifi_sysconf_post(request); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            handle_slot_upload(request, data, len, index, total);
        }
    );
}

void CORE_CLASS_WIFI::send_slot_json(AsyncWebServerRequest *request, int slot) {
    DEBUGLOGWIFI("Sending slot %d data as JSON\n", slot);
    load_configWifi(slot);

    String response = ModClassJson.jsonBuildSlotConfig(
        _wifiConfig.ssid, _wifiConfig.password, _wifiConfig.dhcp,
        _wifiConfig.ip, _wifiConfig.netmask, _wifiConfig.gateway, _wifiConfig.dns
    );
    request->send(200, "application/json", response);
}
void CORE_CLASS_WIFI::handle_slot_post(AsyncWebServerRequest *request, int slot) {
    if (!ESPHTTPServer.checkAuth(request)) {
        // Освобождаем память перед возвратом
        if (request->_tempObject) {
            free(request->_tempObject);
            request->_tempObject = NULL;
        }
        return request->requestAuthentication();
    }
    
    if (request->_tempObject != NULL) {
        String body = String((char*)request->_tempObject);
        DEBUGLOGWIFI("Received body for slot %d: %s\n", slot, body.c_str());
        
        String ssid, password;
        bool dhcp = false;
        IPAddress ip, netmask, gateway, dns;
        
        this->load_configWifi(slot);
        ssid = this->_wifiConfig.ssid;
        password = this->_wifiConfig.password;
        dhcp = this->_wifiConfig.dhcp;
        ip = this->_wifiConfig.ip;
        netmask = this->_wifiConfig.netmask;
        gateway = this->_wifiConfig.gateway;
        dns = this->_wifiConfig.dns;
        
        int parsed = ModClassJson.jsonParseSlotConfig(body, ssid, password, dhcp,
                                                        ip, netmask, gateway, dns);
        
        // Освобождаем память
        free(request->_tempObject);
        request->_tempObject = NULL;
        
        if (parsed == 0) {
            DEBUGLOGWIFI("JSON parse error\n");
            request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
            return;
        }
        
        this->_wifiConfig.ssid = ssid;
        this->_wifiConfig.password = password;
        this->_wifiConfig.dhcp = dhcp;
        this->_wifiConfig.ip = ip;
        this->_wifiConfig.netmask = netmask;
        this->_wifiConfig.gateway = gateway;
        this->_wifiConfig.dns = dns;
        
        bool saveResult = this->save_configWifi(slot);
        
        if (saveResult) {
            request->send(200, "application/json", "{\"success\":true}");
            DEBUGLOGWIFI("Saved slot %d ok.\n", slot);
        } else {
            request->send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
            DEBUGLOGWIFI("Saved slot %d failed.\n", slot);
        }
    } else {
        DEBUGLOGWIFI("No body data for slot %d\n", slot);
        request->send(400, "application/json", "{\"success\":false,\"error\":\"No data\"}");
    }
}
// Обработчик для загрузки тела запроса (добавьте эту функцию)
void CORE_CLASS_WIFI::handle_slot_upload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!request->_tempObject) {
        char *buff = (char*)malloc(total + 1);
        request->_tempObject = buff;
    }
    
    char *buff = (char*)request->_tempObject;
    memcpy(buff + index, data, len);
    
    if (index + len == total) { buff[total] = '\0'; }
}

String CORE_CLASS_WIFI::getVersionStr(){
    return String(CORE_WIFI_VERSION);
}

String CORE_CLASS_WIFI::getGeneratedTime(){
    return String(CORE_WIFI_GENERATED_TIME);
}

String CORE_CLASS_WIFI::getCommitDateStr(){
    return String(CORE_WIFI_COMMIT_DATE_STR);
}

void CORE_CLASS_WIFI::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("%s\n\r", __FUNCTION__);
    String values = "";
    values += "wifiversion|"     + getVersionStr()    + "|div\n";
    values += "wifigentime|"     + getGeneratedTime() + "|div\n";
    values += "wifigendate|"     + getCommitDateStr() + "|div\n";
    request->send(200, "text/plain", values);
}

bool CORE_CLASS_WIFI::load_configWifiSys() {
    DEBUGLOGWIFI("Loading WiFi sys config\n");
    JsonDocument doc;
    if (!ModClassJson.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc)) return false;
    _wifiScanTime = doc["scantime"].as<uint16_t>();
    _wifiAPLifeTime = doc["aptime"].as<uint16_t>();
    return true;
}

bool CORE_CLASS_WIFI::save_configWifiSys() {
    DEBUGLOGWIFI("Saving WiFi sys config\n");
    JsonDocument doc;
    ModClassJson.jsonFileLoadDoc(WIFI_CONFIG_SYS, doc);
    doc["scantime"] = _wifiScanTime;
    doc["aptime"] = _wifiAPLifeTime;
    return ModClassJson.jsonFileSaveDoc(WIFI_CONFIG_SYS, doc);
}

void CORE_CLASS_WIFI::defaultConfigWifiSys() {
    DEBUGLOGWIFI("defaultConfigWifiSys\n");
    _wifiScanTime = 1;
    _wifiAPLifeTime = 10;
}

void CORE_CLASS_WIFI::send_wifi_sysconf_json(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("send_wifi_sysconf_json\n");
    String values = "";
    values += "scantime_hours|" + String(_wifiScanTime / 60) + "|input\n";
    values += "scantime_mins|" + String(_wifiScanTime % 60) + "|input\n";
    values += "aptime|" + String(_wifiAPLifeTime) + "|input\n";
    request->send(200, "text/plain", values);
}

void CORE_CLASS_WIFI::handle_wifi_sysconf_post(AsyncWebServerRequest *request) {
    DEBUGLOGWIFI("handle_wifi_sysconf_post\n");
    if (!ESPHTTPServer.checkAuth(request)) {
        if (request->_tempObject) { free(request->_tempObject); request->_tempObject = NULL; }
        return request->requestAuthentication();
    }

    if (!request->_tempObject) { request->send(400, "application/json", "{\"success\":false}"); return; }

    String body = String((char*)request->_tempObject);
    free(request->_tempObject);
    request->_tempObject = NULL;

    int32_t scantimeVal = 0, aptimeVal = 0;
    if (!ModClassJson.jsonParseInt(body, "scantime", scantimeVal)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
        return;
    }
    if (scantimeVal < 0) scantimeVal = 0;
    if (scantimeVal > 720) scantimeVal = 720;
    _wifiScanTime = (uint16_t)scantimeVal;

    ModClassJson.jsonParseInt(body, "aptime", aptimeVal);
    if (aptimeVal < 0) aptimeVal = 0;
    if (aptimeVal > 60) aptimeVal = 60;
    _wifiAPLifeTime = (uint16_t)aptimeVal;

    if (save_configWifiSys()) {
        scanTime = _wifiScanTime * MINUTES;
        request->send(200, "application/json", "{\"success\":true}");
        DEBUGLOGWIFI("WiFi sys config saved: scantime=%d, aptime=%d\n", _wifiScanTime, _wifiAPLifeTime);
    } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"Save failed\"}");
    }
}

