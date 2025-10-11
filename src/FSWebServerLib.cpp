#include "main.h"
#include <ArduinoJson.h>

#include "FSWebServerLib.h"
#include "prog_isp.h"
#include "prog_swd.h"
#include "udphelper.h"
#include "programmer.h"
#include "debug.h"

#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>

#elif defined(ESP8266)
#include <FS.h>
#endif



AsyncFSWebServer ESPHTTPServer(80);


const char Page_ConfigRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring Wifi.
)=====";

const char Page_IndexRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.html">
Please Wait....Configuring and Restarting.
)=====";

const char Page_GeneralSys[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/system.html">
Please Wait....Configuring.
)=====";

const char Page_GeneralUdp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/udp.html">
Please Wait....Configuring.
)=====";

const char Page_GeneralNtp[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/ntp.html">
Please Wait....Configuring.
)=====";

const char Page_GeneralPrj[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/project.html">
Please Wait....Configuring.
)=====";

const char Page_AvrRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/avr.html">
Please Wait....Configuring.
)=====";

String _Version_App 		= VERSION_APP;
String _Version_Web 		= VERSION_WEB;
String _Version_BuildDate 	= APP_BUILDDATE;
String _Version_BuildTime 	= APP_BUILDTIME;

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}

void AsyncFSWebServer::s_secondTick(void* arg) {
	AsyncFSWebServer* self = reinterpret_cast<AsyncFSWebServer*>(arg);
	if (self->_evs.count() > 0) {	self->sendTimeData();	}
//Check connection timeout if enabled
#if (AP_ENABLE_TIMEOUT > 0)
	// DBG_OUTPUT_PORT.printf("timer%d\r\n", ++self->connectionTimout);
	if ((self->wifiStatus == FS_STAT_CONNECTING) )	{
		if (++self->connectionTimout >= AP_ENABLE_TIMEOUT){
			DBG_OUTPUT_PORT.printf("Connection Timeout, switching to AP Mode.\r\n");
			self->WifiScan = WF_SCAN_NO_NEED;
			self->configureWifiAP();
		}
	}
	if (self->WifiScan == WF_STAT_SCANED)	{
		self->configureWifi();
		self->WifiScan = WF_SCAN_NO_NEED;
	}

	if (self->WifiScan != WF_SCAN_NO_NEED) {	self->load_configWifi(self->scanWifi());	}

#endif //AP_ENABLE_TIMEOUT

}



void AsyncFSWebServer::sendTimeData() {
	DEBUGLOG("sendTimeData %s\r\n", NTP.getTimeDateString().c_str());
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

String formatBytes(size_t bytes) {
	if (bytes < 1024) {		return String(bytes) + "B";	}
	else
		if (bytes < (1024 * 1024))			{	return String(bytes / 1024.0) + "KB";	}
	else
		if (bytes < (1024 * 1024 * 1024))	{	return String(bytes / 1024.0 / 1024.0) + "MB";	}
	else	{	return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";	}
}

void flashLED(int pin, int times, int delayTime) {
	int oldState = digitalRead(pin);
	DEBUGLOG("---Flash LED during %d ms %d times. Old state = %d\r\n", delayTime, times, oldState);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LOW); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, HIGH); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
}


