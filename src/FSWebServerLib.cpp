#include <StreamString.h>
#include "FSWebServerLib.h"
#include "main.h"
#include "avrisp.h"

AsyncFSWebServer ESPHTTPServer(80);



const char Page_ConfigRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/config.html">
Please Wait....Configuring Wifi.
)=====";

const char Page_IndexRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/index.htm">
Please Wait....Configuring and Restarting.
)=====";

const char Page_GeneralRefresh[] = R"=====(
<meta http-equiv="refresh" content="10; URL=/general.html">
Please Wait....Configuring and Restarting.
)=====";

String _Version_App = VERSION_APP;
String _Version_Web = VERSION_WEB;
String _Version_BuildDate = APP_BUILDDATE;
String _Version_BuildTime = APP_BUILDTIME;

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
			self->configureWifiAP();
		}	
	}
	if (self->WifiScan == FS_STAT_SCANED)	{
		self->configureWifi();	
		self->WifiScan = WF_STAT_NONEED;
	}
	if (self->WifiScan != WF_STAT_NONEED) {
		self->load_configWifi(self->scanWifi());
	}
#endif //AP_ENABLE_TIMEOUT

}

void  AsyncFSWebServer::avrGetInfo(AsyncWebServerRequest *request) {
	String values = "";
	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	avrsip_err_t _res = avrprog.cfgFileStructGet( AVRISP_HexFiles_Web	);
	if (_res < ERROR_OK) {
		values+= "getinfoerror|Can't open cfg file.|div\n";
	} else {

		values += "proj|"   +			AVRISP_HexFiles_Web.project_name		+ "|div\n";
		values += "sign|"   +			AVRISP_HexFiles_Web.avr_signature		+ "|div\n";
		values += "chsize|" + 	(String)AVRISP_HexFiles_Web.chipsize 			+ "|div\n";

		values += "hnamen|" + 			AVRISP_HexFiles_Web.hex_filename		+ "|div\n";
		values += "hvern|"  + 			AVRISP_HexFiles_Web.hex_version			+ "|div\n";
		values += "htimen|" +			AVRISP_HexFiles_Web.hex_buildtime		+ "|div\n";

		values += "hnameo|" + 			AVRISP_HexFiles_Web.hex_filename_old	+ "|div\n";
		values += "hvero|"  + 			AVRISP_HexFiles_Web.hex_version_old		+ "|div\n";
		values += "htimeo|"	+  			AVRISP_HexFiles_Web.hex_buildtime_old	+ "|div\n";
		values += "flashtime|" +  		AVRISP_HexFiles_Web.fwTS 				+ "|div\n";

	}
	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void  AsyncFSWebServer::avrCheckFile(AsyncWebServerRequest *request) {
	String values = "";
	AVRISP_HexFileUploaded_t HexFileUploaded ;
	avrsip_err_t _res = avrprog.hexFileMetaStructGet( _hexfileCheck, HexFileUploaded	);
	if (_res < ERROR_OK) {
		values+= "checkmetaerror|no meta info|div\n";
	} else {
		values += "sign|" + HexFileUploaded.signture	 			+ "|div\n";
		values += "proj|" + HexFileUploaded.project_name 			+ "|div\n";
		values += "vers|" + HexFileUploaded.version					+ "|div\n";
		values += "time|" + HexFileUploaded.buildtime	 			+ "|div\n";
		values += "cmpsign|" + (String)HexFileUploaded.cmpsign	 	+ "|div\n";
		values += "cmpproj|" + (String)HexFileUploaded.cmpproj	 	+ "|div\n";
		if (HexFileUploaded.cmpsign && HexFileUploaded.cmpproj){
			_hexfileProg = _hexfileCheck;
		}
	}
	//check hex file binary data before flashing
    int32_t _size = avrprog.hexFileUploadedBodyCheck(_hexfileCheck);
    if (_size >= ERROR_OK) 			{ 
		values	+= "bodyerror|ok|div\n";	
		values 	+= "size|" + (String)_size	 	+ "|div\n";
	}
    if (_size == ERR_NOFILE) 		{ values+= "bodyerror|no file!|div\n";	}
    if (_size == ERR_INCORRECTFILE) { values+= "bodyerror|file damaged|div\n";	}
    if (_size == ERR_HEXADDR) 		{ values+= "bodyerror|addres line damaged|div\n";	}
    if (_size == ERR_HEXCRC) 		{ values+= "bodyerror|crc damaged|div\n";	}

	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}


void  AsyncFSWebServer::avrProgRollback(AsyncWebServerRequest *request) { 
// GalyaUNasOtmena
	String values = "";
	DEBUGLOG("_hexfilename  %s \n\r", _hexfileProg.c_str());
	avrsip_err_t _res ;
 	_res = avrprog.avrChipProgrammMain("2", NTP.getTimeDateString() );
	
	DEBUGLOG("avrProgRollback  %d \n\r", _res);
	values	+= "avrprogres|"+(String) _res+"|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void  AsyncFSWebServer::avrProg(AsyncWebServerRequest *request) {
	String values = "";
	DEBUGLOG("_hexfilename  %s \n\r", _hexfileProg.c_str());
	avrsip_err_t _res ;
 	_res = avrprog.avrChipProgrammMain(_hexfileProg, NTP.getTimeDateString() );
	
	DEBUGLOG("avrProg  %d \n\r", _res);
	values	+= "avrprogres|"+(String) _res+"|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}


void  AsyncFSWebServer::avrProgStatus(AsyncWebServerRequest *request) {
	String values = "";
	

 	values += "avrprogver|" ;
	values += avrprog.chipFlashVerificationResultGet() ;
	values += "|div\n";

	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}


void AsyncFSWebServer::sendTimeData() {
	DEBUGLOG("sendTimeData %s\r\n", NTP.getTimeDateString().c_str());
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

String formatBytes(size_t bytes) {
	if (bytes < 1024) {
		return String(bytes) + "B";
	}
	else if (bytes < (1024 * 1024)) {
		return String(bytes / 1024.0) + "KB";
	}
	else if (bytes < (1024 * 1024 * 1024)) {
		return String(bytes / 1024.0 / 1024.0) + "MB";
	}
	else {
		return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
	}
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


void AsyncFSWebServer::NTPHandler(NTPSyncEvent_t event)	{
	int _ntpevent = static_cast<int>(event);
    if ( _ntpevent == timeSyncd) 		{ DEBUGLOG("\t NTP_timeSyncd\r\n"); 	}
	if ( _ntpevent == noResponse) 		{ DEBUGLOG("\t NTP_noResponse \r\n"); 	}
	if ( _ntpevent == invalidAddress) 	{ DEBUGLOG("\t NTP_invalidAddress\r\n"); 	}
	if ( _ntpevent == requestSent) 		{ DEBUGLOG("\t NTP_requestSent\r\n"); 	}
	if ( _ntpevent == errorSending) 	{ DEBUGLOG("\t NTP_errorSending \r\n"); 	}
	if ( _ntpevent == responseError) 	{ DEBUGLOG("\t NTP_responseError \r\n"); 	}
	if (WiFi.status() != WL_CONNECTED) {return;}
	if (_ntpevent == noResponse || _ntpevent == invalidAddress ||  _ntpevent == responseError ) {
		NTPbeginReserv();
	}	
}

void AsyncFSWebServer::NTPbeginReserv (){
	if  (_ntpserveer == 0) 	NTP.begin(_generalConfig.ntpServerName0, _generalConfig.timezone / 10, _generalConfig.daylight);
	if  (_ntpserveer == 1)  NTP.begin(_generalConfig.ntpServerName1, _generalConfig.timezone / 10, _generalConfig.daylight);
	if  (_ntpserveer == 2)  NTP.begin(_generalConfig.ntpServerName2, _generalConfig.timezone / 10, _generalConfig.daylight);
	_ntpserveer++;
	if (_ntpserveer > 2) _ntpserveer = 2;
}

void AsyncFSWebServer::begin(FS* fs) {
	_fs = fs;

	avrprog.setFs(&SPIFFS);
    avrprog.setReset(false);  // let the AVR run
    avrprog.begin();

	connectionTimout = 0;
	_ntpserveer = 0;
	DBG_OUTPUT_PORT.begin(115200);
	DBG_OUTPUT_PORT.print("\n\n");
#ifndef RELEASE
	DBG_OUTPUT_PORT.setDebugOutput(true);
#endif // RELEASE
	// NTP client setup
	if (CONNECTION_LED >= 0) {
		pinMode(CONNECTION_LED, OUTPUT); // CONNECTION_LED pin defined as output
	}
	if (AP_ENABLE_BUTTON >= 0) {
		pinMode(AP_ENABLE_BUTTON, INPUT_PULLUP); // If this pin is HIGH during startup ESP will run in AP_ONLY mode. Backdoor to change WiFi settings when configured WiFi is not available.
	}
	

	if (AP_ENABLE_BUTTON >= 0) {
		_apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", _apConfig.APenable);
	}

	if (CONNECTION_LED >= 0) {	
		digitalWrite(CONNECTION_LED, HIGH); // Turn LED off	
	}

	if (!_fs) // If SPIFFS is not started
		_fs->begin();
#ifndef RELEASE
	{ // List files
		Dir dir = _fs->openDir("/");
		while (dir.next()) {
			String fileName = dir.fileName();
			size_t fileSize = dir.fileSize();
			DEBUGLOG("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
		}
		DEBUGLOG("\n");
	}
#endif // RELEASE
	if (!load_configGeneral()) { defaultConfigGeneral();  	}
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
	loadHTTPAuth();
//WIFI INIT start here
	if (_generalConfig.updateNTPTimeEvery > 0) { // Enable NTP sync
		
        NTP.setInterval (_generalConfig.updateNTPTimeEvery * 60);
        NTP.setNTPTimeout (NTP_TIMEOUT);
		NTP.onNTPSyncEvent([this](NTPSyncEvent_t event){	NTPHandler(event);	});
		NTPbeginReserv();
		NTP.getTime();	
	}
	// Register wifi Event to control connection LED
	onStationModeConnectedHandler = WiFi.onStationModeConnected([this](WiFiEventStationModeConnected data) {
		this->onWiFiConnected(data);
	});

	onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([this](WiFiEventStationModeDisconnected data) {
		this->onWiFiDisconnected(data);
	});

	onStationModeGotIPHandler = WiFi.onStationModeGotIP([this](WiFiEventStationModeGotIP data) {
		this->onWiFiConnectedGotIP(data);
	});
	String hostName = _generalConfig.deviceName + "_" + _generalConfig.deviceSerial;
	WiFi.hostname(hostName.c_str());
	
	// if (_apConfig.APenable) {
	// 		configureWifiAP(); 
	// 	}
	if (AP_ENABLE_BUTTON >= 0) {
		if (_apConfig.APenable) {
			configureWifiAP(); // Set AP mode if AP button was pressed
		}
		else {
			configureWifi(); // Set WiFi config
		}
	}
	else {
		configureWifi(); // Set WiFi config
	}
	
	DEBUGLOG("Open http://");
	DEBUGLOG(hostName.c_str());
	DEBUGLOG(".local to see the device web page.\r\n");
	
	DEBUGLOG("Device serial number:");
	DEBUGLOG(_generalConfig.deviceSerial.c_str());

	DEBUGLOG("\n\rFlash chip size: %u\r\n", 	ESP.getFlashChipRealSize());
	DEBUGLOG("Scketch size: %u\r\n", 		ESP.getSketchSize());
	DEBUGLOG("Free flash space: %u\r\n", 	ESP.getFreeSketchSpace());

	_secondTk.attach(1.0f, &AsyncFSWebServer::s_secondTick, static_cast<void*>(this)); // Task to run periodic things every second

	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server
	String mdnsName = _generalConfig.deviceName + "_" + _generalConfig.deviceSerial;
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	ConfigureOTA(_httpAuth.wwwPassword.c_str());
	DEBUGLOG("END Setup\n\r");
	
}

//duplicate config stuff for user level config items

bool AsyncFSWebServer::load_configWifi(int _in) {
	if (_in < 0) return false;
	File configFile;
	if (_in == 0) {		 configFile = _fs->open(WIFI_CONFIG_FILE0, "r");	}
#if (USE_RESERV_WIFI > 0)
	if (_in == 1) {		 configFile = _fs->open(WIFI_CONFIG_FILE1, "r");	}
	if (_in == 2) {		 configFile = _fs->open(WIFI_CONFIG_FILE2, "r");	}
	if (_in == 3) {		 configFile = _fs->open(WIFI_CONFIG_FILE3, "r");	}
	
#endif
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
	DEBUGLOG("file size: %d bytes\r\n", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	// String temp;
	// serializeJsonPretty(jsonDoc, temp);
	// Serial.println(temp);
#endif
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
	//config.connectionLed = jsonDoc["led"];

	DEBUGLOG("Config %d wifi initialized. ", _in);
	// DEBUGLOG("SSID: %s ", 	_wifiConfig.ssid.c_str());
	// DEBUGLOG("PASS: %s\r\n", _wifiConfig.password.c_str());
	//// DEBUGLOG("Connection LED: %d\n", config.connectionLed);
	DEBUGLOG(__PRETTY_FUNCTION__); DEBUGLOG("\r\n");

	return true;
}


bool AsyncFSWebServer::load_configGeneral() {
	File configFile = _fs->open(CONFIG_FILE, "r");	

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
	DEBUGLOG("file size: %d bytes\r\n", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp);
#endif
	_generalConfig.ntpServerName0 = jsonDoc["ntp0"].as<const char *>();
	_generalConfig.ntpServerName1 = jsonDoc["ntp1"].as<const char *>();
	_generalConfig.ntpServerName2 = jsonDoc["ntp2"].as<const char *>();
	_generalConfig.updateNTPTimeEvery = jsonDoc["NTPperiod"].as<long>();
	_generalConfig.timezone = jsonDoc["timeZone"].as<long>();
	_generalConfig.daylight = jsonDoc["daylight"].as<long>();
	_generalConfig.deviceName = jsonDoc["deviceName"].as<const char *>();
	_generalConfig.deviceSerial = jsonDoc["deviceSerial"].as<const char *>();
	DEBUGLOG("NTP Server0: %s\r\n", _generalConfig.ntpServerName0.c_str());
	DEBUGLOG("NTP Server1: %s\r\n", _generalConfig.ntpServerName1.c_str());
	DEBUGLOG("NTP Server2: %s\r\n", _generalConfig.ntpServerName2.c_str());
	//DEBUGLOG("Connection LED: %d\n", config.connectionLed);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	return true;
}


void AsyncFSWebServer::defaultConfigGeneral() {
	// DEFAULT CONFIG
	_generalConfig.ntpServerName0 = "pool.ntp.org";
	_generalConfig.ntpServerName1 = "0.ru.pool.ntp.org";
	_generalConfig.ntpServerName2 = "0.gentoo.pool.ntp.org";
	_generalConfig.updateNTPTimeEvery = 15;
	_generalConfig.timezone = 10;
	_generalConfig.daylight = 1;
	_generalConfig.deviceName = "esp8266_server"; 
	_generalConfig.deviceSerial = SERIAL_NUMBER; // String deviceSerial;
	//config.connectionLed = CONNECTION_LED;
	save_configGeneral();
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_configGeneral() {
	//flag_config = false;
	DEBUGLOG("Save config\r\n");
	DynamicJsonDocument jsonDoc(JSON_STR_LEN);
	jsonDoc["ntp0"] 		= _generalConfig.ntpServerName0;
	jsonDoc["ntp1"] 		= _generalConfig.ntpServerName1;
	jsonDoc["ntp2"] 		= _generalConfig.ntpServerName2;
	jsonDoc["NTPperiod"] 	= _generalConfig.updateNTPTimeEvery;
	jsonDoc["timeZone"] 	= _generalConfig.timezone;
	jsonDoc["daylight"] 	= _generalConfig.daylight;
	jsonDoc["deviceName"] 	= _generalConfig.deviceName; 
	jsonDoc["deviceSerial"] = _generalConfig.deviceSerial; 
	File configFile  = _fs->open(CONFIG_FILE, "w");	

	if (!configFile) {
		DEBUGLOG("Failed to open config file for writing\r\n");
		configFile.close();
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp);
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
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
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::save_configWifi(int _in) {
	//flag_config = false;
	DEBUGLOG("Save config\r\n");
	DynamicJsonDocument jsonDoc(JSON_STR_LEN);
	
	jsonDoc["ssid"] = _wifiConfig.ssid;
	jsonDoc["pass"] = _wifiConfig.password;
	jsonDoc["dhcp"] = _wifiConfig.dhcp;
	JsonArray jsonip = jsonDoc.createNestedArray("ip");
	jsonip.add(_wifiConfig.ip[0]);
	jsonip.add(_wifiConfig.ip[1]);
	jsonip.add(_wifiConfig.ip[2]);
	jsonip.add(_wifiConfig.ip[3]);

	JsonArray jsonNM = jsonDoc.createNestedArray("netmask");
	jsonNM.add(_wifiConfig.netmask[0]);
	jsonNM.add(_wifiConfig.netmask[1]);
	jsonNM.add(_wifiConfig.netmask[2]);
	jsonNM.add(_wifiConfig.netmask[3]);

	JsonArray jsonGateway = jsonDoc.createNestedArray("gateway");
	jsonGateway.add(_wifiConfig.gateway[0]);
	jsonGateway.add(_wifiConfig.gateway[1]);
	jsonGateway.add(_wifiConfig.gateway[2]);
	jsonGateway.add(_wifiConfig.gateway[3]);

	JsonArray jsondns = jsonDoc.createNestedArray("dns");
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
	Serial.println(temp);
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::clearConfig(bool reset)	{
	if (_fs->exists(CONFIG_FILE)) { _fs->remove(CONFIG_FILE);	}
	if (_fs->exists(WIFI_CONFIG_FILE0)) { _fs->remove(WIFI_CONFIG_FILE0);	}
#if (USE_RESERV_WIFI > 0)
	if (_fs->exists(WIFI_CONFIG_FILE1)) { _fs->remove(WIFI_CONFIG_FILE1);	}
	if (_fs->exists(WIFI_CONFIG_FILE2)) { _fs->remove(WIFI_CONFIG_FILE2);	}
	if (_fs->exists(WIFI_CONFIG_FILE3)) { _fs->remove(WIFI_CONFIG_FILE3);	}
#endif
	if (_fs->exists(SECRET_FILE)) {		_fs->remove(SECRET_FILE);	}
	if (reset) {
		_fs->end();
		ESP.restart();
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
	DEBUGLOG("496 JSON file size: %d bytes\r\n", size);
	DynamicJsonDocument jsonDoc(1024);
	auto error = deserializeJson(jsonDoc, buf.get());
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}

#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	Serial.println(temp);
#endif

	value = jsonDoc[name].as<const char*>();

	DEBUGLOG("User data initialized.\r\n");
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	return true;
}

bool AsyncFSWebServer::save_user_config(String name, String value) {
	//add logic to test and create if non
	DEBUGLOG(name.c_str());
	DEBUGLOG("\r\n");
	DEBUGLOG(value.c_str());
	DEBUGLOG("\r\n");

	File configFile;
	if (!_fs->exists(USER_CONFIG_FILE))
	{
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
	DynamicJsonDocument jsonDoc(1024);
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
	Serial.println(temp);
#endif
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::clearUserConfig(bool reset) {
	if (_fs->exists(USER_CONFIG_FILE)) {
		_fs->remove(USER_CONFIG_FILE);
	}

	if (reset) {
		_fs->end();
		ESP.restart();
	}
}

bool AsyncFSWebServer::load_user_config(String name, int &value)
{
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
	File configFile = _fs->open(SECRET_FILE, "r");
	if (!configFile) {
		DEBUGLOG("Failed to open secret file\r\n");
		_httpAuth.auth = false;
		_httpAuth.wwwUsername = "";
		_httpAuth.wwwPassword = "";
		configFile.close();
		return false;
	}

	size_t size = configFile.size();
	/*if (size > 256) {
	DEBUGLOG("Secret file size is too large\r\n");
	httpAuth.auth = false;
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
	DEBUGLOG("JSON secret file size: %d bytes\n", size);
	DynamicJsonDocument jsonDoc(256);
	auto error = deserializeJson(jsonDoc, buf.get());
	
	if (error) {
#ifndef RELEASE
		String temp;
		serializeJsonPretty(jsonDoc, temp);
		DBG_OUTPUT_PORT.println(temp);
		DBG_OUTPUT_PORT.println("Failed to parse secret file. Error: ");
		DBG_OUTPUT_PORT.println(error.c_str());
#endif // RELEASE
		_httpAuth.auth = false;
		return false;
	}
#ifndef RELEASE
	String temp;
	serializeJsonPretty(jsonDoc, temp);
	DBG_OUTPUT_PORT.println(temp);
#endif // RELEASE
	
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
		NTPbeginReserv();
		// NTP.begin(_generalConfig.ntpServerName0, _generalConfig.timezone / 10, _generalConfig.daylight);
		NTP.setInterval(15, _generalConfig.updateNTPTimeEvery * 60);
		Serial.println(NTP.getLastNTPSync());
		updateTimeFromNTP = false;
	}
}

void AsyncFSWebServer::configureWifiAP() {
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");

	if (WiFi.status() == WL_CONNECTED) { WiFi.disconnect();	}
	
	WiFi.mode(WIFI_AP);

	wifiStatus = FS_STAT_APMODE;

	load_configGeneral();
	String APname = _generalConfig.deviceName + "_" + _generalConfig.deviceSerial;

	if (_httpAuth.auth) {
		WiFi.softAP(APname.c_str(), _httpAuth.wwwPassword.c_str());
		DEBUGLOG("AP Pass enabled: %s \r\n", _httpAuth.wwwPassword.c_str());
	}
	else {
		WiFi.softAP(APname.c_str());
		DEBUGLOG("AP Pass disabled \r\n");
	}
	if (CONNECTION_LED >= 0) {
		flashLED(CONNECTION_LED, 3, 250);
	}

	DBG_OUTPUT_PORT.printf("AP Mode enabled. SSID: %s IP: %s\r\n", WiFi.softAPSSID().c_str(), WiFi.softAPIP().toString().c_str());
	
	connectionTimout = 0;
	
}

int AsyncFSWebServer::scanWifi() { 
	int _scanNum = -1;
	int nets = WiFi.scanComplete();
	if (nets == WIFI_SCAN_FAILED) {	
		WiFi.scanNetworks(true);
	} 
	else 
	if (nets) {
		for (int i = 0; i < nets; ++i) {
			if (strcmp( _strWifi0,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 0; }
			if (strcmp( _strWifi1,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 1; }
			if (strcmp( _strWifi2,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 2; }
			if (strcmp( _strWifi3,  WiFi.SSID(i).c_str()) == 0){ _scanNum = 3; }
			WifiScan = FS_STAT_SCANED;
		}
		if (WiFi.scanComplete() == WIFI_SCAN_FAILED) { WiFi.scanNetworks(true);	}
	}
	if (nets >= 0) WiFi.scanDelete();
	DEBUGLOG("_scanNum %d nets %d \r\n", _scanNum, nets);	
	return _scanNum;
}


void AsyncFSWebServer::configureWifi() { // set esp8266 as wifi client
	if (wifiStatus == FS_STAT_APMODE) return;
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	//disconnect required here
	//improves reconnect reliability
	WiFi.disconnect();
	//encourge clean recovery after disconnect species5618, 08-March-2018
	WiFi.mode(WIFI_STA);

	DBG_OUTPUT_PORT.printf("Connecting to %s\r\n", _wifiConfig.ssid.c_str());
	WiFi.begin(_wifiConfig.ssid.c_str(), _wifiConfig.password.c_str());
	// connectionTimout = 0;
	wifiStatus = FS_STAT_CONNECTING;
//Only use wait waitForConnectResult if the timeout is not enabled to not mess with the timeout
#if (AP_ENABLE_TIMEOUT <= 0)
	WiFi.waitForConnectResult();
#endif //AP_ENABLE_TIMEOUT
}

void AsyncFSWebServer::ConfigureOTA(String password) {
	// Port defaults to 8266
	// ArduinoOTA.setPort(8266);

	// Hostname was defaults to esp8266-[ChipID]. 
	//TODO new hostname Sam Arcanum
	String hostName = _generalConfig.deviceName+"_"+_generalConfig.deviceSerial;
	ArduinoOTA.setHostname(hostName.c_str());

	// No authentication by default
	if (password != "") {
		ArduinoOTA.setPassword(password.c_str());
		DEBUGLOG("OTA password set %s\n", password.c_str());
	}

#ifndef RELEASE
	ArduinoOTA.onStart([]() {
		DEBUGLOG("StartOTA\r\n");
	});
	ArduinoOTA.onEnd(std::bind([](FS *fs) {
		fs->end();
		DEBUGLOG("\r\nEnd OTA\r\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUGLOG("OTA Progress: %u%%\r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		DEBUGLOG("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) 			DEBUGLOG("Auth Failed\r\n");
		else if (error == OTA_BEGIN_ERROR) 		DEBUGLOG("Begin Failed\r\n");
		else if (error == OTA_CONNECT_ERROR)	DEBUGLOG("Connect Failed\r\n");
		else if (error == OTA_RECEIVE_ERROR) 	DEBUGLOG("Receive Failed\r\n");
		else if (error == OTA_END_ERROR) 		DEBUGLOG("End Failed\r\n");
	});
	DEBUGLOG("\r\nOTA Ready\r\n");
#endif // RELEASE
	ArduinoOTA.begin();
}

void AsyncFSWebServer::onWiFiConnected(WiFiEventStationModeConnected data) {
	DBG_OUTPUT_PORT.println("WiFi Connected: Waiting for DHCP");
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
	}
	DEBUGLOG("Led %d on\n", CONNECTION_LED);
	//turnLedOn();
	wifiDisconnectedSince = 0;
}

void AsyncFSWebServer::onWiFiConnectedGotIP(WiFiEventStationModeGotIP data) {
	DBG_OUTPUT_PORT.printf("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	DEBUGLOG("Gateway:    %s\r\n", WiFi.gatewayIP().toString().c_str());
	DEBUGLOG("DNS:        %s\r\n", WiFi.dnsIP().toString().c_str());
	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, LOW); // Turn LED on
	}
	DEBUGLOG("Led %d on\n", CONNECTION_LED);
	//turnLedOn();
	wifiDisconnectedSince = 0;
	//force NTPsstart after got ip
	if (_generalConfig.updateNTPTimeEvery > 0) { // Enable NTP sync
		updateTimeFromNTP = true;
	}
	
	connectionTimout = 0;
	_ntpserveer = 0;
	wifiStatus = FS_STAT_CONNECTED;
}


void AsyncFSWebServer::onWiFiDisconnected(WiFiEventStationModeDisconnected data) {
	DEBUGLOG(" case STA_DISCONNECTED \r\n");
	if (CONNECTION_LED >= 0) {	digitalWrite(CONNECTION_LED, HIGH);	} // Turn LED off
	//DBG_OUTPUT_PORT.printf("Led %s off\n", CONNECTION_LED);
	//flashLED(config.connectionLed, 2, 100);
	if (wifiDisconnectedSince == 0) { wifiDisconnectedSince = millis(); }
	DEBUGLOG("Disconnected for %d seconds \r\n", (int)((millis() - wifiDisconnectedSince) / 1000));
	wifiStatus = FS_STAT_CONNECTING;
	WifiScan = FS_STAT_SCANING;

}

void AsyncFSWebServer::handleFileList(AsyncWebServerRequest *request) {
	if (!request->hasArg("dir")) { request->send(500, "text/plain", "BAD ARGS"); return; }

	String path = request->arg("dir");
	DEBUGLOG("handleFileList: %s\r\n", path.c_str());
	Dir dir = _fs->openDir(path);
	path = String();

	String output = "[";
	while (dir.next()) {
		File entry = dir.openFile("r");
		if (true)//entry.name()!="secret.json") // Do not show secrets
		{
			if (output != "[")
				output += ',';
			bool isDir = false;
			output += "{\"type\":\"";
			output += (isDir) ? "dir" : "file";
			output += "\",\"name\":\"";
			output += String(entry.name()).substring(1);
			output += "\"}";
		}
		entry.close();
	}

	output += "]";
	//DEBUGLOG("%s\r\n", output.c_str());
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
	if (path.endsWith("/"))
		path += "index.htm";
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) {
			path += ".gz";
		}
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

void AsyncFSWebServer::handleFileDelete(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
	if (request->args() == 0) 
		return request->send(500, "text/plain", "BAD ARGS");
	String path = request->arg(0U);
	DEBUGLOG("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/")
		return request->send(500, "text/plain", "BAD PATH");
	if (!_fs->exists(path))
		return request->send(404, "text/plain", "FileNotFound");
	_fs->remove(path);
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}


int AsyncFSWebServer::handleHexFileUpload( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	int  _ret= 0;
	_hexFileUploadStatus = "";
	static File fsUploadFile;
	static size_t fileSize = 0;
	// Start
	if (!index) { 
		DEBUGLOG("handleHexFileUpload Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) filename = "/" + filename;
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOG("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOG("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) != len) {
			_hexFileUploadStatus  += "uploadstatus|error|div\n";
		}	
		else { 
			fileSize += len;	
		}
	}
	// End
	if (final) { 
		if (fsUploadFile) {	fsUploadFile.close();	}
		_ret = fileSize;
		DEBUGLOG("HexFileUpload final Size: %u\n", fileSize);
		_hexfileCheck = filename;
		_hexFileUploadStatus  += "uploadstatus|ok|div\n";
		_hexFileUploadStatus  += "file|"	 + _hexfileCheck 	+"|div\n";
		_hexFileUploadStatus  += "fileSize|" + (String)fileSize 	+"|div\n";
		fileSize = 0;
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
	return _ret;

}

void AsyncFSWebServer::handleHexFileUploadStatus(AsyncWebServerRequest *request) {
	request->send(200, "text/plain", _hexFileUploadStatus);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
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
		if (fsUploadFile.write(data, len) != len) {
			DBG_OUTPUT_PORT.println("Write error during upload");
		}
		else {
			fileSize += len;
		}
	}
	/*for (size_t i = 0; i < len; i++) {
	if (fsUploadFile)
	fsUploadFile.write(data[i]);
	}*/
	if (final) { // End
		if (fsUploadFile) {
			fsUploadFile.close();
		}
		DEBUGLOG("handleFileUpload Size: %u\n", fileSize);
		fileSize = 0;
	}
}

void AsyncFSWebServer::send_general_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	avrsip_err_t _res = avrprog.cfgFileStructGet( AVRISP_HexFiles_Web	);
	String values = "";
	values += "devicename|"   + (String)_generalConfig.deviceName  		+ "|input\n";
	values += "devicename2|"  + (String)_generalConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" + (String)_generalConfig.deviceSerial 	+ "|div\n";
	values += "deviceserial|" + (String)_generalConfig.deviceSerial 	+ "|input\n";
	values += "devicesign|"   	  +	AVRISP_HexFiles_Web.avr_signature	+ "|input\n";
	values += "deviceproj|"   	  + AVRISP_HexFiles_Web.project_name	+ "|input\n";
	values += "devicemem|" 	  +(String)AVRISP_HexFiles_Web.chipsize 	+ "|input\n";
	values += "versionapp|" + _Version_App + "|div\n";
	values += "versionweb|" + _Version_Web + "|div\n";
	values += "versiondatetime|" + _Version_BuildDate + " " + _Version_BuildTime + "|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_network_configuration_values_html(AsyncWebServerRequest *request, int _index) {
	
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
	values += "dhcp|" + (String)(_wifiConfig.dhcp ? "checked" : "") + "|chk\n";
	request->send(200, "text/plain", values);
	values = "";

	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_connection_state_values_html(AsyncWebServerRequest *request) {

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
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
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
	values += "x_chipid|" + (String)ESP.getChipId() + "|div\n";
	values += "x_sdk|" + (String)ESP.getSdkVersion() + "|div\n";
	values += "x_mhz|" + (String)ESP.getCpuFreqMHz() + "|div\n";

	request->send(200, "text/plain", values);
	//delete &values;
	values = "";
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");

}

String AsyncFSWebServer::getMacAddress() {
	uint8_t mac[6];
	char macStr[18] = { 0 };
	WiFi.macAddress(mac);
	sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	return  String(macStr);
}

void AsyncFSWebServer::send_NTP_configuration_values_html(AsyncWebServerRequest *request) {
	String values = "";
	values += "ntpserver0|" 	+ (String)_generalConfig.ntpServerName0 			+ "|input\n";
	values += "ntpserver1|" 	+ (String)_generalConfig.ntpServerName1 			+ "|input\n";
	values += "ntpserver2|" 	+ (String)_generalConfig.ntpServerName2 			+ "|input\n";

	values += "ntpserver0_d|" 	+ (String)_generalConfig.ntpServerName0 			+ "|div\n";
	values += "ntpserver1_d|" 	+ (String)_generalConfig.ntpServerName1 			+ "|div\n";
	values += "ntpserver2_d|" 	+ (String)_generalConfig.ntpServerName2 			+ "|div\n";

	values += "update|" 	+ (String)_generalConfig.updateNTPTimeEvery 			+ "|input\n";
	values += "tz|" 		+ (String)_generalConfig.timezone 						+ "|input\n";
	values += "dst|" 		+ (String)(_generalConfig.daylight ? "checked" : "") 	+ "|chk\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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

String AsyncFSWebServer::urldecode(String input) // (based on https://code.google.com/p/avr-netino/)
{
	char c;
	String ret = "";

	for (byte t = 0; t < input.length(); t++) {
		c = input[t];
		if (c == '+') c = ' ';
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

void AsyncFSWebServer::send_network_configuration_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	int _saveIn = 0;
	if (request->args() > 0)  // Save Settings
	{
		//String temp = "";
		bool oldDHCP = _wifiConfig.dhcp; // Save status to avoid general.html cleares it
		_wifiConfig.dhcp = false;
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
			if (request->argName(i) == "devicename") {
				_generalConfig.deviceName = urldecode(request->arg(i));
				_wifiConfig.dhcp = oldDHCP;
				continue;
			}
			if (request->argName(i) == "ssid") { _wifiConfig.ssid = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "password") { _wifiConfig.password = urldecode(request->arg(i)); continue; }
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
		ESP.restart();
#endif
		//ConfigureWifi();
		//AdminTimeOutCounter = 0;
	}
	else {
		DEBUGLOG(request->url().c_str());
		handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_general_configuration_html(AsyncWebServerRequest *request) {
	if (!checkAuth(request))
		return request->requestAuthentication();
		AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "devicename") 	{ _generalConfig.deviceName = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "deviceserial") 	{ _generalConfig.deviceSerial = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "devicesign") 	{ AVRISP_HexFiles_Web.avr_signature = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "deviceproj") 	{ AVRISP_HexFiles_Web.project_name = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "devicemem")  	{ AVRISP_HexFiles_Web.chipsize = request->arg(i).toInt();	continue; }
		}
		request->send_P(200, "text/html", Page_GeneralRefresh);
		save_configGeneral();
		avrprog.cfgFileLoadWeb(AVRISP_HexFiles_Web);
#if (NO_RST > 0)
		_fs->end();
		ESP.restart();
#endif
	}
	else {
		handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_NTP_configuration_html(AsyncWebServerRequest *request) {

	if (!checkAuth(request))
		return request->requestAuthentication();

	if (request->args() > 0)  // Save Settings
	{
		_generalConfig.daylight = false;
		//String temp = "";
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "ntpserver0") {
				_generalConfig.ntpServerName0 = urldecode(request->arg(i));
				 
				continue;
			}
			if (request->argName(i) == "ntpserver1") {
				_generalConfig.ntpServerName1 = urldecode(request->arg(i));
				 
				continue;
			}
			if (request->argName(i) == "ntpserver2") {
				_generalConfig.ntpServerName2 = urldecode(request->arg(i));
				
				continue;
			}
			if (request->argName(i) == "update") {
				_generalConfig.updateNTPTimeEvery = request->arg(i).toInt();
				NTP.setInterval(_generalConfig.updateNTPTimeEvery * 60);
				continue;
			}
			if (request->argName(i) == "tz") {
				_generalConfig.timezone = request->arg(i).toInt();
				  NTP.setTimeZone(_generalConfig.timezone / 10);
				continue;
			}
			if (request->argName(i) == "dst") {
				_generalConfig.daylight = true;
				DEBUGLOG("Daylight Saving: %d\r\n", _generalConfig.daylight);
				continue;
			}
		}
		NTP.setNtpServerName(_generalConfig.ntpServerName0.c_str());
		NTP.setDayLight(_generalConfig.daylight);
		NTPbeginReserv();
		save_configGeneral();

		//firstStart = true;

		setTime(NTP.getTime()); //set time
	}
	handleFileRead("/ntp.html", request);
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");

}

void AsyncFSWebServer::restart_esp(AsyncWebServerRequest *request) {
	request->send_P(200, "text/html", Page_IndexRefresh);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
	_fs->end(); // SPIFFS.end();
	delay(1000);
	ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	String values = "";

	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";

	request->send(200, "text/plain", values);

	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::send_wwwauth_configuration_html(AsyncWebServerRequest *request) {
	DEBUGLOG("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0)  // Save Settings
	{
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

	//DEBUGLOG(__PRETTY_FUNCTION__);
	//DEBUGLOG("\r\n");
}

bool AsyncFSWebServer::saveHTTPAuth() {
	//flag_config = false;
	DEBUGLOG("Save secret\r\n");
	DynamicJsonDocument jsonDoc(256);
	
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
	Serial.println(temp);
#endif // RELEASE
	serializeJson(jsonDoc, configFile);
	configFile.flush();
	configFile.close();
	return true;
}

void AsyncFSWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	String values = "";
	uint32_t maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	//bool updateOK = Update.begin(maxSketchSpace);
	bool updateOK = maxSketchSpace < ESP.getFreeSketchSpace();
	StreamString result;
	Update.printError(result);
	DEBUGLOG("--MaxSketchSpace: %d\r\n", maxSketchSpace);
	DEBUGLOG("--Update error = %s\r\n", result.c_str());
	values += "remupd|" + (String)((updateOK) ? "OK" : "ERROR") + "|div\n";

	if (Update.hasError()) {
		result.trim();
		values += "remupdResult|" + result + "|div\n";
	}
	else {
		values += "remupdResult||div\n";
	}

	request->send(200, "text/plain", values);
	DEBUGLOG(__FUNCTION__);
	DEBUGLOG("\r\n");
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	_browserMD5 = "";
	DEBUGLOG("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  // Read hash
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserMD5.c_str());
				continue;
			}if (request->argName(i) == "size") {
				_updateSize = request->arg(i).toInt();
				DEBUGLOG("Update size: %l\r\n", _updateSize);
				continue;
			}
		}
		request->send(200, "text/html", "OK --> MD5: " + _browserMD5);
	}

}

void AsyncFSWebServer::updateFirmware(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
	// handler for the file upload, get's the sketch bytes, and writes
	// them through the Update object
	static long totalSize = 0;
	if (!index) { //UPLOAD_FILE_START
		SPIFFS.end();
		Update.runAsync(true);
		DBG_OUTPUT_PORT.printf("Update start: %s\r\n", filename.c_str());
		uint32_t maxSketchSpace = ESP.getSketchSize();
		DBG_OUTPUT_PORT.printf("Max free scketch space: %u\r\n", maxSketchSpace);
		DBG_OUTPUT_PORT.printf("New scketch size: %u\r\n", _updateSize);
		if (_browserMD5 != NULL && _browserMD5 != "") {
			Update.setMD5(_browserMD5.c_str());
			DBG_OUTPUT_PORT.printf("Hash from client: %s\r\n", _browserMD5.c_str());
		}
		if (!Update.begin(_updateSize)) {//start with max available size
			Update.printError(DBG_OUTPUT_PORT);
		}

	}

	// Get upload file, continue if not start
	totalSize += len;
	DBG_OUTPUT_PORT.print(".");
	size_t written = Update.write(data, len);
	if (written != len) {
		DBG_OUTPUT_PORT.printf("len = %d, written = %l, totalSize = %l\r\n", len, written, totalSize);
		//Update.printError(DBG_OUTPUT_PORT);
		//return;
	}
	if (final) {  // UPLOAD_FILE_END
		String updateHash;
		DBG_OUTPUT_PORT.println("Applying update...");
		if (Update.end(true)) { //true to set the size to the current progress
			updateHash = Update.md5String();
			DBG_OUTPUT_PORT.printf("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
			DBG_OUTPUT_PORT.printf("Update Success: %u\nRebooting...\r\n", request->contentLength());
		}
		else {
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

	while (p < URL.length())
	{
		t = URL.indexOf("/", p);
		if (t >= 0)
		{
			name = URL.substring(p, t);
			p = t + 1;

		}
		else
		{
			name = URL.substring(p);
			p = URL.length();
		}
		if (name.substring(1, 2) == "_")
		{
			type = name.substring(0, 2);
			if (type == "i_")
			{
				type = "input";
			}
			else if (type == "d_")
			{
				type = "div";
			}
			else if (type == "c_")
			{
				type = "chk";
			}
			name = name.substring(2);
		}
		else
		{
			type = "input";
		}

		load_user_config(name, data);
		values += name + "|" + data + "|" + type + "\n";
	}
	request->send(200, "text/plain", values);
	values = "";

	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");


}


void AsyncFSWebServer::post_rest_config(AsyncWebServerRequest *request) {

	String target = "/";

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
		DEBUGLOG(request->argName(i).c_str());
		DEBUGLOG(" : ");
		DEBUGLOG(urldecode(request->arg(i)).c_str());

		//check for post redirect
		if (request->argName(i) == "afterpost")
		{
			target = urldecode(request->arg(i));
		}
		else  //or savedata in Json File
		{
			save_user_config(request->argName(i), request->arg(i));
		}
	}

	request->redirect(target);

	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");


}


void AsyncFSWebServer::serverInit() {
	//SERVER INIT
	//list directory
	on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileList(request);
	});
	//load editor
	on("/edit", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/edit.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	//create file
	on("/edit", HTTP_PUT, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileCreate(request);
	});	//delete file
	on("/edit", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleFileDelete(request);
	});
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/edit", HTTP_POST, [](AsyncWebServerRequest *request) { request->send(200, "text/plain", ""); }, 
		[this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
			this->handleFileUpload(request, filename, index, data, len, final);
	});

	on("/admin/devconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_values_html(request);
	});

	on("/admin/values/0", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request, 0);
	});

		on("/admin/values/1", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request, 1);
	});
	on("/admin/values/2", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request, 2);
	});
		on("/admin/values/3", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_values_html(request, 3);
	});

	on("/admin/connectionstate", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_connection_state_values_html(request);
	});
	on("/admin/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_information_values_html(request);
	});
	on("/admin/ntpvalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_values_html(request);
	});
	on("/config.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_network_configuration_html(request);
	});
	on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
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
				json += ",\"hidden\":" + String(WiFi.isHidden(i) ? "true" : "false");
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
	on("/general.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_general_configuration_html(request);
	});
	on("/ntp.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_NTP_configuration_html(request);
	});
	on("/admin/restart", [this](AsyncWebServerRequest *request) {
		DBG_OUTPUT_PORT.println(request->url());
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->restart_esp(request);
	});
	on("/admin/wwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_values_html(request);
	});
	on("/admin", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/admin.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_wwwauth_configuration_html(request);
	});
	on("/update/updatepossible", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->send_update_firmware_values_html(request);
	});
	on("/setmd5", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		//DBG_OUTPUT_PORT.println("md5?");
		this->setUpdateMD5(request);
	});
	on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (!this->handleFileRead("/update.html", request))
			request->send(404, "text/plain", "FileNotFound");
	});

	on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200, "text/html", (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting...");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
		this->_fs->end();
		ESP.restart();
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->updateFirmware(request, filename, index, data, len, final);
	});
	
	//first callback is called after the request has ended with all parsed arguments
	//second callback handles file uploads at that location
	on("/avr/info", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->avrGetInfo(request);
	});

	on("/avr/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		this->handleHexFileUpload( filename, index, data, len, final);
	});
	
	on("/avr/uploadstatus", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handleHexFileUploadStatus(request);
	});

	on("/avr/checkmeta", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->avrCheckFile(request);
	});

	on("/avr/flashrun", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->avrProg(request);
	});

	on("/avr/flashrollback", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->avrProgRollback(request);
	});

	on("/avr/flashstatus", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->avrProgStatus(request);
	});

	on("/rconfig", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->handle_rest_config(request);
	});

	on("/pconfig", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		this->post_rest_config(request);
	});


	on("/json", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (jsoncallback) { 
			this->jsoncallback(request);
		} 
		else {
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}
	});

	on("/rest", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (restcallback)
		{
			this->restcallback(request);
		}
		else
		{
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}

	});

	on("/post", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		if (postcallback)
		{
			this->postcallback(request);
		}
		else
		{
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}

	});

	//called when the url is not defined here
	//use it to load content from SPIFFS
	onNotFound([this](AsyncWebServerRequest *request) {
		DEBUGLOGFH("Not found: %s\r\n", request->url().c_str());
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(200);
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		if (!this->handleFileRead(request->url(), request))
			request->send(404, "text/plain", "FileNotFound");
		delete response; // Free up memory!
	});

	_evs.onConnect([](AsyncEventSourceClient* client) {
		DEBUGLOG("Event source client connected from %s\r\n", client->client()->remoteIP().toString().c_str());
	});
	addHandler(&_evs);

