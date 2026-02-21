#include "main.h"
#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include <ArduinoJson.h>
#include "FSWebServerLib.h"
#include "common.h"
#include "debug.h"

#include "module_json/module_json.h"
#include "module_wifi/module_wifi.h"
#include "module_udp/module_udp.h"
#include "module_ntp/module_ntp.h"

WIFIMOD_CLASS modWifiClass(false);


WIFIMOD_CLASS :: WIFIMOD_CLASS (bool _in) {
	 dumb = _in;
 }

void WIFIMOD_CLASS::s_secondTick(void* arg) {
	WIFIMOD_CLASS* self = reinterpret_cast<WIFIMOD_CLASS*>(arg);
	if (ESPHTTPServer._evs.count() > 0) {	modNtpClass.sendTimeData();	}
//Check connection timeout if enabled
#if (AP_ENABLE_TIMEOUT > 0)
	// DBG_OUTPUT_PORT.printf("timer%d\r\n", ++self->connectionTimout);
	if (self->wifiStatus == FS_STAT_CONNECTING) 	{
		if (++self->connectionTimout >= AP_ENABLE_TIMEOUT){
			DBG_OUTPUT_PORT.printf("Connection Timeout. Switching to AP Mode.\r\n");
			self->WifiScan = WF_SCAN_NO_NEED;
			self->configureWifiAP();
		}
	}
	if (self->wifiStatus == FS_STAT_WRONGPASSWORDS) {
		DBG_OUTPUT_PORT.printf("All passwords wrong. Switching to AP Mode.\r\n");
		self->WifiScan = WF_SCAN_NO_NEED;
		self->configureWifiAP();
	}

	if (self->WifiScan == WF_STAT_SCANED)	{
		self->configureWifi();
		self->WifiScan = WF_SCAN_NO_NEED;
	}

	if (self->WifiScan != WF_SCAN_NO_NEED) {
		self->load_configWifi(self->scanWifi());
	}
	
#endif //AP_ENABLE_TIMEOUT
}

#if defined(ESP32)
    void WIFIMOD_CLASS::begin(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void WIFIMOD_CLASS::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
	connectionTimout = 0;
	String hostName = ESPHTTPServer._sysConfig.deviceName + "_" + ESPHTTPServer._sysConfig.deviceSerial;
	WiFi.hostname(hostName.c_str());
	if (AP_ENABLE_BUTTON >= 0) {
		// Set AP mode if AP button was pressed
		if (_apConfig.APenable) {	configureWifiAP();	}
// Set WiFi config
	else {	configureWifi();	}
	}
	// Set WiFi config
	else {	configureWifi(); 	}
	_secondTk.attach(1.0f, &WIFIMOD_CLASS::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second
#if (USE_RESERV_WIFI > 0)
	if (!load_configWifi(3)) { defaultConfigWifi(3); _apConfig.APenable = true; 	}
	if (!load_configWifi(2)) { defaultConfigWifi(2); _apConfig.APenable = true; 	}
	if (!load_configWifi(1)) { defaultConfigWifi(1); _apConfig.APenable = true; 	}
#endif
// Try to load configuration from file system// Load defaults if any error
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
	#elif defined(ESP8266)
	onStationModeConnectedHandler 		= WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) 		{	this->onWiFiConnected(data);		});
	onStationModeDisconnectedHandler 	= WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) 	{	this->onWiFiDisconnected(data);		});
	onStationModeGotIPHandler 			= WiFi.onStationModeGotIP([this](WiFiEventStationModeGotIP data) 				{	this->onWiFiConnectedGotIP(data);	});
	#endif

}


