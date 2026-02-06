#include "main.h"
#include <ArduinoJson.h>

#include "FSWebServerLib.h"

#include "debug.h"
#include "module_wifi.h"
#include "module_udp.h"
#include "module_ntp.h"

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
#include "module_prog_swd.h"
#endif


#include "common.h"


AsyncFSWebServer ESPHTTPServer(80);

String _Version_App 		= VERSION_APP;
String _Version_Web 		= VERSION_WEB;
String _Version_BuildDate 	= APP_BUILDDATE;
String _Version_BuildTime 	= APP_BUILDTIME;

AsyncFSWebServer::AsyncFSWebServer(uint16_t port) : AsyncWebServer(port) {}




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
		wifiModClass._apConfig.APenable = !digitalRead(AP_ENABLE_BUTTON); // Read AP button. If button is pressed activate AP
		DEBUGLOG("AP Enable = %d\n", wifiModClass._apConfig.APenable);
	}

	if (CONNECTION_LED >= 0) {
		digitalWrite(CONNECTION_LED, HIGH);
		 // Turn LED off
	}
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

	wifiModClass.begin(&SPIFFS); // wifi load cfg and set callback hooks

	
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
	ntpModClass.ntpBegin();
	wifiModClass.webInit();
	ntpModClass.webInit();
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

	String mdnsName = _sysConfig.deviceName + "_" + _sysConfig.deviceSerial;
	MDNS.begin(mdnsName.c_str()); // I've not got this to work. Need some investigation.
	MDNS.addService("http", "tcp", 80);
	prepareSizesForUpdate();
	ConfigureOTA(_httpAuth.wwwPassword.c_str());
	// ledInit();
	// progSwd.begin();
}

void AsyncFSWebServer::showDBG() {

}


//duplicate config stuff for user level config items

bool AsyncFSWebServer::load_config_Sys() {
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_FILE_SYS, jsonDoc)){
		return false;
	}
	_sysConfig.deviceName 			= jsonDoc["deviceName"].as<const char *>();
	_sysConfig.deviceSerial 		= jsonDoc["deviceSerial"].as<const char *>();
	_sysConfig.deviceType 			= jsonDoc["deviceType"].as<const char *>();
	return true;
}


bool AsyncFSWebServer::load_jsonDoc(const String& file, JsonDocument& jsonDoc){
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
	if ( buf == NULL){ return false;	}
	DEBUGLOGFH("File: %s, size: %d\r\n", file.c_str(), size);
	configFile.readBytes(buf, size);
	configFile.close();
	auto error = deserializeJson(jsonDoc, buf);
	free(buf);
	if (error) {
		DEBUGLOG("Failed to parse config file. Error: %s\r\n", error.c_str());
		return false;
	}
	return true;
}