void AsyncFSWebServer::ntpHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);
    if ( _ntpevent == timeSyncd) 		{ DEBUGLOG("\t NTP_timeSyncd\r\n"); 	}
	if ( _ntpevent == noResponse) 		{ DEBUGLOG("\t NTP_noResponse \r\n"); 	}
	if ( _ntpevent == invalidAddress) 	{ DEBUGLOG("\t NTP_invalidAddress\r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGLOG("\t NTP_requestSent\r\n"); 	}
	if ( _ntpevent == errorSending) 	{ DEBUGLOG("\t NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGLOG("\t NTP_responseError \r\n"); 	}
	if (WiFi.status() != WL_CONNECTED) {return;}
	if (_ntpevent == noResponse || _ntpevent == invalidAddress ||  _ntpevent == responseError ) {
		ntpBeginReserv();
	}
}

void AsyncFSWebServer::ntpBeginReserv (){
	if  (_ntpserveer == 0) 	NTP.begin(_ntpConfig.ntpServerName0, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	if  (_ntpserveer == 1)  NTP.begin(_ntpConfig.ntpServerName1, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	if  (_ntpserveer == 2)  NTP.begin(_ntpConfig.ntpServerName2, _ntpConfig.timezone / 10, _ntpConfig.daylight);
	_ntpserveer++;
	if (_ntpserveer > 2) _ntpserveer = 2;
}

void AsyncFSWebServer::ntpBegin (){
	if (_ntpConfig.updateNTPTimeEvery > 0) { // Enable NTP sync
        NTP.setInterval (_ntpConfig.updateNTPTimeEvery * MIN);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent([this](NTPSyncEvent_t event){	ntpHandler(event);	});
		ntpBeginReserv();
		NTP.getTime();
	}
}

#if defined(ESP32)
    void AsyncFSWebServer::begin(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void AsyncFSWebServer::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
	avrprog.setFs(&SPIFFS); // init FS
	espProgrammer.setFs(&SPIFFS); // init FS
	swdprog.setFs(&SPIFFS);
	connectionTimout = 0;
	_ntpserveer = 0;
	DBG_OUTPUT_PORT.begin(115200);
	DBG_OUTPUT_PORT.print("\n\n");
#ifndef RELEASE
	DBG_OUTPUT_PORT.setDebugOutput(true);
#endif // RELEASE

// CONNECTION_LED pin defined as output
	if (CONNECTION_LED >= 0) {	pinMode(CONNECTION_LED, OUTPUT);	}
	// If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
	if (AP_ENABLE_BUTTON >= 0) {	pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP); 	}

	if (AP_ENABLE_BUTTON >= 0) {
		_apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", _apConfig.APenable);
	}

	// Turn LED off
	if (CONNECTION_LED >= 0) {		digitalWrite(CONNECTION_LED, HIGH);	}
    if (!_fs) { _fs->begin();  }// If SPIFFS is not started
#ifndef RELEASE
	{ // List files
#if defined(ESP32)
		File dir = _fs->open("/");

#elif defined(ESP8266)
		Dir dir = _fs->openDir("/");
		while (dir.next()) {
			String fileName = dir.fileName();
			size_t fileSize = dir.fileSize();
			DEBUGLOG("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
		}
		DEBUGLOG("\n");
#endif
	}
#endif // RELEASE
	loadHTTPAuth();

	if (!load_config_Sys()) { defaultConfigSys();  	}
	if (!load_config_NTP()) { defaultConfigNTP();  	}
	if (!load_config_UDP()) { defaultConfigUDP();  	}
	if (!load_config_metar()) { default_config_metar();  	}
#if (USE_RESERV_WIFI > 0)
	if (!load_configWifi(3)) { defaultConfigWifi(3); _apConfig.APenable = true; 	}
	if (!load_configWifi(2)) { defaultConfigWifi(2); _apConfig.APenable = true; 	}
	if (!load_configWifi(1)) { defaultConfigWifi(1); _apConfig.APenable = true; 	}
#endif
// Try to load configuration from file system// Load defaults if any error
	if (!load_configWifi(0)) { defaultConfigWifi(0); _apConfig.APenable = true;		}
	DEBUGLOG("_strWifis[0] %s\r\n", _strWifi0);
	DEBUGLOG("_strWifis[1] %s\r\n", _strWifi1);
	DEBUGLOG("_strWifis[2] %s\r\n", _strWifi2);
	DEBUGLOG("_strWifis[3] %s\r\n", _strWifi3);

	// NTP client setup
	if (_ntpConfig.updateNTPTimeEvery > 0) { // Enable NTP sync
        NTP.setInterval (_ntpConfig.updateNTPTimeEvery * 60);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent([this](NTPSyncEvent_t event){	ntpHandler(event);	});
		ntpBeginReserv();
		NTP.getTime();
	}
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
	//WIFI INIT start here
	String hostName = _sysConfig.deviceName + "_" + _sysConfig.deviceSerial;
	WiFi.hostname(hostName.c_str());
	if (AP_ENABLE_BUTTON >= 0) {
		// Set AP mode if AP button was pressed
		if (_apConfig.APenable) {	configureWifiAP();	}
		// Set WiFi config
		else {	configureWifi();	}
	}
	// Set WiFi config
	else {	configureWifi();	}
	DEBUGLOG("Open http://");
	DEBUGLOG(hostName.c_str());
	DEBUGLOG(".local to see the device web page.\r\n");
	DEBUGLOG("Device serial number:");	DEBUGLOG(_sysConfig.deviceSerial.c_str());	DEBUGLOG("\n\r");
	if (!_sysConfig.deviceType.isEmpty()) {
		DEBUGLOG("Device type: ");			DEBUGLOG(_sysConfig.deviceType.c_str());	DEBUGLOG("\n\r");
	}
#if defined(ESP32)
	DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipSize());
#endif
#if ESP8266
	DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
#endif
	DEBUGLOG("Scketch size: %u\r\n", 		ESP.getSketchSize());
	DEBUGLOG("Free flash space: %u\r\n", 	ESP.getFreeSketchSpace());

	_secondTk.attach(1.0f, &AsyncFSWebServer::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second

	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server
	String mdnsName = _sysConfig.deviceName + "_" + _sysConfig.deviceSerial;
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	prepareSizesForUpdate();
	ConfigureOTA(_httpAuth.wwwPassword.c_str());


	// ledInit();

	if (_sysConfig.deviceType ==  DEVTYPE_AVR){
		espProgrammer.prog_ProgTypeSet(DEVTYPE_AVR);
		DEBUGLOG("AVR Setup\n\r");
	}
	else if ( _sysConfig.deviceType == DEVTYPE_SWD){
		espProgrammer.prog_ProgTypeSet(DEVTYPE_SWD);
		DEBUGLOG("SWD Setup\n\r");
	}
	else if ( _sysConfig.deviceType == DEVTYPE_GPIO){
		DEBUGLOG("GPIO Setup\n\r");
	}
	espProgrammer.begin();

}

void AsyncFSWebServer::showDBG() {

}


//duplicate config stuff for user level config items

bool AsyncFSWebServer::load_configWifi(int _in) {
	if (_in < 0){		return false;	}
	char filename[FILENAME_LENGHT];
	sprintf(filename, "/%s%d.json", WIFI_CONFIG_FILE_NAME, _in);
	JsonDocument jsonDoc;
	if (!load_jsonDoc(filename, jsonDoc)){		return false;	}

	_wifiConfig.ssid = jsonDoc["ssid"].as<const char *>();
	if (_in == 0)  sprintf(_strWifi0, "%s", _wifiConfig.ssid.c_str());
	if (_in == 1)  sprintf(_strWifi1, "%s", _wifiConfig.ssid.c_str());
	if (_in == 2)  sprintf(_strWifi2, "%s", _wifiConfig.ssid.c_str());
	if (_in == 3)  sprintf(_strWifi3, "%s", _wifiConfig.ssid.c_str());

	_wifiConfig.password 	= jsonDoc["pass"].as<const char *>();
	_wifiConfig.ip 			= IPAddress(jsonDoc["ip"][0], jsonDoc["ip"][1], jsonDoc["ip"][2], jsonDoc["ip"][3]);
	_wifiConfig.netmask 	= IPAddress(jsonDoc["netmask"][0], jsonDoc["netmask"][1], jsonDoc["netmask"][2], jsonDoc["netmask"][3]);
	_wifiConfig.gateway 	= IPAddress(jsonDoc["gateway"][0], jsonDoc["gateway"][1], jsonDoc["gateway"][2], jsonDoc["gateway"][3]);
	_wifiConfig.dns 		= IPAddress(jsonDoc["dns"][0], jsonDoc["dns"][1], jsonDoc["dns"][2], jsonDoc["dns"][3]);
	_wifiConfig.dhcp 		= jsonDoc["dhcp"].as<bool>();

	// DEBUGLOG("Config %d wifi initialized. ", _in);
	// DEBUGLOG("SSID: %s ", 	_wifiConfig.ssid.c_str());
	// DEBUGLOG("PASS: %s\r\n", _wifiConfig.password.c_str());
	// DEBUGLOG(__PRETTY_FUNCTION__); DEBUGLOG("\r\n");

	return true;
}

bool AsyncFSWebServer::load_config_metar() {
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_FILE_METAR, jsonDoc)){	return false;	}
	_metarConfig.icao = jsonDoc["icao"].as<const char *>();
	return true;
}

bool AsyncFSWebServer::load_config_Sys() {
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_FILE_SYS, jsonDoc)){	return false;	}
	_sysConfig.deviceName 			= jsonDoc["deviceName"].as<const char *>();
	_sysConfig.deviceSerial 		= jsonDoc["deviceSerial"].as<const char *>();
	_sysConfig.deviceType 			= jsonDoc["deviceType"].as<const char *>();
	return true;
}


bool AsyncFSWebServer::load_config_UDP() {
	// DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_FILE_UDP, jsonDoc)){	return false;	}
#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif
	_udpConfig.udpPortTx 			= jsonDoc["udpPortTx"].as< int >();
	_udpConfig.udpPortRx 			= jsonDoc["udpPortRx"].as< int >();
	_udpConfig.udpTimeOut			= jsonDoc["udpTimeOut"].as< int >();
	_udpConfig.keyword				= jsonDoc["udpkeyword"].as<const char *>();
	DEBUGLOG("updPortTx: %d\r\n"	, _udpConfig.udpPortTx);
	DEBUGLOG("updPortRx: %d\r\n"	, _udpConfig.udpPortRx);
	DEBUGLOG("udpTimeOut: %d\r\n"	, _udpConfig.udpTimeOut);
	DEBUGLOG("keyword: %s\r\n"		, _udpConfig.keyword.c_str());
	return true;
}

bool AsyncFSWebServer::load_jsonDoc(const String& file,	JsonDocument& jsonDoc){
	File configFile = _fs->open(file, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUGLOG("Config file size is too large");
	configFile.close();
	return false;
	}*/
	char * buf = (char *) malloc(size);
	if ( buf == NULL){	return false;	}
	DEBUGLOGFH("File: %s, size: %d\r\n", file.c_str(), size);
	configFile.readBytes(buf, size);
	configFile.close();
	auto error = deserializeJson(jsonDoc, buf);
	free(buf);
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	// String temp;
	// serializeJsonPretty(jsonDoc, temp);
	// Serial.println(temp);
#endif
	return true;
}

bool AsyncFSWebServer::load_config_NTP() {
	// DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_FILE_NTP, jsonDoc)){		return false;	}
	_ntpConfig.ntpServerName0 		= jsonDoc["ntp0"].as<const char *>();
	_ntpConfig.ntpServerName1 		= jsonDoc["ntp1"].as<const char *>();
	_ntpConfig.ntpServerName2 		= jsonDoc["ntp2"].as<const char *>();
	_ntpConfig.updateNTPTimeEvery 	= jsonDoc["NTPperiod"].as<long>();
	_ntpConfig.timezone 			= jsonDoc["timeZone"].as<long>();
	_ntpConfig.daylight 			= jsonDoc["daylight"].as<long>();

	DEBUGLOG("NTP Server0: %s\r\n", _ntpConfig.ntpServerName0.c_str());
	DEBUGLOG("NTP Server1: %s\r\n", _ntpConfig.ntpServerName1.c_str());
	DEBUGLOG("NTP Server2: %s\r\n", _ntpConfig.ntpServerName2.c_str());
	return true;
}

void AsyncFSWebServer::default_config_metar() {
	_metarConfig.icao	=	"UWGG";
	save_config_metar();
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

// DEFAULT CONFIG SUSTEM
void AsyncFSWebServer::defaultConfigSys() {
	// DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	_sysConfig.deviceName 		= "esp_server";
	_sysConfig.deviceSerial 	= SERIAL_NUMBER;
	_sysConfig.deviceType 		= DEVTYPE_GPIO;
	//_sysConfig.connectionLed = CONNECTION_LED;
	save_configSys();
}

void AsyncFSWebServer::defaultConfigUDP() {
	// DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	// DEFAULT CONFIG UDP
	_udpConfig.udpPortTx = UDP_BROADCAST_PORT_DFLT;
	_udpConfig.udpPortRx = UDP_BROADCAST_PORT_DFLT+1;
	_udpConfig.udpTimeOut = UDP_BROADCAST_TIME_DFLT;
	_udpConfig.keyword = UDP_BROADCAST_KEYWORD_DFLT;
	save_configUDP();
}
void AsyncFSWebServer::defaultConfigNTP() {
	// DEFAULT CONFIG NTP
	_ntpConfig.ntpServerName0 = NTPSERVER_DFLT0;
	_ntpConfig.ntpServerName1 = NTPSERVER_DFLT1;
	_ntpConfig.ntpServerName2 = NTPSERVER_DFLT2;
	_ntpConfig.updateNTPTimeEvery = 15;
	_ntpConfig.timezone = 10;
	_ntpConfig.daylight = 1;
	save_configNTP();
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_jsonDoc(const JsonDocument& jsonDoc,	const String& file) {
	File configFile  = _fs->open(file, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open config file for writing\r\n");
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

bool AsyncFSWebServer::save_config_metar() {
	DEBUGLOG("Save config METAR\r\n");
	JsonDocument jsonDoc;
	jsonDoc["icao"] = _metarConfig.icao;
	return save_jsonDoc(jsonDoc, CONFIG_FILE_METAR);
}

bool AsyncFSWebServer::save_configSys() {
	DEBUGLOG("Save config SYSTEM\r\n");
	JsonDocument jsonDoc;
	jsonDoc["deviceName"] 	= _sysConfig.deviceName;
	jsonDoc["deviceSerial"] = _sysConfig.deviceSerial;
	jsonDoc["deviceType"] 	= _sysConfig.deviceType;
	return save_jsonDoc(jsonDoc, CONFIG_FILE_SYS);
}

bool AsyncFSWebServer::save_configNTP() {
	DEBUGLOG("Save config NTP \r\n");
	JsonDocument jsonDoc;
	jsonDoc["ntp0"] 		= _ntpConfig.ntpServerName0;
	jsonDoc["ntp1"] 		= _ntpConfig.ntpServerName1;
	jsonDoc["ntp2"] 		= _ntpConfig.ntpServerName2;
	jsonDoc["NTPperiod"] 	= _ntpConfig.updateNTPTimeEvery;
	jsonDoc["timeZone"] 	= _ntpConfig.timezone;
	jsonDoc["daylight"] 	= _ntpConfig.daylight;
	return save_jsonDoc(jsonDoc, CONFIG_FILE_NTP);
}

bool AsyncFSWebServer::save_configUDP() {
	DEBUGLOG("Save config UDP \r\n");
	JsonDocument jsonDoc;
	jsonDoc["udpPortTx"] 	= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 	= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 	= _udpConfig.udpTimeOut;
	jsonDoc["udpkeyword"] 	= _udpConfig.keyword;
	return save_jsonDoc(jsonDoc, CONFIG_FILE_UDP);
}


void AsyncFSWebServer::defaultConfigWifi(int _in) {
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
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_configWifi(int _in) {
	//flag_config = false;
	DEBUGLOG("Save config\r\n");
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
		DEBUGLOG("Failed to open config file for writing\r\n");
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

void AsyncFSWebServer::clearConfig(bool reset)	{
	if (_fs->exists(CONFIG_FILE_SYS)) 	{ _fs->remove(CONFIG_FILE_SYS);	}
	if (_fs->exists(CONFIG_FILE_UDP)) 	{ _fs->remove(CONFIG_FILE_UDP);	}
	if (_fs->exists(CONFIG_FILE_NTP)) 	{ _fs->remove(CONFIG_FILE_NTP);	}
	if (_fs->exists(WIFI_CONFIG_FILE0)) { _fs->remove(WIFI_CONFIG_FILE0);	}
#if (USE_RESERV_WIFI > 0)
	if (_fs->exists(WIFI_CONFIG_FILE1)) { _fs->remove(WIFI_CONFIG_FILE1);	}
	if (_fs->exists(WIFI_CONFIG_FILE2)) { _fs->remove(WIFI_CONFIG_FILE2);	}
	if (_fs->exists(WIFI_CONFIG_FILE3)) { _fs->remove(WIFI_CONFIG_FILE3);	}
#endif
	if (_fs->exists(SECRET_FILE)) {		_fs->remove(SECRET_FILE);	}
	if (reset) {
		if (_fs) { _fs->end();  }// If SPIFFS is started - finish it.
		restart_esp();
	}
}


bool AsyncFSWebServer::load_user_config(String name, String &value) {
	File configFile = _fs->open(USER_CONFIG_FILE, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUGLOG("Config file size is too large");
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	//DEBUGLOG("496 JSON file size: %d bytes\r\n", size);
	JsonDocument jsonDoc;
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif

	value = jsonDoc[name].as<const char*>();

	DEBUGLOG("User data initialized.\r\n");
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	return true;
}

bool AsyncFSWebServer::save_user_config(String name, String value) {
	//add logic to test and create if non
	DEBUGLOG(name.c_str());		DEBUGLOG("\r\n");
	DEBUGLOG(value.c_str());	DEBUGLOG("\r\n");

	File configFile;
	if (!_fs->exists(USER_CONFIG_FILE))		{
		configFile = _fs->open(USER_CONFIG_FILE, "w");
		if (!configFile) {
			DEBUGLOG("Failed to open config file for writing\r\n");
			configFile.close();
			return false;
		}
		//create blank json file
		DEBUGLOG("Creating user config file for writing\r\n");
		configFile.print("{}");
		configFile.close();
	}
	//get existing json file
	configFile = _fs->open(USER_CONFIG_FILE, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open config file");
		return false;
	}
	size_t size = configFile.size();
	/*if (size > 1024) {
	DEBUGLOG("Config file size is too large");
	configFile.close();
	return false;
	}*/

	// Allocate a buffer to store contents of the file.
	std::unique_ptr<char[]> buf(new char[size]);

	// We don't use String here because ArduinoJson library requires the input
	// buffer to be mutable. If you don't use ArduinoJson, you may as well
	// use configFile.readString instead.
	configFile.readBytes(buf.get(), size);
	configFile.close();
	DEBUGLOG("Read JSON file size: %d bytes\r\n", size);
	JsonDocument jsonDoc;
	auto error = deserializeJson(jsonDoc, buf.get());

	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}
	else
	{
		DEBUGLOG("Parse User config file\r\n");
	}

	jsonDoc[name] = value;

	configFile = _fs->open(USER_CONFIG_FILE, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}

#ifndef RELEASE
	DEBUGLOG("Save user config \r\n");
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::clearUserConfig(bool reset) 	{
	if (_fs->exists(USER_CONFIG_FILE)) { _fs->remove(USER_CONFIG_FILE);	}

	if (reset) {
		if (_fs) { _fs->end();  }// If SPIFFS is started - finish it.
		restart_esp();
	}
}

bool AsyncFSWebServer::load_user_config(String name, int &value)	{
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = sTemp.toInt();
	return bTemp;
}

bool AsyncFSWebServer::save_user_config(String name, int value) {
	return AsyncFSWebServer::save_user_config(name, String(value));
}

bool AsyncFSWebServer::load_user_config(String name, float &value) {
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = sTemp.toFloat();
	return bTemp;
}

bool AsyncFSWebServer::save_user_config(String name, float value) {
	return AsyncFSWebServer::save_user_config(name, String(value, 8));
}

bool AsyncFSWebServer::load_user_config(String name, long &value) {
	String sTemp = "";
	bool bTemp = load_user_config(name, sTemp);
	value = atol(sTemp.c_str());
	return bTemp;
}

bool AsyncFSWebServer::save_user_config(String name, long value) {
	return AsyncFSWebServer::save_user_config(name, String(value));
}

bool AsyncFSWebServer::loadHTTPAuth() {
	JsonDocument jsonDoc;
	if (!load_jsonDoc(SECRET_FILE, jsonDoc)){
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		DEBUGLOG("Huh");
		return false;
	}
	_httpAuth.auth = jsonDoc["auth"];
	_httpAuth.wwwUsername = jsonDoc["user"].as<String>();
	_httpAuth.wwwPassword = jsonDoc["pass"].as<String>();

	DEBUGLOG(_httpAuth.auth ? "Secret initialized.\r\n" : "Auth disabled.\r\n");
	if (_httpAuth.auth) {
		DEBUGLOG("User: %s\r\n", _httpAuth.wwwUsername.c_str());
		DEBUGLOG("Pass: %s\r\n", _httpAuth.wwwPassword.c_str());
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	return true;
}

void AsyncFSWebServer::handle() {
	ArduinoOTA.handle();
	if (updateTimeFromNTP) {
		ntpBeginReserv();
		// NTP.begin(_sysConfig.ntpServerName0, _sysConfig.timezone / 10, _sysConfig.daylight);
		NTP.setInterval(15, _ntpConfig.updateNTPTimeEvery * 60);
		Serial.println(NTP.getLastNTPSync());
		updateTimeFromNTP = false;
	}
}

void AsyncFSWebServer::configureWifiAP() {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");

	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	WiFi.mode(WIFI_AP);
	wifiStatus = FS_STAT_APMODE;

	// need only when we online at last
	load_config_Sys();
	load_config_UDP();
	load_config_NTP();

	String APname = _sysConfig.deviceName + "_" + _sysConfig.deviceSerial;
	if (_httpAuth.auth) {
		WiFi.softAP(APname, _httpAuth.wwwPassword);
		DEBUGLOG("AP Pass enabled: %s \r\n", _httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOG("AP Pass disabled \r\n");
	}
	if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 3, 250);	}
	DBG_OUTPUT_PORT.printf("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	connectionTimout = 0;
}

int AsyncFSWebServer::scanWifi() {
	int _scanNum = -1;
	int y = 0;

	int nets = WiFi.scanComplete();
	if (nets == WIFI_SCAN_FAILED) {	WiFi.scanNetworks(true);	}
	if (nets > 0) {
		for (int i = 0; i < nets; ++i) {
			if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 3; y=i;}
			if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 2; y=i;}
			if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 1; y=i;}
			if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 0; y=i;}
		}
		WiFi.scanDelete();
	}
	if (_scanNum >= 0) {	WifiScan = WF_STAT_SCANED;	}

	DEBUGLOG("timeout: %d _scanNum = %d nets = %d \r\n", (AP_ENABLE_TIMEOUT - connectionTimout), _scanNum, nets);
	return _scanNum;
}


void AsyncFSWebServer::configureWifi() { // set esp8266 as wifi client
	if (wifiStatus == FS_STAT_APMODE) {return;}
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
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

void AsyncFSWebServer::ConfigureOTA(String password) {
	//DONE new hostname Sam Arcanum
	String hostName = _sysConfig.deviceName+"_"+_sysConfig.deviceSerial;
	ArduinoOTA.setHostname(hostName.c_str());
	// No authentication by default
	if (password != "") {
		ArduinoOTA.setPassword(password.c_str());
		DEBUGLOG("OTA password set %s\n", password.c_str());
	}

#ifndef RELEASE
	ArduinoOTA.onStart([]() {
		DEBUGLOG("\r\n ArduinoOTA start. \r\n");
	});

#if defined(ESP32)
	ArduinoOTA.onEnd(std::bind([](fs::SPIFFSFS* fs)
#elif defined(ESP8266)
	ArduinoOTA.onEnd(std::bind([](FS* fs)
#endif
	{
		fs->end();
		DEBUGLOG("\r\n ArduinoOTA end. \r\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUGLOG("\t OTA Progress: %u%% \r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUGLOG("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) 			{DEBUGLOG("Auth Failed\r\n");		}
		else if (error == OTA_BEGIN_ERROR) 		{DEBUGLOG("Begin Failed\r\n");		}
		else if (error == OTA_CONNECT_ERROR)	{DEBUGLOG("Connect Failed\r\n");	}
		else if (error == OTA_RECEIVE_ERROR) 	{DEBUGLOG("Receive Failed\r\n");	}
		else if (error == OTA_END_ERROR) 		{DEBUGLOG("End Failed\r\n");		}
	});
	DEBUGLOG("\r\n ArduinoOTA Ready \r\n");
#endif // RELEASE
	ArduinoOTA.begin();
}

#if defined(ESP32)
void AsyncFSWebServer::onWiFiConnected( )	{
#elif ESP8266
void AsyncFSWebServer::onWiFiConnected(WiFiEventStationModeConnected data) {
#endif

	DBG_OUTPUT_PORT.println("WiFi Connected: Waiting for DHCP");
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
		//turnLedOn();
		DEBUGLOG("Led %d on\n", CONNECTION_LED);
	}
	wifiDisconnectedSince = 0;

}

#if defined(ESP32)
void AsyncFSWebServer::onWiFiConnectedGotIP( ) {
#elif defined(ESP8266)
void AsyncFSWebServer::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
#endif
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW);
		 // Turn LED on
		 //turnLedOn();
	}

	DBG_OUTPUT_PORT.printf("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	DBG_OUTPUT_PORT.printf("Led %d on\n", CONNECTION_LED);
	wifiDisconnectedSince = 0;
	//force NTPsstart after got ip
	if (_ntpConfig.updateNTPTimeEvery > 0) {	updateTimeFromNTP = true;	}		// Enable NTP sync

	connectionTimout = 0;
	_ntpserveer = 0;
	wifiStatus = FS_STAT_CONNECTED;

	//udp broadcast - we are online!
    udpBroadcast.udpBroadcastSend(getUpdPortTx(), udpJsonBroadcast());
	//udp start to listen
	udpBroadcast.udpStart(getUpdPortRx());
	//ntpBegin();

}

#if defined(ESP32)
void AsyncFSWebServer::onWiFiDisconnected( ) {
#elif defined(ESP8266)
void AsyncFSWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
#endif
	udpBroadcast.udpStop();
	if (wifiStatus == FS_STAT_RESET) {return;}

	DEBUGLOG(" case STA_DISCONNECTED \r\n");
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH);
		// flashLED(config.connectionLed, 2, 100);
	} // Turn LED off
	// FIXME
	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUGLOG("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = WF_STAT_SCANING;

}


// working with pages vvv

void AsyncFSWebServer::handleFileList(AsyncWebServerRequest *request) {
	if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }
	String path = request->arg("dir");
	DEBUGLOGFH("handleFileList: %s\r\n", path.c_str());
	String output = "[";

#ifdef ESP32
	File root =  _fs->open(path);
	File file = root.openNextFile();
	while (file) {
		if (output != "[")	{output += ',';}
		bool isDir = false;
		output += "{\"type\":\"";
		isDir = file.isDirectory();
		output += (isDir) ? "dir" : "file";
		output += "\",\"name\":\"";
		output += String(file.name());
		output += "\"}";
		file = root.openNextFile();
	}
	#else
	Dir dir = _fs->openDir(path);
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (true)//entry.name()!="secret.json") // Do not show secrets
		{
			if (output != "[")	{output += ',';}
			bool isDir = false;
			output += "{\"type\":\"";
			output += (isDir) ? "dir" : "file";
			output += "\",\"name\":\"";
			output += String(entry.name()).substring(1);
			output += "\"}";
		}
		entry.close();
		}
#endif

	output += "]";
	DEBUGLOGFH("%s\r\n", output.c_str());
	request->send(200, "text/json", output);
}



String getContentType(String filename, AsyncWebServerRequest *request) {
	if (request->hasArg("download")) return "application/octet-stream";
	else if (filename.endsWith(".htm")) return "text/html";
	else if (filename.endsWith(".html")) return "text/html";
	else if (filename.endsWith(".css")) return "text/css";
	else if (filename.endsWith(".js"))   return "application/javascript";
	else if (filename.endsWith(".json")) return "application/json";
	else if (filename.endsWith(".png")) return "image/png";
	else if (filename.endsWith(".gif")) return "image/gif";
	else if (filename.endsWith(".jpg")) return "image/jpeg";
	else if (filename.endsWith(".ico")) return "image/x-icon";
	else if (filename.endsWith(".xml")) return "text/xml";
	else if (filename.endsWith(".pdf")) return "application/x-pdf";
	else if (filename.endsWith(".zip")) return "application/x-zip";
	else if (filename.endsWith(".gz"))  return "application/x-gzip";
	else if (filename.endsWith(".hex")) return "text/html";
	return "text/plain";
}

bool AsyncFSWebServer::handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUGLOGFH("handleFileRead: %s\r\n", path.c_str());
	if (CONNECTION_LED >= 0) {
		// CANNOT RUN DELAY() INSIDE CALLBACK
		flashLED(CONNECTION_LED, 1, 25); // Show activity on LED
	}
	if (path.endsWith("/")) {	path += HTML_INDEX;	}
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) { path += ".gz"; }
		DEBUGLOGFH("Content type: %s\r\n", contentType.c_str());
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(".gz"))
			response->addHeader("Content-Encoding", "gzip");
		//File file = SPIFFS.open(path, "r");
		DEBUGLOGFH("File %s exist\r\n", path.c_str());
		request->send(response);
		DEBUGLOGFH("File %s Sent\r\n", path.c_str());

		return true;
	}
	else
		DEBUGLOGFH("Cannot find %s\n", path.c_str());
	return false;
}

void AsyncFSWebServer::handleFileCreate(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0)
		return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0U);
	DEBUGLOG("handleFileCreate: %s\r\n", path.c_str());
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (_fs->exists(path))
		return request->send(500, "text/plain", "FILE EXISTS");
	File file = _fs->open(path, "w");
	if (file)
		file.close();
	else
		return request->send(500, "text/plain", "CREATE FAILED");
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}


// удаление файла

void AsyncFSWebServer::handleFileDelete(AsyncWebServerRequest *request) {
	if (!checkAuth(request))	{	return request->requestAuthentication();	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = request->arg(0U);
	DEBUGLOG("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/")		{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!_fs->exists(path)) {	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
	request->send(200, "text/plain", "");
}

void AsyncFSWebServer::handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	static File fsUploadFile;
	static size_t fileSize = 0;

	if (!index) { // Start
		DEBUGLOG("handleFileUpload Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) filename = "/" + filename;
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOG("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOG("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) != len) {	DBG_OUTPUT_PORT.println("Write error during upload");	}
		else {	fileSize += len;	}
	}
	//da fack?!
	/*for (size_t i = 0; i < len; i++) {
	if (fsUploadFile)
	fsUploadFile.write(data[i]);
	}*/
	if (final) { // End
		if (fsUploadFile) {	fsUploadFile.close();	}
		DEBUGLOG("handleFileUpload Size: %u\n", fileSize);
		fileSize = 0;
	}
}

// wifi.html vvv
void AsyncFSWebServer::send_network_configuration_values_html(AsyncWebServerRequest *request, int _index) {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
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

void AsyncFSWebServer::send_connection_state_values_html(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String state = "N/A";
	String Networks = "";
	if (WiFi.status() == 0) state = "Idle";
	else if (WiFi.status() == 1) state = "NO SSID AVAILBLE";
	else if (WiFi.status() == 2) state = "SCAN COMPLETED";
	else if (WiFi.status() == 3) state = "CONNECTED";
	else if (WiFi.status() == 4) state = "CONNECT FAILED";
	else if (WiFi.status() == 5) state = "CONNECTION LOST";
	else if (WiFi.status() == 6) state = "DISCONNECTED";

	WiFi.scanNetworks(true);

	String values = "";
	values += "connectionstate|" + state + "|div\n";
	//values += "networks|Scanning networks ...|div\n";
	request->send(200, "text/plain", values);
	state = "";
	values = "";
	Networks = "";

}


void AsyncFSWebServer::send_information_values_html(AsyncWebServerRequest *request) {
	String values = "";
	values += "x_ssid|" + (String)WiFi.SSID() + "|div\n";
	values += "x_ip|" + (String)WiFi.localIP()[0] + "." + (String)WiFi.localIP()[1] + "." + (String)WiFi.localIP()[2] + "." + (String)WiFi.localIP()[3] + "|div\n";
	values += "x_gateway|" + (String)WiFi.gatewayIP()[0] + "." + (String)WiFi.gatewayIP()[1] + "." + (String)WiFi.gatewayIP()[2] + "." + (String)WiFi.gatewayIP()[3] + "|div\n";
	values += "x_netmask|" + (String)WiFi.subnetMask()[0] + "." + (String)WiFi.subnetMask()[1] + "." + (String)WiFi.subnetMask()[2] + "." + (String)WiFi.subnetMask()[3] + "|div\n";
	values += "x_mac|" + getMacAddress() + "|div\n";
	values += "x_dns|" + (String)WiFi.dnsIP()[0] + "." + (String)WiFi.dnsIP()[1] + "." + (String)WiFi.dnsIP()[2] + "." + (String)WiFi.dnsIP()[3] + "|div\n";
	values += "x_ntp_sync|" + (String)NTP.getTimeDateString(NTP.getLastNTPSync()) + "|div\n";
	values += "x_ntp_time|" + (String)NTP.getTimeStr() + "|div\n";
	values += "x_ntp_date|" + (String)NTP.getDateStr() + "|div\n";
	values += "x_ntp_adr|" + (String)NTP.getNtpServerName() + "|div\n";
	values += "x_uptime|" + (String)NTP.getUptimeString() + "|div\n";
	values += "x_last_boot|" + NTP.getTimeDateString(NTP.getLastBootTime()) + "|div\n";
	#ifdef ESP32
	values += "x_chipid|" + (String)ESP.getChipModel() + "|div\n";
	#elif defined(ESP8266)
	values += "x_chipid|" + (String)ESP.getChipId() + "|div\n";
	#endif
	values += "x_sdk|" + (String)ESP.getSdkVersion() + "|div\n";
	values += "x_mhz|" + (String)ESP.getCpuFreqMHz() + "|div\n";

	request->send(200, "text/plain", values);
	//delete &values;
	values = "";
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");

}

String AsyncFSWebServer::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}



void AsyncFSWebServer::send_scanwifi(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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

void AsyncFSWebServer::send_network_configuration_html(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		_wifiConfig.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
			if (request->argName(i) == "devicename") {
				_sysConfig.deviceName = urldecode(request->arg(i));
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
		delay(1000);\
		_fs->end();
		restart_esp();
#endif
	}
	else {
		DEBUGLOG(request->url().c_str());
		handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

// wifi.html ^^^

void AsyncFSWebServer::restart_esp() {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	wifiStatus = FS_STAT_RESET;
	WiFi.disconnect(true, false);
	_fs->end();
	delay(1000);
	ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	String values = "";
	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";

	request->send(200, "text/plain", values);

	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	DEBUGLOG("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0) { // Save Settings
		_httpAuth.auth = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
				DEBUGLOG("User: %s\n", _httpAuth.wwwUsername.c_str());
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
				DEBUGLOG("Pass: %s\n", _httpAuth.wwwPassword.c_str());
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
				DEBUGLOG("HTTP Auth enabled\r\n");
				continue;
			}
		}
		saveHTTPAuth();
	}
	handleFileRead("/system.html", request);
}

bool AsyncFSWebServer::saveHTTPAuth() {
	//flag_config = false;
	DEBUGLOG("Save secret\r\n");
	JsonDocument jsonDoc;

	jsonDoc["auth"] = _httpAuth.auth;
	jsonDoc["user"] = _httpAuth.wwwUsername;
	jsonDoc["pass"] = _httpAuth.wwwPassword;

	//TODO add AP data to html Sam Arcanum
	File configFile = _fs->open(SECRET_FILE, "w");
	if (!configFile) {
		DEBUGLOG("Failed to open secret file for writing\r\n");
		configFile.close();
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp.c_str());
#endif // RELEASE
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::prepareSizesForUpdate (){
	maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	freeSketchSpace  = ESP.getFreeSketchSpace();
	DEBUGLOG("Update prepare size \n\r");
}

void AsyncFSWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	String values = "";
	String updateOKstr = "";
	String updateFiletype = "";
	typeOTAfile = UNSUPPORTED;

	if (_updateFileName == OTA_FILENAME_FIRMWARE) {
		typeOTAfile = FIRMWARE;
		updateFiletype = OTA_FIRMWARE;
	}
	if (_updateFileName == OTA_FILENAME_FILESYSTEM) {
		typeOTAfile = FILESYSTEM;
		updateFiletype = OTA_FILESYSTEM;
	}
	if (typeOTAfile == UNSUPPORTED) {
		updateFiletype = OTA_UNSUPPORTED;
	}

	bool updateOK = maxSketchSpace < freeSketchSpace;
	if (updateOK == true) {
		updateOKstr = "OK" ;
	} else {
		updateOKstr = "ERROR" ;
	}

	DEBUGLOG("--updateOK: %s\r\n", updateOKstr);
	DEBUGLOG("--FreeSketchSpace: %d\r\n", freeSketchSpace);
	DEBUGLOG("--MaxSketchSpace: %d\r\n", maxSketchSpace);
	DEBUGLOG("--UpdateFiletype: %d\r\n", updateFiletype);


	values += "upd|" 			+ updateOKstr 				+ "|div\n";
	values += "updSizeFree|" 	+ (String)freeSketchSpace 	+ "|div\n";
	values += "updSizeMax|" 	+ (String)maxSketchSpace  	+ "|div\n";
	values += "updFileType|" 	+ updateFiletype		  	+ "|div\n";
	request->send(200, "text/plain", values);
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	_browserFileMD5 = "";
	DEBUGLOG("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  {// Read hash
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserFileMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserFileMD5.c_str());
				continue;
			}
			if (request->argName(i) == "size") {
				_updateFileSize = request->arg(i).toInt();
				DEBUGLOG("Update size: %d \r\n", _updateFileSize);
				continue;
			}
			if (request->argName(i) == "name") {
				_updateFileName = request->arg(i).c_str();
				DEBUGLOG("Update filename: %s \r\n", _updateFileName.c_str());
				continue;
			}
		}

		request->send(200, "text/html", "OK --> MD5: " + _browserFileMD5);
	}

}
void AsyncFSWebServer::updateFileExecute (AsyncWebServerRequest *request) {
	AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
	response->addHeader("Connection", "close");
	response->addHeader("Access-Control-Allow-Origin", "*");
	request->send(response);
	if (this->_fs) { this->_fs->end(); }//this->_fs->end();
	this->restart_esp();

}