#define HIDE_SECRET
#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET

#ifdef HIDE_CONFIG
	on(CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});

	on(USER_CONFIG_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request))
			return request->requestAuthentication();
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
		json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
		json += "}";
		request->send(200, "text/json", json);
		json = String();
	});
	//server.begin(); --> Not here
	DEBUGLOG("HTTP server started\r\n");
}

bool AsyncFSWebServer::checkAuth(AsyncWebServerRequest *request) {
	if (!_httpAuth.auth) {
		return true;
	}
	else {
		return request->authenticate(_httpAuth.wwwUsername.c_str(), _httpAuth.wwwPassword.c_str());
	}

}

const char* AsyncFSWebServer::getHostName() {
	String hostname = _generalConfig.deviceName+"_"+_generalConfig.deviceSerial;
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

void AsyncFSWebServer::serialShowInfo() {
	Serial.printf("Ep8266 service chip firmware ver: %s\n\r",  VERSION_APP);
	Serial.printf("Ep8266 web pages ver: %s\n\r",  VERSION_WEB);
	Serial.printf("build DateTime: %s %s  \n\r",  __DATE__, __TIME__);
	Serial.printf("WifiHostName  %s \n\r", 	WiFi.hostname().c_str());
	Serial.printf("GotIP Address: %s \n", WiFi.localIP().toString().c_str());
	Serial.printf("Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
	Serial.printf("DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
	String hostname = _generalConfig.deviceName+"_"+_generalConfig.deviceSerial;
	Serial.printf("local DNS hostname  http://%s.local \n\r", hostname.c_str());
	Serial.printf("or you can connect directly  http://%s \n\r", WiFi.localIP().toString().c_str());
}