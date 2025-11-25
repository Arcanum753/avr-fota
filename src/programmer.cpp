#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "main.h"

#ifdef ESP32
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include "debug.h"
#include "FSWebServerLib.h"
#include "ntp_mod.h"
#include "programmer.h"
#include "prog_isp.h"
#include "prog_swd.h"


ESP_Programmer espProgrammer(0);
ESP_Programmer::ESP_Programmer(uint8_t in): _in(in){ }

#if ESP32
    void ESP_Programmer::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void ESP_Programmer::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool ESP_Programmer::begin (){
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {         cfg_FileSave();    }
	if (progType == DEVTYPE_AVR) {
		avrprog.setReset(false);  // let the AVR chip run by level up RST pin.
		avrprog.begin(); 		  // load AVR isp cfg's
	}
	if (progType == DEVTYPE_SWD) {	swdprog.stm32Fx_begin();	}
    return true;
}

void  ESP_Programmer::webInit()	{
	//stm32.html vvv
	ESPHTTPServer.on("/prog/diskinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		programmerGetDiskInfo (request);
	});

	ESPHTTPServer.on("/prog/fileslist", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		programmerGetFilesList (request);
	});

	ESPHTTPServer.on("/prog/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		programmerFileDelete(request);
	});

	ESPHTTPServer.on("/prog/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		programmerFileUpload2FS( filename, index, data, len, final);
	});

	ESPHTTPServer.on("/prog/uploadstat", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		programmerFileUpload2FSStat(request);
	});
	ESPHTTPServer.on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		programmerFileUpload2Chip(request);
	});
//stm32.html ^^^

//avr.html vvv
//first callback is called after the request has ended with all parsed arguments
//second callback handles file uploads at that location
	ESPHTTPServer.on("/avr/info", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		avrGetActualFWInfo(request);
	});
	ESPHTTPServer.on("/avr/flashrun", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		avrProg(request);
	});
	ESPHTTPServer.on("/avr/flashstatus", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		avrProgStatus(request);
	});
	ESPHTTPServer.on("/avr/fuseread", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		avrFusesRead(request);
	});
	ESPHTTPServer.on("/avr/fusewrite", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		avrWebFusesWrite(request);
	});
//avr.html ^^^

}

int ESP_Programmer::cfg_FileSaveFromWeb(Prog_CfgFile_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	_Prog_CfgFile	=  _inStruct;
	int _ret =  (int)cfg_FileSave();
	return _ret ;
}

int  ESP_Programmer::cfg_FileStructGet(Prog_CfgFile_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    progerr_t _ret = ERROR_OK;
    if(!cfg_FileLoad()) {  return ERR_CFG; }
    _inStruct = _Prog_CfgFile;
    return _ret ;
}

void ESP_Programmer::cfg_SetDefault() {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	// _Prog_CfgFile.programmer_type	= DEFAULT_PROG_TYPE;
    _Prog_CfgFile.project_name  	= DEFAULT_PROG_PROJNAME;
    _Prog_CfgFile.chip_size      	= DEFAULT_chipsize;
}

bool ESP_Programmer::cfg_FileLoad() {
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
	JsonDocument jsonDoc;
	if (!load_jsonDoc(CONFIG_PROG_JSON, jsonDoc)){	return false;	}
	// _Prog_CfgFile.programmer_type	= jsonDoc["type"].as<const char *>();
    _Prog_CfgFile.project_name		= jsonDoc["project"].as<const char *>();
    _Prog_CfgFile.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
	return true;
}