void AsyncFSWebServer::uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	String	values = "";
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	static long totalSize = 0;
	int updatePartition = 1;
	if (!index) { //UPLOAD_FILE_START
		if (_fs) { _fs->end(); }//SPIFFS.end();
		//Update.runAsync(true);
		uint32_t maxSketchSpace = ESP.getSketchSize();
		DBG_OUTPUT_PORT.printf("Update start: %s\r\n", filename.c_str());
		DBG_OUTPUT_PORT.printf("Max free scketch space: %u\r\n", maxSketchSpace);
		DBG_OUTPUT_PORT.printf("New scketch size: %u\r\n", _updateFileSize);
		if (_browserFileMD5 != NULL && _browserFileMD5 != "") {
			Update.setMD5(_browserFileMD5.c_str());
			DBG_OUTPUT_PORT.printf("Hash from btowser: %s\r\n", _browserFileMD5.c_str());
		} else {
			values += "OTA Update error no md4 hash!" ;
			request->send(500, "text/plain", values);
			return ;
		}
		#if defined(ESP32)
		if (typeOTAfile == FILESYSTEM) 	{ updatePartition = U_SPIFFS; }
		#elif defined(ESP8266)
		if (typeOTAfile == FILESYSTEM) 	{ updatePartition = U_FS; }
		#endif

		if (typeOTAfile == FIRMWARE) 	{ updatePartition = U_FLASH; }

		if (!Update.begin(_updateFileSize, updatePartition)) {	//start with max available size
			Update.printError(DBG_OUTPUT_PORT);
			Update.end();
			values += "OTA Update error at begin" ;
			request->send(500, "text/plain", values);
			return ;
		}
		if (typeOTAfile == UNSUPPORTED || updatePartition == 1) {
			values += "OTA Update error UNSUPPORTED file!" ;
			request->send(500, "text/plain", values);
			return ;
		}
	}
	// Get upload file, continue if not start
	totalSize += len;
	//percernt formula
	uint16_t percentLoaded = (totalSize * 100) /  _updateFileSize ;
	if (  (percentLoaded % 5) == 0  && (percentLoaded != percentLoadedPrev)) {
		percentLoadedPrev = percentLoaded;
		DBG_OUTPUT_PORT.printf("Uploaded: %d bytes  %u %%\r\n", totalSize, percentLoaded);
	}

	size_t written = Update.write(data, len);
	if (written != len) {
		values += "OTA Update error data load! len = " + (String)len + "written = "+ (String)written + "totalSize ="+ (String)totalSize +" \r\n";
		DBG_OUTPUT_PORT.printf(values.c_str());
		request->send(500, "text/plain", values);
		return ;
	}
	if (final) {  // UPLOAD_FILE_END
		String updateHash;
		DBG_OUTPUT_PORT.println("Applying update...");
		if (Update.end(true)) { //true to set the size to the current progress
			updateHash = Update.md5String();
			DBG_OUTPUT_PORT.printf("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
			DBG_OUTPUT_PORT.printf("Update Success: %u\nRebooting...\r\n", request->contentLength());
		} else {
			updateHash = Update.md5String();
			DBG_OUTPUT_PORT.printf("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
			Update.printError(DBG_OUTPUT_PORT);
		}
	}

	//delay(2);
}

void AsyncFSWebServer::handle_rest_config(AsyncWebServerRequest *request) {
	String values = "";
	// handle generic rest call
	//dirty processing as no split function
	int p = 0; //string ptr
	int t = 0; // temp string pointer
	String URL = request->url().substring(9);
	String name = "";
	String data = "";
	String type = "";

	while (p < URL.length())	{
		t = URL.indexOf("/", p);
		if (t >= 0)		{
			name = URL.substring(p, t);
			p = t + 1;
		}	else	{
			name = URL.substring(p);
			p = URL.length();
		}
		if (name.substring(1, 2) == "_")	{
			type = name.substring(0, 2);
			if (type == "i_")	{	type = "input";	}
			else	if (type == "d_")	{	type = "div";	}
			else	if (type == "c_")	{	type = "chk";	}
			name = name.substring(2);
		}
		else	{	type = "input";		}

		load_user_config(name, data);
		values += name + "|" + data + "|" + type + "\n";
	}
	request->send(200, "text/plain", values);

	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");

}


void AsyncFSWebServer::post_rest_config(AsyncWebServerRequest *request) {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	String target = "/";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
		DEBUGLOG(request->argName(i).c_str());
		DEBUGLOG(" : ");
		DEBUGLOG(urldecode(request->arg(i)).c_str());
		//check for post redirect
		if (request->argName(i) == "afterpost")		{	target = urldecode(request->arg(i));	}
		//or savedata in Json File
		else {	save_user_config(request->argName(i), request->arg(i));	}
	}
	request->redirect(target);

}