void AsyncFSWebServer::defaultConfigSys() {
	// DEFAULT CONFIG SYSTEM
	_sysConfig.deviceName 		= "esp_server";
	_sysConfig.deviceSerial 	= SERIAL_NUMBER;
	_sysConfig.deviceType 		= DEVTYPE_GPIO;
	//_sysConfig.connectionLed = CONNECTION_LED;
	save_configSys();
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

bool AsyncFSWebServer::save_configSys() {
	DEBUGLOG("Save config SYSTEM\r\n");
	JsonDocument jsonDoc;
	jsonDoc["deviceName"] 	= _sysConfig.deviceName;
	jsonDoc["deviceSerial"] = _sysConfig.deviceSerial;
	jsonDoc["deviceType"] 	= _sysConfig.deviceType;
	return save_jsonDoc(jsonDoc, CONFIG_FILE_SYS);
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
	
	if (request->args() == 0)		{	return request->send(500, "text/plain", "BAD ARGS");}
	String path = request->arg(0U);
	DEBUGLOG("handleFileCreate: %s\r\n", path.c_str());
	if (path == "/")			{	return request->send(500, "text/plain", "BAD PATH");	}
	if (_fs->exists(path))		{	return request->send(500, "text/plain", "FILE EXISTS");	}
	File file = _fs->open(path, "w");
	if (file)	{	file.close();	}
	else		{	return request->send(500, "text/plain", "CREATE FAILED");	}
	request->send(200, "text/plain", "");
	path = String(); // Remove? Useless statement?
}


// удаление файла

void AsyncFSWebServer::handleFileDelete(AsyncWebServerRequest *request) {
	
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = request->arg(0U);
	DEBUGLOG("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/") 		{	return request->send(500, "text/plain", "BAD PATH");	}
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
	wifiModClass.wifiStatus = FS_STAT_RESET;
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

void
AsyncFSWebServer::set_wwwauth_configuration(AsyncWebServerRequest *request)	{
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

void AsyncFSWebServer::prepareSizesForUpdate (){
	maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	freeSketchSpace  = ESP.getFreeSketchSpace();
	DEBUGLOG("Update prepare size \n\r");
}

void AsyncFSWebServer::send_update_firmware_values_html(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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
	if (typeOTAfile == UNSUPPORTED) {	updateFiletype = OTA_UNSUPPORTED;	}

	bool updateOK = maxSketchSpace < freeSketchSpace;
	if (updateOK == true) {	updateOKstr = "OK" ; } 
		else {	updateOKstr = "ERROR" ;	}

	DEBUGLOG("--updateOK: %s\r\n", updateOKstr);
	DEBUGLOG("--FreeSketchSpace: %d\r\n", freeSketchSpace);
	DEBUGLOG("--MaxSketchSpace: %d\r\n", maxSketchSpace);
	DEBUGLOG("--UpdateFiletype: %d\r\n", updateFiletype);

	values += "upd|" 			+ updateOKstr 				+ "|div\n";
	values += "updSizeFree|" 	+ (String)freeSketchSpace 	+ "|div\n";
	values += "updSizeMax|" 	+ (String)maxSketchSpace  	+ "|div\n";
	values += "updFileType|" 	+ updateFiletype		  	+ "|div\n";
	request->send(200, "text/plain", values);
}

void AsyncFSWebServer::setUpdateMD5(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
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
		}
		else	{
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
void AsyncFSWebServer::send_system_version_values_html(AsyncWebServerRequest *request) { // answer for "get" request
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
	//DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	
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
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MISO, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MISO, LOW);	}
					continue;
				}
				if (request->argName(i) == "led2")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_MOSI, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_MOSI, LOW);	}
					continue;
				}
				if (request->argName(i) == "led3")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_SCK, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_SCK, LOW);		}
					continue;
				}
				if (request->argName(i) == "led4")	{
					// if (urldecode(request->arg(i)) == "on")  {	digitalWrite(PIN_RST, HIGH);	}
					// if (urldecode(request->arg(i)) == "off") {	digitalWrite(PIN_RST, LOW);	}
					continue;
				}
			}
		}
		request->send(200, "text/plain", values);
		DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
	}
}

// gpio.html ^^^


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
		this->send_system_version_values_html(request);
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
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (restcallback)		{
			this->restcallback(request);
		}	else	{
			String values = "";
			request->send(200, "text/plain", values);
			values = "";
		}

	});

	on("/post", [this](AsyncWebServerRequest *request) {
		if (!this->checkAuth(request)) {	return request->requestAuthentication(); };
		if (postcallback)	{
			this->postcallback(request);
		}	else	{
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

const String AsyncFSWebServer::getHostName() {
	return _sysConfig.deviceName+"_"+_sysConfig.deviceSerial;
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



// TODO РАСПИХАТЬ УДАЛЕНИЕ ПО МОДУЛЯМ
// // Function to delete all CFG files
// void AsyncFSWebServer::clearConfig(bool reset)	{
// 	if (_fs->exists(CONFIG_FILE_SYS)) 	{ _fs->remove(CONFIG_FILE_SYS);	}
// 	if (_fs->exists(CONFIG_FILE_UDP)) 	{ _fs->remove(CONFIG_FILE_UDP);	}
// 	// if (_fs->exists(CONFIG_FILE_NTP)) 	{ _fs->remove(CONFIG_FILE_NTP);	}
// 	if (_fs->exists(WIFI_CONFIG_FILE0)) { _fs->remove(WIFI_CONFIG_FILE0);	}
// #if (USE_RESERV_WIFI > 0)
// 	if (_fs->exists(WIFI_CONFIG_FILE1)) { _fs->remove(WIFI_CONFIG_FILE1);	}
// 	if (_fs->exists(WIFI_CONFIG_FILE2)) { _fs->remove(WIFI_CONFIG_FILE2);	}
// 	if (_fs->exists(WIFI_CONFIG_FILE3)) { _fs->remove(WIFI_CONFIG_FILE3);	}
// #endif
// 	if (_fs->exists(SECRET_FILE)) {		_fs->remove(SECRET_FILE);	}
// 	if (reset) {
// 		if (_fs) { _fs->end();  }// If SPIFFS is started - finish it.
// 		ESPHTTPServer.restart_esp();
// 	}
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