bool WIFIMOD_CLASS::load_configWifi(int _in) {
	if (_in < 0){
		return false;
	}
	char filename[40];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	JsonDocument jsonDoc;
	if (!ModClassJson.load_jsonDoc(filename, jsonDoc)){
		return false;
	}

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


// TODO переделать !
bool WIFIMOD_CLASS::save_configWifi(int _in) {
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
#if (USE_RESERV_WIFI > 0)
	if (_in == 1) {		 configFile = _fs->open(WIFI_CONFIG_FILE1, "w");	}
	if (_in == 2) {		 configFile = _fs->open(WIFI_CONFIG_FILE2, "w");	}
	if (_in == 3) {		 configFile = _fs->open(WIFI_CONFIG_FILE3, "w");	}
#endif
	if (!configFile) {
		DEBUGLOGWIFI("Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}



void WIFIMOD_CLASS::defaultConfigWifi(int _in) {
	// DEFAULT CONFIG
	_wifiConfig.ssid = "YOUR_DEFAULT_WIFI_SSID";
	_wifiConfig.password = "YOUR_DEFAULT_WIFI_PASSWD";
	_wifiConfig.dhcp 		= 1;
	_wifiConfig.ip 			= IPAddress(192, 168, 1, 4);
	_wifiConfig.netmask 	= IPAddress(255, 255, 255, 0);
	_wifiConfig.gateway 	= IPAddress(192, 168, 1, 1);
	_wifiConfig.dns 		= IPAddress(192, 168, 1, 1);
	//config.connectionLed = CONNECTION_LED;
	save_configWifi(_in);
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}

void WIFIMOD_CLASS::configureWifiAP() {
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");

	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	wifiStatus = FS_STAT_APMODE;

	

	String APname = ESPHTTPServer._sysConfig.deviceName + "_" + ESPHTTPServer._sysConfig.deviceSerial;
	if (ESPHTTPServer._httpAuth.auth) {
		WiFi.softAP(APname, ESPHTTPServer._httpAuth.wwwPassword);
		DEBUGLOGWIFI("AP Pass enabled: %s \r\n", ESPHTTPServer._httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOGWIFI("AP Pass disabled \r\n");
	}
	if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 3, 250);	}
	DBG_OUTPUT_PORT.printf("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
}

int WIFIMOD_CLASS::scanWifi() {
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

	DEBUGLOGWIFI("timeout: %d _scanNum = %d nets = %d \r\n", (AP_ENABLE_TIMEOUT - connectionTimout), _scanNum, nets);
	return _scanNum;
}


void WIFIMOD_CLASS::configureWifi() { // set esp8266 as wifi client
	if (wifiStatus == FS_STAT_APMODE) {return;}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	//disconnect required here
	//improves reconnect reliability
	if (WiFi.isConnected()) {		WiFi.disconnect(); 	}
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.mode(WIFI_STA);
	if (WifiScan == WF_STAT_SCANED){
		DBG_OUTPUT_PORT.printf("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
		WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	}  else  {
		WiFi.scanNetworks(true);
	}
	wifiStatus = FS_STAT_CONNECTING;
//Only use wait waitForConnectResult if the timeout is not enabled to not mess with the timeout
#if (AP_ENABLE_TIMEOUT <= 0)
	WiFi.waitForConnectResult();
#endif //AP_ENABLE_TIMEOUT
}



#if defined(ESP32)
void WIFIMOD_CLASS::onWiFiConnected()	{
#elif ESP8266
void WIFIMOD_CLASS::onWiFiConnected(WiFiEventStationModeConnected data) {
#endif

	DBG_OUTPUT_PORT.println("WiFi Connected: Waiting for DHCP");
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
		//turnLedESPHTTPServer.on();
		DEBUGLOGWIFI("Led %d on\n", CONNECTION_LED);
	}
	wifiDisconnectedSince = 0;

}


// Do functions when we get IP.
//means we get nor,al connection
#if defined(ESP32)
void WIFIMOD_CLASS::onWiFiConnectedGotIP() {
#elif defined(ESP8266)
void WIFIMOD_CLASS::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
#endif
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW);
		 // Turn LED on
		 //turnLedESPHTTPServer.on();
	}

	DBG_OUTPUT_PORT.printf("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("Led %d on\n", CONNECTION_LED);
	wifiDisconnectedSince = 0;
	
	

	connectionTimout = 0;
	
	wifiStatus = FS_STAT_CONNECTED;

	//udp start to listen
	udpBroadcast.webInit();
	udpBroadcast.begin(udpBroadcast.getUpdPortRx());
	// TODO NTPBEGIN

	// FIXME put it into udp module
	//udp broadcast - we are online!
    if (udpBroadcast.getudpPowerOn() == true ) { 
		udpBroadcastSimple();
	}
	modNtpClass.ntpOnConnected();

}

#if defined(ESP32)
void WIFIMOD_CLASS::onWiFiDisconnected() {
#elif defined(ESP8266)
void WIFIMOD_CLASS::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#endif

	udpBroadcast.udpStop();	// always stop!
	modNtpClass.ntpOnDisconected();

	if (wifiStatus == FS_STAT_RESET) {return;}

DEBUGLOGWIFI(" case STA_DISCONNECTED \r\n");
	if(WiFi.status() != WL_CONNECTED && WiFi.status() != WL_NO_SSID_AVAIL)	  {
		wifiStatus = FS_STAT_WRONGPASSWORDS;
		WifiScan = WF_SCAN_NO_NEED;
		wifiSsidSetPSWDwrong(_wifiConfig.ssid);		
		WiFi.disconnect();		// anyway need it to avoid wifi logic errors
	}

	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH);
		// flashLED(config.connectionLed, 2, 100);
	} // Turn LED off
	// FIXME
	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUGLOGWIFI("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;

}

void WIFIMOD_CLASS::wifiSsidSetPSWDwrong(String _str) {
	DEBUGLOGWIFI("wifi ssid wrong password: %s \n", _str.c_str());
	if (strcmp( _strWifi3,  _str.c_str()) == 0)	{	memset (_strWifi3, 0, sizeof(_strWifi3)); }
	if (strcmp( _strWifi2,  _str.c_str()) == 0)	{	memset (_strWifi2, 0, sizeof(_strWifi2)); }
	if (strcmp( _strWifi1,  _str.c_str()) == 0)	{	memset (_strWifi1, 0, sizeof(_strWifi1)); }
	if (strcmp( _strWifi0,  _str.c_str()) == 0)	{	memset (_strWifi0, 0, sizeof(_strWifi0)); }
}
 
// wifi.html vvv
void WIFIMOD_CLASS::send_network_configuration_values_html(AsyncWebServerRequest *request, int _index) {
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
	load_configWifi(_index);
	String values = "";
	values += "ssid|" + (String)_wifiConfig.ssid + "|input\n";
	values += "password|" + (String)_wifiConfig.password + "|input\n";
	values += "ip_0|" + (String)_wifiConfig.ip[0] + "|input\n";
	values += "ip_1|" + (String)_wifiConfig.ip[1] + "|input\n";
	values += "ip_2|" + (String)_wifiConfig.ip[2] + "|input\n";
	values += "ip_3|" + (String)_wifiConfig.ip[3] + "|input\n";
	values += "nm_0|" + (String)_wifiConfig.netmask[0] + "|input\n";
	values += "nm_1|" + (String)_wifiConfig.netmask[1] + "|input\n";
	values += "nm_2|" + (String)_wifiConfig.netmask[2] + "|input\n";
	values += "nm_3|" + (String)_wifiConfig.netmask[3] + "|input\n";
	values += "gw_0|" + (String)_wifiConfig.gateway[0] + "|input\n";
	values += "gw_1|" + (String)_wifiConfig.gateway[1] + "|input\n";
	values += "gw_2|" + (String)_wifiConfig.gateway[2] + "|input\n";
	values += "gw_3|" + (String)_wifiConfig.gateway[3] + "|input\n";
	values += "dns_0|" + (String)_wifiConfig.dns[0] + "|input\n";
	values += "dns_1|" + (String)_wifiConfig.dns[1] + "|input\n";
	values += "dns_2|" + (String)_wifiConfig.dns[2] + "|input\n";
	values += "dns_3|" + (String)_wifiConfig.dns[3] + "|input\n";
	values += "dhcp|" + (String) (_wifiConfig.dhcp ? "checked" : "") + "|chk\n";
	request->send(200, "text/plain", values);
	values = "";


}



void WIFIMOD_CLASS::send_info_values_html(AsyncWebServerRequest *request) {
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

String WIFIMOD_CLASS::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}




void WIFIMOD_CLASS::send_scanwifi(AsyncWebServerRequest *request) {
	// DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	String json = "[";
	int n = WiFi.scanComplete();
	if (n == WIFI_SCAN_FAILED) {	WiFi.scanNetworks(true);	}
	else if (n) {
		for (int i = 0; i < n; ++i) {
			if (i) json += ",";
			json += "{";
			json += "\"rssi\":" 	+ String(WiFi.RSSI(i));
			json += ",\"ssid\":\"" 	+ WiFi.SSID(i) 			+ "\"";
			json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) 		+ "\"";
			json += ",\"channel\":" + String(WiFi.channel(i));
			json += ",\"secure\":" 	+ String(WiFi.encryptionType(i));
			#ifdef ESP8266
			json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
			#endif
			#ifdef ESP32
			//TODO
			#endif
			json += "}";
		}
		WiFi.scanDelete();
		if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {	WiFi.scanNetworks(true);	}
	}
	json += "]";
	request->send(200, "text/json", json);
	json = "";
}

void WIFIMOD_CLASS::send_network_configuration_html(AsyncWebServerRequest *request) {
	// DEBUGLOGWIFI(__FUNCTION__);	DEBUGLOGWIFI("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		_wifiConfig.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOGWIFI("Arg %d: %s\r\n", i, request->arg(i).c_str());
			if (request->argName(i) == "devicename") {
				ESPHTTPServer._sysConfig.deviceName = urldecode(request->arg(i));
				_wifiConfig.dhcp = oldDHCP;
				continue;
			}
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

#if (USE_RESERV_WIFI > 0)
		if (_saveIn == 1) {save_configWifi(1);}
		if (_saveIn == 2) {save_configWifi(2);}
		if (_saveIn == 3) {save_configWifi(3);}
#endif
#if (NO_RST > 0)
		//yield();
		delay(1000);
		_fs->end();
		ESPHTTPServer.restart_esp();
#endif
	}
	else {
		DEBUGLOGWIFI(request->url().c_str());
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
	DEBUGLOGWIFI(__PRETTY_FUNCTION__);	DEBUGLOGWIFI("\r\n");
}
// wifi.html ^^^


//wifi.html vvv
void WIFIMOD_CLASS::webInit ()	{

	ESPHTTPServer.on("/wifi.html", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_html(request);
	});

	ESPHTTPServer.on("/wifi/info", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_info_values_html(request);
	});


	ESPHTTPServer.on("/wifi/values/0", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_values_html(request, 0);
	});
	ESPHTTPServer.on("/wifi/values/1", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };

		this->send_network_configuration_values_html(request, 1);
	});
	ESPHTTPServer.on("/wifi/values/2", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };

		this->send_network_configuration_values_html(request, 2);
	});
	ESPHTTPServer.on("/wifi/values/3", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };

		this->send_network_configuration_values_html(request, 3);
	});

	ESPHTTPServer.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
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
				//TODO
				#endif
				json += "}";
			}
			WiFi.scanDelete();
			if (WiFi.scanComplete() == WIFI_SCAN_FAILED) {
				WiFi.scanNetworks(true);
			}
		}
		json += "]";
		request->send(200, "text/json", json);
		json = "";
	});

	

}
//wifi.html ^^^