// sam arcanum web pages functions VVV

// *.html vvv
void AsyncFSWebServer::send_system_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "devicename|"  	+ _sysConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" 	+ _sysConfig.deviceSerial 		+ "|div\n";
	values += "devicetype|" 	+ _sysConfig.deviceType 		+ "|div\n";
	values += "versionapp|" 	+ _Version_App + "|div\n";
	values += "versionweb|" 	+ _Version_Web + "|div\n";
	values += "versiondatetime|" + _Version_BuildDate + " " + _Version_BuildTime + "|div\n";
	request->send(200, "text/plain", values);
}
// *.html ^^^

// ntp.html vvv
void AsyncFSWebServer::send_NTP_configuration_html(AsyncWebServerRequest *request) {
	//DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	if (!checkAuth(request)) {		return request->requestAuthentication(); }
	if (request->args() > 0)  {// Save Settings
		_ntpConfig.daylight = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver0") {
				_ntpConfig.ntpServerName0 = urldecode(request->arg(i));
				DEBUGLOG("ntpServerName0: %s\r\n", _ntpConfig.ntpServerName0);
				continue;
			}
			if (request->argName(i) == "ntpserver1") {
				_ntpConfig.ntpServerName1 = urldecode(request->arg(i));
				DEBUGLOG("ntpServerName1: %s\r\n", _ntpConfig.ntpServerName1);
				continue;
			}
			if (request->argName(i) == "ntpserver2") {
				_ntpConfig.ntpServerName2 = urldecode(request->arg(i));
				DEBUGLOG("ntpServerName2: %s\r\n", _ntpConfig.ntpServerName2);
				continue;
			}
			if (request->argName(i) == "update") {
				_ntpConfig.updateNTPTimeEvery = request->arg(i).toInt();
				NTP.setInterval(_ntpConfig.updateNTPTimeEvery * 60);
				continue;
			}
			if (request->argName(i) == "tz") {
				_ntpConfig.timezone = request->arg(i).toInt();
				  NTP.setTimeZone(_ntpConfig.timezone / 10);
				continue;
			}
			if (request->argName(i) == "dst") {
				_ntpConfig.daylight = true;
				DEBUGLOG("Daylight Saving: %d\r\n", _ntpConfig.daylight);
				continue;
			}
		}
		save_configNTP();
		NTP.setNtpServerName(_ntpConfig.ntpServerName0.c_str());
		NTP.setDayLight(_ntpConfig.daylight);
		ntpBeginReserv();
		setTime(NTP.getTime()); //set time
	}
	handleFileRead("/ntp.html", request);
	// request->send_P(200, "text/html", Page_GeneralNtp);


}
void AsyncFSWebServer::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "ntpserver0|" 	+ (String)_ntpConfig.ntpServerName0 			+ "|input\n";
	values += "ntpserver1|" 	+ (String)_ntpConfig.ntpServerName1 			+ "|input\n";
	values += "ntpserver2|" 	+ (String)_ntpConfig.ntpServerName2 			+ "|input\n";

	values += "ntpserver0_d|" 	+ (String)_ntpConfig.ntpServerName0 			+ "|div\n";
	values += "ntpserver1_d|" 	+ (String)_ntpConfig.ntpServerName1 			+ "|div\n";
	values += "ntpserver2_d|" 	+ (String)_ntpConfig.ntpServerName2 			+ "|div\n";

	values += "update|" 	+ (String)_ntpConfig.updateNTPTimeEvery 			+ "|input\n";
	values += "tz|" 		+ (String)_ntpConfig.timezone 						+ "|input\n";
	values += "dst|" 		+ (String)(_ntpConfig.daylight ? "checked" : "") 	+ "|chk\n";
	request->send(200, "text/plain", values);
}

