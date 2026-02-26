
#include "main.h"
#include "version.h"
#include <ArduinoJson.h>
#include "FSWebServerLib.h"


#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#endif

#if defined(ESP8266)
#include <FS.h>
#endif

#if defined(ESP32)
#include <ESPmDNS.h>
#elif defined(ESP8266)
#include <ESP8266mDNS.h>
#endif

#if defined(PROGTYPE_ISP)
#include "module_prog_isp.h"
#endif

#if defined(PROGTYPE_SWD)
#include "module_prog_swd/swd.h"
#endif

#if defined(MODULE_GPIO)
#include "module_gpio/module_gpio.h"
#endif

#if defined(MODULE_UDP)
#include "module_udp/module_udp.h"
#endif

#include "core_ntp/module_ntp.h"

#include "debug.h"

#include "core_editor/module_editor.h"
#include "core_ota/module_ota.h"
#include "core_json/module_json.h"
#include "core_wifi/module_wifi.h"



#include "common.h"

AsyncFSWebServer ESPHTTPServer(80);

String _Version_App 		= VERSION_APP;
String _Version_Web 		= VERSION_WEB;
String _Version_BuildDate 	= APP_BUILDDATE;
String _Version_BuildTime 	= APP_BUILDTIME;

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}

void flashLED(int pin, int times, int delayTime) {
	int oldState = digitalRead(pin);


	DEBUGLOGLED("---Flash LED during %d ms %d times. Old state = %d\r\n", delayTime, times, oldState);

	for (int i = 0; i < times; i++) {
		digitalWrite(pin, LOW); // Turn on LED
		delay(delayTime);
		digitalWrite(pin, HIGH); // Turn on LED
		delay(delayTime);
	}
	digitalWrite(pin, oldState); // Turn on LED
}

#if defined(ESP32)
    void AsyncFSWebServer::begin(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void AsyncFSWebServer::begin(FS* fs)                         // esp8266/esp32 flash file system
#endif
{
	_fs = fs;
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
		modWifiClass._apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", modWifiClass._apConfig.APenable);
	}

	if (CONNECTION_LED >= 0) {		digitalWrite(CONNECTION_LED, HIGH);	}
	// Turn LED off
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

	ModClassJson.setFs(&SPIFFS); // !!!MUST!!! be set as first as possible!

	loadHTTPAuth();
	if (!load_config_Sys()) { defaultConfigSys();  	}

	modWifiClass.begin(&SPIFFS); // wifi load cfg and set callback hooks
	
	//WIFI INIT start here
	String hostName = _sysConfig.deviceName + "_" + _sysConfig.deviceSerial;
	
	DEBUGLOG("Open http://");
	DEBUGLOG(hostName.c_str());
	DEBUGLOG(".local to see the device web page.\r\n");
	DEBUGLOG("Device serial number:");	DEBUGLOG(_sysConfig.deviceSerial.c_str());	DEBUGLOG("\n\r");
	if (!_sysConfig.deviceType.isEmpty()) {
		DEBUGLOG("Device type: ");	DEBUGLOG(_sysConfig.deviceType.c_str());	DEBUGLOG("\n\r");
	}
	#if defined(ESP32)
	DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipSize());
	#endif
	#if ESP8266
	DEBUGLOG("Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
	#endif
	DEBUGLOG("Scketch size: %u\r\n", 		ESP.getSketchSize());
	DEBUGLOG("Free flash space: %u\r\n", 	ESP.getFreeSketchSpace());
	
	
	AsyncWebServer::begin();
	serverInit(); // Configure and start Web server
	modWifiClass.webInit();
	
#if defined(MODULE_NTP)
	modNtpClass.begin();
	modNtpClass.webInit();
#endif

	
	
	String mdnsName = hostName;
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	
	modOtaClass.setFs(&SPIFFS);
	modOtaClass.begin(hostName, _httpAuth.wwwPassword );  //ConfigureOTA(_httpAuth.wwwPassword.c_str());
	modOtaClass.webInit();
	
	ModClassEdit.setFs(&SPIFFS);
	ModClassEdit.webInit();

#if defined(MODULE_GPIO)
	ModClassGpio.setFs(&SPIFFS);
	ModClassGpio.webInit();
#endif
	
	#ifdef PROGTYPE_SWD
		progSwd.setFs(&SPIFFS);
		progSwd.begin();
		progSwd.web_Init();
	#endif
	
	#ifdef PROGTYPE_ISP
		progIsp.setFs(&SPIFFS);
		progIsp.begin();
		progIsp.web_Init();
	#endif

	// ledInit();
}


//duplicate config stuff for user level config items

