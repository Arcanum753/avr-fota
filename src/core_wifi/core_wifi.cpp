#include "main.h"
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#endif
#if defined(ESP8266)
#include <FS.h>
#endif

#include <DNSServer.h>
#include <ArduinoJson.h>

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



CORE_CLASS_WIFI 	modWifiClass(false);
DNSServer 		dnsServer;

CORE_CLASS_WIFI :: CORE_CLASS_WIFI (bool _in) {
	 dumb = _in;
 }

void CORE_CLASS_WIFI::s_secondTick(void* arg) {
	CORE_CLASS_WIFI* self = reinterpret_cast<CORE_CLASS_WIFI*>(arg);

	//DNS captive
	if (self->wifiStatus == FS_STAT_APMODE) {	dnsServer.processNextRequest();	}
	

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

}

#if defined(ESP32)
void CORE_CLASS_WIFI::begin(fs::SPIFFSFS* fs)
#endif
#if defined(ESP8266)
    void CORE_CLASS_WIFI::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
	connectionTimout = 0;
	scanTime = ESPHTTPServer.configSys_ScanTimeGet();
	scanTime *= MINUTES;
	String hostName = ESPHTTPServer.getHostName();
	WiFi.hostname(hostName.c_str());
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
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(filename, jsonDoc) == false){ return false; }

	_wifiConfig.ssid = jsonDoc["ssid"].as<const char *>();
	if (_in == 0)  sprintf(_strWifi0, "%s", _wifiConfig.ssid.c_str());
	if (_in == 1)  sprintf(_strWifi1, "%s", _wifiConfig.ssid.c_str());
	if (_in == 2)  sprintf(_strWifi2, "%s", _wifiConfig.ssid.c_str());
	if (_in == 3)  sprintf(_strWifi3, "%s", _wifiConfig.ssid.c_str());

	_wifiConfig.password = jsonDoc["pass"].as<const char *>();
	_wifiConfig.ip = IPAddress(jsonDoc["ip"][0], jsonDoc["ip"][1], jsonDoc["ip"][2], jsonDoc["ip"][3]);
	_wifiConfig.netmask = IPAddress(jsonDoc["netmask"][0], jsonDoc["netmask"][1], jsonDoc["netmask"][2], jsonDoc["netmask"][3]);
	_wifiConfig.gateway = IPAddress(jsonDoc["gateway"][0], jsonDoc["gateway"][1], jsonDoc["gateway"][2], jsonDoc["gateway"][3]);
	_wifiConfig.dns = IPAddress(jsonDoc["dns"][0], jsonDoc["dns"][1], jsonDoc["dns"][2], jsonDoc["dns"][3]);
	_wifiConfig.dhcp = jsonDoc["dhcp"].as<bool>();

	return true;
}


bool CORE_CLASS_WIFI::save_configWifi(int _in) {
	//flag_config = false;
	DEBUGLOGWIFI("Save config\r\n");
	JsonDocument jsonDoc;

	jsonDoc["ssid"] = _wifiConfig.ssid;
	jsonDoc["pass"] = _wifiConfig.password;
	jsonDoc["dhcp"] = _wifiConfig.dhcp;
	JsonArray jsonip = jsonDoc["ip"].to<JsonArray>();
	jsonip.add(_wifiConfig.ip[0]);
	jsonip.add(_wifiConfig.ip[1]);
	jsonip.add(_wifiConfig.ip[2]);
	jsonip.add(_wifiConfig.ip[3]);

	JsonArray jsonNM = jsonDoc["netmask"].to<JsonArray>();
	jsonNM.add(_wifiConfig.netmask[0]);
	jsonNM.add(_wifiConfig.netmask[1]);
	jsonNM.add(_wifiConfig.netmask[2]);
	jsonNM.add(_wifiConfig.netmask[3]);

	JsonArray jsonGateway = jsonDoc["gateway"].to<JsonArray>();
	jsonGateway.add(_wifiConfig.gateway[0]);
	jsonGateway.add(_wifiConfig.gateway[1]);
	jsonGateway.add(_wifiConfig.gateway[2]);
	jsonGateway.add(_wifiConfig.gateway[3]);

	JsonArray jsondns = jsonDoc["dns"].to<JsonArray>();
	jsondns.add(_wifiConfig.dns[0]);
	jsondns.add(_wifiConfig.dns[1]);
	jsondns.add(_wifiConfig.dns[2]);
	jsondns.add(_wifiConfig.dns[3]);

	//jsonDoc["led"] = config.connectionLed;

	//TODO add AP data to html
	File configFile ;

	if (_in == 0) {		 configFile = _fs->open(WIFI_CONFIG_FILE0, "w");	}
	if (_in == 1) {		 configFile = _fs->open(WIFI_CONFIG_FILE1, "w");	}
	if (_in == 2) {		 configFile = _fs->open(WIFI_CONFIG_FILE2, "w");	}
	if (_in == 3) {		 configFile = _fs->open(WIFI_CONFIG_FILE3, "w");	}

	if (!configFile) {
		DEBUGLOGWIFI("Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}

	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
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
}

int CORE_CLASS_WIFI::scanWifi() {
	int _scanNum = -1;

	int nets = WiFi.scanComplete();
	if (nets == WIFI_SCAN_FAILED) {	WiFi.scanNetworks(true);	}
	if (nets > 0) {
		for (int i = 0; i < nets; ++i) {
			if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 3; }
			if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 2; }
			if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 1; }
			if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 0; }
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
	if(WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_SSID_AVAIL)	  {
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
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	memset (_strWifi3, 0, sizeof(_strWifi3)); }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	memset (_strWifi2, 0, sizeof(_strWifi2)); }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	memset (_strWifi1, 0, sizeof(_strWifi1)); }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	memset (_strWifi0, 0, sizeof(_strWifi0)); }
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

	WiFi.scanNetworks(true);

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