// ntp.html ^^^


// udp.html vvv
void  AsyncFSWebServer::udpTest(AsyncWebServerRequest *request) {
	udpBroadcast.udpBroadcastSend(getUpdPortTx(), udpJsonBroadcast());
}
void AsyncFSWebServer::send_udp_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "udpporttx|" 	  		+(String)_udpConfig.udpPortTx 	+ "|input\n";
	values += "udpportrx|" 	  		+(String)_udpConfig.udpPortRx 	+ "|input\n";
	values += "udptime|"   			+(String)_udpConfig.udpTimeOut 	+ "|input\n";
	values += "udpkeyword|"   		+		 _udpConfig.keyword 	+ "|input\n";
	request->send(200, "text/plain", values);
}


void AsyncFSWebServer::get_udp_configuration_html(AsyncWebServerRequest *request) {
	//DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	if (request->args() > 0) { // get new configs from args
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "udpporttx")  		{ _udpConfig.udpPortTx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpportrx")  		{ _udpConfig.udpPortRx = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udptime")  			{ _udpConfig.udpTimeOut = request->arg(i).toInt();		continue; }
			if (request->argName(i) == "udpkeyword")		{ _udpConfig.keyword = urldecode(request->arg(i));		continue; }
		}
		request->send_P(200, "text/html", Page_GeneralUdp);	// refresh page
		save_configUDP();	 	// Save Settings
		udpBroadcastTimer();	// start new UDP broadcasting
	}
	else {	handleFileRead(request->url(), request);	}
}
// udp.html ^^^