bool AsyncFSWebServer::load_config_Sys() {
	JsonDocument jsonDoc;
	if (!ModClassJson.load_jsonDoc(CONFIG_FILE_SYS, jsonDoc)){	return false;	}
	_sysConfig.deviceName 			= jsonDoc["deviceName"].as<const char *>();
	_sysConfig.deviceSerial 		= jsonDoc["deviceSerial"].as<const char *>();
	_sysConfig.deviceType 			= jsonDoc["deviceType"].as<const char *>();
	return true;
}

void AsyncFSWebServer::defaultConfigSys() {
	// DEFAULT CONFIG SYSTEM
	_sysConfig.deviceName 		= "esp_server";
	_sysConfig.deviceSerial 	= SERIAL_NUMBER;
	_sysConfig.deviceType 		= DEVMODULE_GPIO;
	//_sysConfig.connectionLed = CONNECTION_LED;
	save_configSys();
}

bool AsyncFSWebServer::save_configSys() {
	DEBUGLOG("Save config SYSTEM\r\n");
	JsonDocument jsonDoc;
	jsonDoc["deviceName"] 	= _sysConfig.deviceName;
	jsonDoc["deviceSerial"] = _sysConfig.deviceSerial;
	jsonDoc["deviceType"] 	= _sysConfig.deviceType;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_FILE_SYS);
}


bool AsyncFSWebServer::loadHTTPAuth() {
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	JsonDocument jsonDoc;
	if (!ModClassJson.load_jsonDoc(SECRET_FILE, jsonDoc)){
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
	return true;
}

// working with pages vvv
void AsyncFSWebServer::send_information_values_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";


	
	#ifdef ESP32
	values += "x_chipid|" 	+ (String)ESP.getChipModel() + "|div\n";
	#elif defined(ESP8266)
	values += "x_chipid|" + (String)ESP.getChipId() + "|div\n";
	#endif
	values += "x_sdk|" + (String)ESP.getSdkVersion() + "|div\n";
	values += "x_mhz|" + (String)ESP.getCpuFreqMHz() + "|div\n";

	request->send(200, "text/plain", values);
	//delete &values;
	values = "";

}

void AsyncFSWebServer::restart_esp() {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	modWifiClass.wifiStatus = FS_STAT_RESET;
	WiFi.disconnect(true, false);
	_fs->end();
	delay(1000);
	ESP.restart();
}

void AsyncFSWebServer::send_wwwauth_configuration_values_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "wwwauth|" + (String)(_httpAuth.auth ? "checked" : "") + "|chk\n";
	values += "wwwuser|" + (String)_httpAuth.wwwUsername + "|input\n";
	values += "wwwpass|" + (String)_httpAuth.wwwPassword + "|input\n";

	request->send(200, "text/plain", values);

}

void AsyncFSWebServer::set_wwwauth_configuration(AsyncWebServerRequest *request)	{
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	DEBUGLOG("%s %d\n", __FUNCTION__, request->args());
	if (request->args() > 0)	{
		bool save	   = false;
		_httpAuth.auth = false;
		for (uint8_t i = 0; i < request->args(); i++)		{
			if (request->argName(i) == "authconf") {
				save = true;
				continue;
			}
			if (request->argName(i) == "wwwuser") {
				_httpAuth.wwwUsername = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwpass") {
				_httpAuth.wwwPassword = urldecode(request->arg(i));
				continue;
			}
			if (request->argName(i) == "wwwauth") {
				_httpAuth.auth = true;
				continue;
			}
		}
		if (!_httpAuth.auth)		{
			_httpAuth.wwwUsername = "";
			_httpAuth.wwwPassword = "";
		}

		if (save)		{
			request->send_P(200, "text/html", Page_GeneralSys);
			saveHTTPAuth();
		}
	}
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


bool AsyncFSWebServer:: handleFileRead(String path, AsyncWebServerRequest *request) {
	DEBUGEDIT("handleFileRead: %s\r\n", path.c_str());
	if (CONNECTION_LED >= 0) {	flashLED(CONNECTION_LED, 1, 25); 	}	// Show activity on LED
	// CANNOT RUN DELAY() INSIDE CALLBACK
	if (path.endsWith("/")) {	path += HTML_INDEX;	}
	String contentType = getContentType(path, request);
	String pathWithGz = path + ".gz";
	if (_fs->exists(pathWithGz) || _fs->exists(path)) {
		if (_fs->exists(pathWithGz)) { path += ".gz"; }
		DEBUGEDIT("Content type: %s\r\n", contentType.c_str());
		AsyncWebServerResponse *response = request->beginResponse(*_fs, path, contentType);
		if (path.endsWith(".gz"))
			response->addHeader("Content-Encoding", "gzip");
		//File file = SPIFFS.open(path, "r");
		DEBUGEDIT("File %s exist\r\n", path.c_str());
		request->send(response);
		DEBUGEDIT("File %s Sent\r\n", path.c_str());
		return true;
	}
	else
		DEBUGEDIT("Cannot find %s\n", path.c_str());
	return false;
}

// *.html vvv
void AsyncFSWebServer::send_system_version_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";
	values += "devicename|"  	+ _sysConfig.deviceName  		+ "|div\n";
	values += "deviceserial|" 	+ _sysConfig.deviceSerial 		+ "|div\n";
	values += "devicetype|" 	+ _sysConfig.deviceType 		+ "|div\n";
	values += "versionapp|" 	+ _Version_App + "|div\n";
	values += "versionweb|" 	+ _Version_Web + "|div\n";
	
	//values += "versiondatetime|" + _Version_BuildDate + " " + _Version_BuildTime + "|div\n";

	values += "gitbranch|" ;values += GIT_BRANCH ;values += "|div\n";
	values += "gitcommit|" ;values += GIT_COMMIT ;values += "|div\n";
	values += "buildenv|" ;values += BUILD_ENV ;values += "|div\n";
	values += "versiondatetime|" ;values += BUILD_TIME ;values += "|div\n";
	

	request->send(200, "text/plain", values);
}
// *.html ^^^