bool ESP_Programmer::cfg_FileSave(){
	DEBUGLOG("Save config PROJ\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= _Prog_CfgFile.programmer_type;
    jsonDoc["project"]		= _Prog_CfgFile.project_name;
    jsonDoc["chipsize"]     = _Prog_CfgFile.chip_size;
	return save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}

String ESP_Programmer::formatBytes(size_t bytes) {
	if (bytes < 1024) 					{	return String(bytes) + "B";	}
	else
	if (bytes < (1024 * 1024))			{	return String(bytes / 1024.0) + "KB";	}
	else
	if (bytes < (1024 * 1024 * 1024))	{	return String(bytes / 1024.0 / 1024.0) + "MB";	}
	else	{	return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";	}
}

bool ESP_Programmer::web_GetDiskInfo(String &_str)	{
	bool _ret = true;

	String values 	= 	"";
	size_t sizeAll	=	0;
	size_t sizeUsed	=	0;
	#if ESP32
	 sizeAll	=	_fs->totalBytes();
	 sizeUsed	=	_fs->usedBytes();
	#elif defined(ESP8266)

	#endif

	size_t sizeFree = 0;

	if (sizeAll > sizeUsed ){ sizeFree = sizeAll - sizeUsed;	}

	values	+= "diskall|"	+ (String)(sizeAll)		+"|div\n";
	values	+= "diskused|"	+ (String)(sizeUsed) 	+"|div\n";
	values	+= "diskfree|"	+ (String)(sizeFree) 	+"|div\n";
	_str = values;
	return _ret;
}

bool ESP_Programmer::web_GetFileList(String &_str)	{
	bool _ret = true;

	size_t pos ;
	std::string fname = "";
	std::string ftype = "";
    uint32_t i = 0;

	// AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	// int _res = avrprog.cfgFileStructGet( AVRISP_HexFiles_Web	);

	String json = "[";
#if defined(ESP8266)
    if (!_fs) { _fs->begin();  }// If SPIFFS is not started
    Dir files = _fs->openDir("/");
    while (files.next()) {
        fname = files.fileName().c_str() ;
        pos = fname.find_last_of(FILE_TYPE_COMMA);
        ftype = fname.substr(pos + 1);
		if ((ftype == FILE_TYPE_HEX) || (ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)  ) {
			size_t fsize = files.fileSize();
            if (i) json += ",";
			json += "{";
			json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
			json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
			json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
			json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
			json += ",\"progchip\":\"";									json += "\"";
			json += ",\"progactual\":\"";								json += "\"";
			json += ",\"progdate\":\"";									json += "\"";
			json += "}";
			i++;
        }
    }
#endif
#if defined(ESP32)
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
    File root =  _fs->open("/");
    File files = root.openNextFile();
    while (files) {
        fname = files.name() ;
        pos = fname.find_last_of(FILE_TYPE_COMMA);
        ftype = fname.substr(pos + 1);
        if ((ftype == FILE_TYPE_HEX) || (ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)  ) {
			size_t fsize = files.size();
			if (i) json += ",";
			json += "{";
			json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
			json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
			json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
			json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
			json += ",\"progchip\":\"";									json += "\"";
			json += ",\"progactual\":\"";								json += "\"";
			json += ",\"progdate\":\"";									json += "\"";
			json += "}";
			i++;
        }
        files = root.openNextFile();
    }
#endif
	json += "]";

	_str = json;
	return _ret;
}

bool ESP_Programmer::save_jsonDoc(const JsonDocument& jsonDoc,	const String& file) {
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
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
bool ESP_Programmer::load_jsonDoc(const String& file,	JsonDocument& jsonDoc){
	if (!_fs) { _fs->begin();  }// If SPIFFS is not started
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

void ESP_Programmer::prog_ProgTypeSet(String _str)	{
	progType = _str;
}

int  ESP_Programmer::prog_Programm(String _path, String _fwTime)	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
	DEBUGLOGISP(" file %s time %s \r\n", _path.c_str(), _fwTime.c_str());
	int _res = ERR_OPENFILE;

	if (progType == DEVTYPE_AVR) {
		_res  = avrprog.avr_ChipProgrammMain(_path, _fwTime );
		DEBUGLOG("avrProg  %d \n\r", _res);
	}
	if (progType == DEVTYPE_SWD) {
		_res  = swdprog.stm32_ChipProgrammMain(_path );
	}

	DEBUGLOGISP("Programming end \r\n");
	return _res;
}


// avr.html vvv
void  ESP_Programmer::avrGetActualFWInfo(AsyncWebServerRequest *request) {
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

void  ESP_Programmer::avrProg(AsyncWebServerRequest *request) {
	String values = "";
	DEBUGLOG("_hexfilename  %s \n\r", _hexfileProg.c_str()); // что программируем
	// int _res  = avrprog.avr_ChipProgrammMain(_hexfileProg, NTP.getTimeDateString() );
	int _res  = espProgrammer.prog_Programm(_hexfileProg,  NTP.getTimeDateString());

	DEBUGLOG("avrProg  %d \n\r", _res);
	values	+= "avrprogres|"+(String) _res+"|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}


void  ESP_Programmer::avrProgStatus(AsyncWebServerRequest *request) {
	String values = "";
 	values += "avrprogver|" ;
	values += avrprog.chipFlashVerificationResultGet() ;
	values += "|div\n";

	request->send(200, "text/plain", values);
	DEBUGLOG(__PRETTY_FUNCTION__);	DEBUGLOG("\r\n");
}

void  ESP_Programmer::avrFusesRead(AsyncWebServerRequest *request) {
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


void  ESP_Programmer::avrWebFusesWrite(AsyncWebServerRequest *request) {
		// AVRISP_fuses_t AVRISP_fuses ;
	String s_high = "";  	uint8_t high = 0;
	String s_low  = "";		uint8_t low  = 0;
	String s_lock = "";		uint8_t lock = 0;
	String s_ext  = "";		uint8_t ext  = 0;
	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOG("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "avrfusehigh") 	{ s_high = ESPHTTPServer.urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuselow") 	{ s_low  = ESPHTTPServer.urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseprot") 	{ s_lock = ESPHTTPServer.urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseext") 	{ s_ext  = ESPHTTPServer.urldecode(request->arg(i));	continue; }
		}
		request->send_P(200, "text/html", Page_AvrRefresh);

		high =	hex2bin(s_high[0]);
		if (s_high[1])     	high = (high<<4) + 	hex2bin(s_high[1]);
		low =	hex2bin(s_low[0]);
    	if (s_low[1])     	low = (low<<4) + 	hex2bin(s_low[1]);
		lock =	hex2bin(s_lock[0]);
    	if (s_lock[1])     	lock = (lock<<4) + 	hex2bin(s_lock[1]);
		ext =	hex2bin(s_ext[0]);
    	if (s_ext[1])     	ext = (ext<<4) + 	hex2bin(s_ext[1]);

		avrprog.chipFusesWrite(high, low, lock, ext);
	}
	else {
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
	DEBUGLOG(__PRETTY_FUNCTION__);
	DEBUGLOG("\r\n");
}
// avr.html ^^^


// stm32.html vvv
void ESP_Programmer::programmerGetFilesList (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String json = "";
	espProgrammer.web_GetFileList(json);
	request->send(200, "text/json", json);
	json = "";
    DEBUGLOGISP("List of *.hex *.bin *.binary files: %s \n\r", json);
}

void ESP_Programmer::programmerGetDiskInfo (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String values = "";
	espProgrammer.web_GetDiskInfo(values);
	request->send(200, "text/json", values);
	values = "";
    DEBUGLOGISP("Disk info: %s \n\r", values);
}

void ESP_Programmer::programmerFileDelete(AsyncWebServerRequest *request) {
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
int ESP_Programmer::programmerFileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
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


void ESP_Programmer::programmerFileUpload2FSStat(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void ESP_Programmer::programmerFileUpload2Chip(AsyncWebServerRequest *request) {
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = ESPHTTPServer.urldecode(request->arg(i));	continue; }
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


// TODO Insert to Logseq "Common.h" page
/*
 * hex2bin
 * Turn a Hex digit (0..9, A..F) into the equivalent binary value (0-16)
 * returns 0xFF if bad hex digit.
 */
uint8_t ESP_Programmer::hex2bin (uint8_t h)    {
    if (h >= '0' && h <= '9')
       { return(h - '0'); }
    if (h >= 'A' && h <= 'F')
       { return((h - 'A') + 10); }
	if (h >= 'a' && h <= 'f')
       { return((h - 'a') + 10); }
    DEBUGLOGISP("Bad hex digit! %x \n\r", h);
    return 0xff;
}