// project.html vvv
void AsyncFSWebServer::send_project_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	Prog_CfgFile_t Prog_CfgFile;
	int _res = espProgrammer.cfg_FileStructGet(Prog_CfgFile);

	String values = "";
	values += "progproj|"	+ 		 Prog_CfgFile.project_name			+ "|input\n";
	values += "progmem|"	+(String)Prog_CfgFile.chip_size 	+ "|input\n";
	request->send(200, "text/plain", values);
}

void AsyncFSWebServer::get_project_configuration_html(AsyncWebServerRequest *request) {
	if (!checkAuth(request)) {		return request->requestAuthentication(); 	}
	Prog_CfgFile_t Prog_CfgFile;
	if (request->args() > 0) { // Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			// if (request->argName(i) == "devicesign") 		{ AVRISP_HexFiles_Web.avr_signature = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "progproj") 		{ Prog_CfgFile.project_name = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "progmem")  		{ Prog_CfgFile.chip_size = request->arg(i).toInt();			continue; }
		}
		request->send_P(200, "text/html", Page_GeneralPrj);
		espProgrammer.cfg_FileSaveFromWeb(Prog_CfgFile);
	}
	else {	handleFileRead(request->url(), request);	}
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}
// project.html ^^^

// system.html vvv
void AsyncFSWebServer::send_device_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "name|"		+ _sysConfig.deviceName		+ "|input\n";
	values += "serial|" 	+ _sysConfig.deviceSerial 	+ "|input\n";
	values += "progtype|"	+ _sysConfig.deviceType		+ "|input\n";
	request->send(200, "text/plain", values);
}

void AsyncFSWebServer::get_system_configuration_html(AsyncWebServerRequest *request) {
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	if (!checkAuth(request)) {		return request->requestAuthentication(); 	}
	if (request->args() > 0) { // Save Settings
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "name") 		{ _sysConfig.deviceName 	= urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "serial") 	{ _sysConfig.deviceSerial 	= urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "progtype") 	{ _sysConfig.deviceType 	= urldecode(request->arg(i));	continue; }
		}
		request->send_P(200, "text/html", Page_GeneralSys);
		save_configSys();
	}
	else {	handleFileRead(request->url(), request);	}
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

// system.html ^^^

// gpio.html vvv

void  AsyncFSWebServer::gpioGetArgs(AsyncWebServerRequest *request) {
	String values = "";
	String uartStr = "";
	if (request->args() > 0) { // get new configs from args
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() , request->arg(i).c_str() );
			if (request->argName(i) == "uartstr")	{
				uartStr = urldecode(request->arg(i));
				Serial.printf("%s \n\r", uartStr.c_str() );
				continue;
			}
			if ( _sysConfig.deviceType == DEVTYPE_GPIO){
				if (request->argName(i) == "led1")	{
					if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MISO, HIGH);	}
					if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MISO, LOW);	}
					continue;
				}
				if (request->argName(i) == "led2")	{
					if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MOSI, HIGH);	}
					if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MOSI, LOW);	}
					continue;
				}
				if (request->argName(i) == "led3")	{
					if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_SCK, HIGH);	}
					if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_SCK, LOW);		}
					continue;
				}
				if (request->argName(i) == "led4")	{
					if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_RST, HIGH);	}
					if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_RST, LOW);	}
					continue;
				}
			}
		}
		request->send(200, "text/plain", values);
		DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	}
}

// gpio.html ^^^