// project.html vvv
void AsyncFSWebServer::send_project_configuration_values_html(AsyncWebServerRequest *request) { // answer for "get" request
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	String values = "";

	// CfgFile_ProgSwd_t Prog_CfgFile;
	// int _res = progSwd.cfg_FileStructGet(Prog_CfgFile);
	// values += "progproj|"	+ 		 Prog_CfgFile.project_name			+ "|input\n";
	// values += "progmem|"	+(String)Prog_CfgFile.chip_size 	+ "|input\n";

	request->send(200, "text/plain", values);
}

void AsyncFSWebServer::get_project_configuration_html(AsyncWebServerRequest *request) {
	// CfgFile_ProgSwd_t Prog_CfgFile;
	if (request->args() > 0) { // Save Settings
		// for (uint8_t i = 0; i < request->args(); i++) {
		// 	DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
		// 	// if (request->argName(i) == "devicesign") 		{ AVRISP_HexFiles_Web.avr_signature = urldecode(request->arg(i));	continue; }
		// 	if (request->argName(i) == "progproj") 		{ Prog_CfgFile.project_name = urldecode(request->arg(i));	continue; }
		// 	if (request->argName(i) == "progmem")  		{ Prog_CfgFile.chip_size = request->arg(i).toInt();			continue; }
		// }
		// request->send_P(200, "text/html", Page_GeneralPrj);
		// progSwd.cfg_FileSaveFromWeb(Prog_CfgFile);
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
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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
	
}
// system.html ^^^

String getContentType(String filename, AsyncWebServerRequest *request) {
	if 	(request->hasArg("download")) 		{return "application/octet-stream";}
	else if (filename.endsWith(".htm"))  	{return "text/html";}
	else if (filename.endsWith(".html")) 	{return "text/html";}
	else if (filename.endsWith(".css")) 	{return "text/css";}
	else if (filename.endsWith(".js"))   	{return "application/javascript";}
	else if (filename.endsWith(".json")) 	{return "application/json";}
	else if (filename.endsWith(".png")) 	{return "image/png";}
	else if (filename.endsWith(".gif")) 	{return "image/gif";}
	else if (filename.endsWith(".jpg")) 	{return "image/jpeg";}
	else if (filename.endsWith(".ico")) 	{return "image/x-icon";}
	else if (filename.endsWith(".xml")) 	{return "text/xml";}
	else if (filename.endsWith(".pdf")) 	{return "application/x-pdf";}
	else if (filename.endsWith(".zip")) 	{return "application/x-zip";}
	else if (filename.endsWith(".gz"))  	{return "application/x-gzip";}
	else if (filename.endsWith(".hex")) 	{return "text/html";} // TODO ??
	return "text/plain";
}

void AsyncFSWebServer::serverInit() {
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

	on("/system/infovalues", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_information_values_html(request);
	});	
	on("/system/version", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_system_version_html(request);
	});	
	on("/system.html", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->get_system_configuration_html(request);
	});	
	// FIXME
	on("/system/savewwwauth", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->set_wwwauth_configuration(request);
	});	
	on("/system/devconf", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		this->send_device_values_html(request);
	});	

//system.html ^^^

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


#ifdef HIDE_SECRET
	on(SECRET_FILE, HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		AsyncWebServerResponse *response = request->beginResponse(403, "text/plain", "Forbidden");
		response->addHeader("Connection", "close");
		response->addHeader("Access-Control-Allow-Origin", "*");
		request->send(response);
	});
#endif // HIDE_SECRET


#ifdef HIDE_CONFIG
	on(CONFIG_FILE_SYS, HTTP_GET, [this](AsyncWebServerRequest *request) {
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

const String AsyncFSWebServer::getHostName() {
	return _sysConfig.deviceName+"_"+_sysConfig.deviceSerial;
}

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