String CORE_CLASS_WIFI::buildNetworksJson() {
    String json = "[";
    int n = WiFi.scanComplete();
    
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true);
    }
    else if (n) {
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
        if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);
        }
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
		bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
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

    //captive
    ESPHTTPServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->redirect("http://" + WiFi.softAPIP().toString());
    });

    ESPHTTPServer.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Microsoft NCSI");
    });

    ESPHTTPServer.on("/wifi/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });
}

void CORE_CLASS_WIFI::send_slot_json(AsyncWebServerRequest *request, int slot) {
    DEBUGLOGWIFI("Sending slot %d data as JSON\n", slot);
    load_configWifi(slot);
    
    JsonDocument jsonDoc;
    jsonDoc["ssid"] = _wifiConfig.ssid;
    jsonDoc["password"] = _wifiConfig.password;
    jsonDoc["dhcp"] = _wifiConfig.dhcp;
    
    JsonArray ip = jsonDoc["ip"].to<JsonArray>();
    ip.add(_wifiConfig.ip[0]);
    ip.add(_wifiConfig.ip[1]);
    ip.add(_wifiConfig.ip[2]);
    ip.add(_wifiConfig.ip[3]);
    
    JsonArray netmask = jsonDoc["netmask"].to<JsonArray>();
    netmask.add(_wifiConfig.netmask[0]);
    netmask.add(_wifiConfig.netmask[1]);
    netmask.add(_wifiConfig.netmask[2]);
    netmask.add(_wifiConfig.netmask[3]);
    
    JsonArray gateway = jsonDoc["gateway"].to<JsonArray>();
    gateway.add(_wifiConfig.gateway[0]);
    gateway.add(_wifiConfig.gateway[1]);
    gateway.add(_wifiConfig.gateway[2]);
    gateway.add(_wifiConfig.gateway[3]);
    
    JsonArray dns = jsonDoc["dns"].to<JsonArray>();
    dns.add(_wifiConfig.dns[0]);
    dns.add(_wifiConfig.dns[1]);
    dns.add(_wifiConfig.dns[2]);
    dns.add(_wifiConfig.dns[3]);
    
    String response;
    serializeJson(jsonDoc, response);
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
        
        JsonDocument jsonDoc;
        DeserializationError error = deserializeJson(jsonDoc, body);
        
        if (error) {
            DEBUGLOGWIFI("JSON parse error: %s\n", error.c_str());
            request->send(400, "application/json", "{\"success\":false,\"error\":\"JSON parse error\"}");
            
            // Освобождаем память
            free(request->_tempObject);
            request->_tempObject = NULL;
            return;
        }
        
        this->load_configWifi(slot);
        
        if (jsonDoc.containsKey("ssid")) this->_wifiConfig.ssid = jsonDoc["ssid"].as<String>();
        if (jsonDoc.containsKey("password")) this->_wifiConfig.password = jsonDoc["password"].as<String>();
        if (jsonDoc.containsKey("dhcp")) this->_wifiConfig.dhcp = jsonDoc["dhcp"].as<bool>();
        
        if (jsonDoc["ip"].is<JsonArray>()) {
            JsonArray ip = jsonDoc["ip"].as<JsonArray>();
            this->_wifiConfig.ip = IPAddress(ip[0], ip[1], ip[2], ip[3]);
        }
        
        if (jsonDoc["netmask"].is<JsonArray>()) {
            JsonArray nm = jsonDoc["netmask"].as<JsonArray>();
            this->_wifiConfig.netmask = IPAddress(nm[0], nm[1], nm[2], nm[3]);
        }
        
        if (jsonDoc["gateway"].is<JsonArray>()) {
            JsonArray gw = jsonDoc["gateway"].as<JsonArray>();
            this->_wifiConfig.gateway = IPAddress(gw[0], gw[1], gw[2], gw[3]);
        }
        
        if (jsonDoc["dns"].is<JsonArray>()) {
            JsonArray dns = jsonDoc["dns"].as<JsonArray>();
            this->_wifiConfig.dns = IPAddress(dns[0], dns[1], dns[2], dns[3]);
        }
        
        bool saveResult = this->save_configWifi(slot);
        
        // Освобождаем память после использования
        free(request->_tempObject);
        request->_tempObject = NULL;
        
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
    DEBUGUDP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "wifiversion|"     + getVersionStr()    + "|dev\n";
    values += "wifigentime|"     + getGeneratedTime() + "|dev\n";
    values += "wifigendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}