// avr.html vvv
void  AsyncFSWebServer::avrGetActualFWInfo(AsyncWebServerRequest *request) {
	// DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");

	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	int _res0 = avrprog.cfgFileStructGet( AVRISP_HexFiles_Web	);

	Prog_CfgFile_t Prog_CfgFile;
	int _res1 = espProgrammer.cfg_FileStructGet(Prog_CfgFile);

	String values = "";
	if (_res0 < ERROR_OK || _res1 < ERROR_OK) {
		values+= "getinfoerror|Can't open cfg file.|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	values += "proj|"   +			Prog_CfgFile.project_name		+ "|div\n";
	values += "chsize|" + 	(String)Prog_CfgFile.chip_size 			+ "|div\n";
	//values += "signcfg|"   +			AVRISP_HexFiles_Web.avr_signature		+ "|div\n";
	values += "signcon|"   +			avrprog.avrChipSignGet()		+ "|div\n";
	values += "hnamen|" + 			AVRISP_HexFiles_Web.hex_filename		+ "|div\n";
	values += "hvern|"  + 			AVRISP_HexFiles_Web.hex_version			+ "|div\n";
	values += "htimen|" +			AVRISP_HexFiles_Web.hex_buildtime		+ "|div\n";
	values += "flashtime|" +  		AVRISP_HexFiles_Web.fwTS 				+ "|div\n";
	request->send(200, "text/plain", values);
}

void  AsyncFSWebServer::avrProg(AsyncWebServerRequest *request) {
	String values = "";
	DEBUGLOG("_hexfilename  %s \n\r", _hexfileProg.c_str()); // что программируем
	// int _res  = avrprog.avr_ChipProgrammMain(_hexfileProg, NTP.getTimeDateString() );
	int _res  = espProgrammer.prog_Programm(_hexfileProg,  NTP.getTimeDateString());

	DEBUGLOG("avrProg  %d \n\r", _res);
	values	+= "avrprogres|"+(String) _res+"|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}


void  AsyncFSWebServer::avrProgStatus(AsyncWebServerRequest *request) {
	String values = "";
 	values += "avrprogver|" ;
	values += avrprog.chipFlashVerificationResultGet() ;
	values += "|div\n";

	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

void  AsyncFSWebServer::avrFusesRead(AsyncWebServerRequest *request) {
	String values = "";
	AVRISP_fuses_t AVRISP_fuses ;

	avrprog.chipFusesRead(AVRISP_fuses);
	char strbuf[256];

	sprintf(strbuf, "avrfusehigh|%02x|input\n", AVRISP_fuses.high);
	values+= String(strbuf);
	sprintf(strbuf, "avrfuselow|%02x|input\n", AVRISP_fuses.low);
	values+= String(strbuf);
	sprintf(strbuf, "avrfuseprot|%02x|input\n", AVRISP_fuses.lock);
	values+= String(strbuf);
	sprintf(strbuf, "avrfuseext|%02x|input\n", AVRISP_fuses.ext);
	values+= String(strbuf);

	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}


void  AsyncFSWebServer::avrWebFusesWrite(AsyncWebServerRequest *request) {
	if (!checkAuth(request))	{return request->requestAuthentication(); }
		// AVRISP_fuses_t AVRISP_fuses ;
	String s_high = "";  	uint8_t high = 0;
	String s_low  = "";		uint8_t low  = 0;
	String s_lock = "";		uint8_t lock = 0;
	String s_ext  = "";		uint8_t ext  = 0;
	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "avrfusehigh") 	{ s_high = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuselow") 	{ s_low  = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseprot") 	{ s_lock = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseext") 	{ s_ext  = urldecode(request->arg(i));	continue; }
		}
		request->send_P(200, "text/html", Page_AvrRefresh);

		high =        		hex2bin(s_high[0]);
		if (s_high[1])     	high = (high<<4) + 	hex2bin(s_high[1]);
		low =        		hex2bin(s_low[0]);
    	if (s_low[1])     	low = (low<<4) + 	hex2bin(s_low[1]);
		lock =        		hex2bin(s_lock[0]);
    	if (s_lock[1])     	lock = (lock<<4) + 	hex2bin(s_lock[1]);
		ext =        		hex2bin(s_ext[0]);
    	if (s_ext[1])     	ext = (ext<<4) + 	hex2bin(s_ext[1]);

		avrprog.chipFusesWrite(high, low, lock, ext);
	}
	else {
		handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}
// avr.html ^^^


// stm32.html vvv
void AsyncFSWebServer::programmerGetFilesList (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String json = "";
	espProgrammer.web_GetFileList(json);
	request->send(200, "text/json", json);
	json = "";
    DEBUGLOGISP("List of *.hex *.bin *.binary files: %s \n\r", json);
}

void AsyncFSWebServer::programmerGetDiskInfo (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String values = "";
	espProgrammer.web_GetDiskInfo(values);
	request->send(200, "text/json", values);
	values = "";
    DEBUGLOGISP("Disk info: %s \n\r", values);
}

void AsyncFSWebServer::programmerFileDelete(AsyncWebServerRequest *request) {
	if (!checkAuth(request))	{	return request->requestAuthentication();	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = request->arg(0U);
	DEBUGLOG("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/")		{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) {path = "/" + path;}
	if (!_fs->exists(path)) {	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
	request->send(200, "text/plain", "");
}

// загрузчик файла из фронтенда
int AsyncFSWebServer::programmerFileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	int  _ret= 0;
	_hexFileUploadStatus = "";
	static File fsUploadFile;
	static size_t fileSize = 0;
	// Start
	if (!index) {
		DEBUGLOG("Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) {filename = "/" + filename;}
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOG("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOG("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) != len) {
			_hexFileUploadStatus  += "uploadstatus|error|div\n";
			_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
			_hexFileUploadStatus  += "fileSize|" + (String)fileSize 	+"|div\n";
		}
		else {	fileSize += len;	}
	}
	// End
	if (final) {
		if (fsUploadFile) {	fsUploadFile.close();	}
		_ret = fileSize;
		DEBUGLOG("HexFileUpload final Size: %u\n", fileSize);
		_hexfileCheck = filename;
		_hexFileUploadStatus  += "status|ok|div\n";
		_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
		_hexFileUploadStatus  += "fileSize|" + (String)fileSize 	+"|div\n";
		fileSize = 0;
	}
	return _ret;
}


void AsyncFSWebServer::programmerFileUpload2FSStat(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void AsyncFSWebServer::programmerFileUpload2Chip(AsyncWebServerRequest *request) {
	if (!checkAuth(request))	{	return request->requestAuthentication();	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")		{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) {path = "/" + path;}
	if (!_fs->exists(path)) {	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOG("programmerFileUpload2Chip: %s\r\n", path.c_str());
	request->send(200, "text/plain", "");

	//здесь уже выход программирования
	espProgrammer.prog_Programm(path,  NTP.getTimeDateString());

}

// stm32.html ^^^

void AsyncFSWebServer::serverInit() {
	//SERVER INIT
//edit.html vvv
	//list directory
	on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->handleFileList(request);
	});
	//load editor
	on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (!this->handleFileRead("/edit.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	//create file
	on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->handleFileCreate(request);
	});	//delete file
	on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->handleFileDelete(request);
	});
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) {
		 request->send(200, "text/plain", ""); },
		[this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
			this->handleFileUpload(request, filename, index, data, len, final);
	});
//edit.html ^^^

//wifi.html vvv
	on("/admin/values/0", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_values_html(request, 0);
	});

	on("/admin/values/1", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_values_html(request, 1);
	});
	on("/admin/values/2", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_values_html(request, 2);
	});
	on("/admin/values/3", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_values_html(request, 3);
	});

	on("/admin/connectionstate", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_connection_state_values_html(request);
	});
	on("/admin/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_information_values_html(request);
	});

	on("/wifi.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_network_configuration_html(request);
	});
	on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_scanwifi(request);
	});

//wifi.html ^^^

// 	udp.html vvv
	on("/udp/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_udp_configuration_values_html(request);
	});
	on("/udp.html", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->get_udp_configuration_html(request);
	});
	on("/udp/test", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->udpTest(request);
	});

// /udp.html ^^^
// ntp.html vvv
	on("/ntp/info", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_NTP_configuration_values_html(request);
	});

	on("/ntp.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_NTP_configuration_html(request);
	});
// ntp.html ^^^


//system.html vvv
	on("/system/restart", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		DBG_OUTPUT_PORT.println(request->url());
		request->send_P(200, "text/html", Page_IndexRefresh);
		this->restart_esp();
	});
	on("/system/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_wwwauth_configuration_values_html(request);
	});


	on("/admin", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (!this->handleFileRead("/admin.html", request)) {	request->send(404, "text/plain", "FileNotFound");	}
	});
	on("/system/info", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_system_configuration_values_html(request);
	});
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->get_system_configuration_html(request);
	});
	// FIXME
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_wwwauth_configuration_html(request);
	});
	on("/system/devconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_device_values_html(request);
	});
//system.html ^^^

//update.html vvv
	on("/update/updatepossible", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_update_firmware_values_html(request);
	});
	on("/setmd5", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->setUpdateMD5(request);
	});
	on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (!this->handleFileRead("/update.html", request)) { request->send(404, "text/plain", "FileNotFound");	}
	});

	on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
		//what do when we finish
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->updateFileExecute (request);
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		// uploading
		this->uploadUpdateFile(request, filename, index, data, len, final);
	});
//update.html ^^^




	//project.html vvv
	on("/project/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_project_configuration_values_html(request); // show values
	});

	on("/project.html", HTTP_POST,  [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->get_project_configuration_html(request); // save values from the page
	});
	//project.html ^^^


	//stm32.html vvv

	on("/prog/diskinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->programmerGetDiskInfo (request);
	});

	on("/prog/fileslist", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->programmerGetFilesList (request);
	});

	on("/prog/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->programmerFileDelete(request);
	});

	on("/prog/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->programmerFileUpload2FS( filename, index, data, len, final);
	});

	on("/prog/uploadstat", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->programmerFileUpload2FSStat(request);
	});
	on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->programmerFileUpload2Chip(request);
	});
//stm32.html ^^^

//avr.html vvv
//first callback is called after the request has ended with all parsed arguments
//second callback handles file uploads at that location
	on("/avr/info", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->avrGetActualFWInfo(request);
	});
	on("/avr/flashrun", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->avrProg(request);
	});
	on("/avr/flashstatus", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->avrProgStatus(request);
	});
	on("/avr/fuseread", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->avrFusesRead(request);
	});
	on("/avr/fusewrite", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->avrWebFusesWrite(request);
	});
//avr.html ^^^

//gpio.html vvv
	on("/gpio", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->gpioGetArgs(request);
	});
//gpio.html ^^^



	on("/rconfig", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->handle_rest_config(request);
	});

	on("/pconfig", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->post_rest_config(request);
	});

	on("/json", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (jsoncallback) {	this->jsoncallback(request);	}
		else {
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}
	});

	on("/rest", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (restcallback)	{	this->restcallback(request);	}
			else	{
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}

	});

	on("/post", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (postcallback)	{	this->postcallback(request);	}
			else	{
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}

	});

	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound([this](AsyncWebServerRequest *request) {
		DEBUGLOGFH("Not found: %s\r\n", request->url().c_str());
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(200);
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		if (!this->handleFileRead(request->url(), request)) {	request->send(404, "text/plain", "FileNotFound");	} //TODO 404.html
		delete response; // Free up memory!
	});

	_evs.onConnect([](AsyncEventSourceClient* client) {
		DEBUGLOG("Event source client connected from %s\r\n", client->client()->remoteIP().toString().c_str());
	});
	addHandler(&_evs);

#define HIDE_SECRET
#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET

//#define HIDE_CONFIG
#ifdef HIDE_CONFIG
	on(CONFIG_FILE_SYS, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});

	on(USER_CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});

#endif // HIDE_CONFIG

	//get heap status, analog input value and all GPIO statuses in one json call
	on("/all", HTTP_GET, [](AsyncWebServerRequest *request) {
		String json = "{";
		json += "\"heap\":" + String(ESP.getFreeHeap());
		json += ", \"analog\":" + String(analogRead(A0));
		//json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
		json += "}";
		request->send(200, "text/json", json);
		json = String();
	});
	//server.begin(); --> Not here
	DEBUGLOG("HTTP server started\r\n");
}

bool AsyncFSWebServer::checkAuth(AsyncWebServerRequest *request) {
	if (!_httpAuth.auth) {	return true;}
	else {
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}

}

const char* AsyncFSWebServer::getHostName() {
	String hostname = _sysConfig.deviceName+"_"+_sysConfig.deviceSerial;
	return hostname.c_str();
}

AsyncFSWebServer& AsyncFSWebServer::setJSONCallback(JSON_CALLBACK_SIGNATURE) {
	this->jsoncallback = jsoncallback;
	return *this;
}

AsyncFSWebServer& AsyncFSWebServer::setRESTCallback(REST_CALLBACK_SIGNATURE) {
	this->restcallback = restcallback;
	return *this;
}

AsyncFSWebServer& AsyncFSWebServer::setPOSTCallback(POST_CALLBACK_SIGNATURE) {
	this->postcallback = postcallback;
	return *this;
}

void AsyncFSWebServer::setUSERVERSION(String Version) {
	_Version_App = Version;
}

// String AsyncFSWebServer::FilesListGet() {


//     if (!_fs) { _fs->begin();  }// If SPIFFS is not started
// // #ifdef ESP32
// //     File root =  _fs->open("/");
// //     File file = root.openNextFile();
// //     while (file) {
// //         if (file.isDirectory()) {
// //             json +=   "DIR: [";  json += file.name();   json += "]\n\r";
// //         } else {
// //             json +=   "\t";
// // 			json += file.name();
// // 			json +=   " \t";
// // 			json += String(file.size());
// // 			json +=   "\n\r";
// //         }
// //         file = root.openNextFile();
// //     }
// // #else
// //     Dir files = _fs->openDir("/");
// //     while (files.next()) {
// //         if (files.isDirectory()) {
// //             list +=   "DIR: [";  list += files.fileName();   list +=   "] \n\r";
// //         } else {
// //             File f = files.openFile("r");	list +=   "\t";
// // 			list  += files.fileName();		list +=   " \t";
// // 			list  += String(f.size());		list +=   "\n\r";
// //         }
// //     }
// // #endif
// 	String json = "[";

// 	json += "{";
// 	json +=  "\"fname\":filenameTest1";
// 	json +=  "\"type\":filetypeTest1";
// 	json +=  "\"size\":filesizeTest1";
// 	json +=  "\"actual\":actualTest1";
// 	json +=  "\"chip\":chipTest1";
// 	json +=  "\"date\":dateTest1";
// 	json += "}";

// 	json += "{";
// 	json +=  "\"fname\":filenameTest2";
// 	json +=  "\"type\":filetypeTest2";
// 	json +=  "\"size\":filesizeTest2";
// 	json +=  "\"actual\":actualTest2";
// 	json +=  "\"chip\":chipTest2";
// 	json +=  "\"date\":dateTest2";
// 	json += "}";

// 	json += "]";
//     return json;
// }



void AsyncFSWebServer::serialShowInfo() {
	Serial.printf("Ep8266 service chip firmware ver: %s\n\r",  VERSION_APP);
	Serial.printf("Ep8266 web pages ver: %s\n\r",  VERSION_WEB);
	Serial.printf("build DateTime: %s %s  \n\r",  __DATE__, __TIME__);
	Serial.printf("wifi ssid: %s \n", WiFi.SSID().c_str());
	Serial.printf("GotIP Address: %s \n", WiFi.localIP().toString().c_str());

	#if defined(ESP32)
    Serial.printf("WifiHostName  %s \n\r", 	WiFi.getHostname());
    #elif defined(ESP8266)
	Serial.printf("WifiHostName  %s \n\r", 	WiFi.hostname().c_str());
    #endif


	Serial.printf("Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
	Serial.printf("DNS: %s\r\n", WiFi.dnsIP().toString().c_str());

	String hostname = _sysConfig.deviceName+"_"+_sysConfig.deviceSerial;
	Serial.printf("local DNS hostname  http://%s.local \n\r", hostname.c_str());
	Serial.printf("or you can connect directly  http://%s \n\r", WiFi.localIP().toString().c_str());
}

uint16_t AsyncFSWebServer::getUpdPortTx() 	{	return _udpConfig.udpPortTx;	}
uint16_t AsyncFSWebServer::getUpdPortRx() 	{	return _udpConfig.udpPortRx;	}
uint16_t AsyncFSWebServer::getudpTimeOut() 	{	return _udpConfig.udpTimeOut;	}
String AsyncFSWebServer::getudpKeyword() 	{	return _udpConfig.keyword;	}

String AsyncFSWebServer::udpJsonBroadcast() {
	String _ret = "";

	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	avrprog.cfgFileStructGet( AVRISP_HexFiles_Web);
	String hostname = "http://" + _sysConfig.deviceName+"_"+_sysConfig.deviceSerial+".local";
	String iphost 	= "http://" + WiFi.localIP().toString();
	JsonDocument jsonDoc;
	jsonDoc["deviceName"] 		= _sysConfig.deviceName;
	jsonDoc["deviceSerial"] 	= _sysConfig.deviceSerial;
	jsonDoc["deviceType"] 		= _sysConfig.deviceType;


	jsonDoc["ip"] 				= WiFi.localIP().toString();
	jsonDoc["dnshost"] 			= hostname;
	jsonDoc["iphost"] 			= iphost;
	jsonDoc["mac"] 			    = WiFi.macAddress();
	jsonDoc["ntpNow"] 			= NTP.getTimeDateString() ;

	jsonDoc["udpPortTx"] 		= _udpConfig.udpPortTx;
	jsonDoc["udpPortRx"] 		= _udpConfig.udpPortRx;
	jsonDoc["udpTimeOut"] 		= _udpConfig.udpTimeOut;
	jsonDoc["keyword"] 			= _udpConfig.keyword;

	//jsonDoc["chip"] 			= AVRISP_HexFiles_Web.avr_signature;
	// jsonDoc["size"] 			= AVRISP_HexFiles_Web.chipsize;
	// jsonDoc["project"] 			= AVRISP_HexFiles_Web.project_name;
	jsonDoc["esp8266Ver"] 		= VERSION_APP;
	jsonDoc["webVer"] 			= VERSION_WEB;
	jsonDoc["buildDate"] 		= __DATE__;
	jsonDoc["buildTime"] 		= __TIME__;

	serializeJsonPretty(jsonDoc, _ret);
	// DEBUGLOG(__PRETTY_FUNCTION__); DEBUGLOG("\r\n");
	return _ret;
}


// TODO Insert to Logseq "Common.h" page
/*
 * hex2bin
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 * returns 0xFF if bad hex digit.
 */
uint8_t AsyncFSWebServer::hex2bin (uint8_t h)    {
    if (h >= '0' && h <= '9')
       { return(h - '0'); }
    if (h >= 'A' && h <= 'F')
       { return((h - 'A') + 10); }
	if (h >= 'a' && h <= 'f')
       { return((h - 'a') + 10); }
    DEBUGLOGISP("Bad hex digit! %x \n\r", h);
    return 0xff;
}
// convert a single hex digit character to its integer value (from https://code.google.com/p/avr-netino/)
unsigned char AsyncFSWebServer::h2int(char c) {
	if (c >= '0' && c <= '9') {
		return((unsigned char)c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return((unsigned char)c - 'a' + 10);
	}
	if (c >= 'A' && c <= 'F') {
		return((unsigned char)c - 'A' + 10);
	}
	return(0);
}

String AsyncFSWebServer::urldecode(String input) { // (based on https://code.google.com/p/avr-netino/)
	char c;
	String ret = "";

	for (byte t = 0; t < input.length(); t++) {
		c = input[t];
		if (c == '+') { c = ' ';}
		if (c == '%') {
			t++;
			c = input[t];
			t++;
			c = (h2int(c) << 4) | h2int(input[t]);
		}
		ret.concat(c);
	}
	return ret;
}

//
// Check the Values is between 0-255
//
boolean AsyncFSWebServer::checkRange(String Value) {
	if (Value.toInt() < 0 || Value.toInt() > 255) {
		return false;
	}
	else {
		return true;
	}
}
