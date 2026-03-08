
#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef ESP32
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include "FSWebServerLib.h"
#include "debug.h"



#include "core_ntp/module_ntp.h"




#include "core_json/module_json.h"

#include "prog_swd.h"
#include "module_prog_swd.h"
#include "common.h"

Class_ProgSwd progSwd(0);
Class_ProgSwd::Class_ProgSwd(uint8_t in): _in(in){ }

#if ESP32
    void Class_ProgSwd::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void Class_ProgSwd::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool Class_ProgSwd::begin (){
    DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {	cfg_FileSave();	}
	swdprog.stm32Fx_begin();
	//TODO return init result
    return true;
}


// TODO навести тут порядок с именаяи GET/POST запросов.
// all about webAPI. Set hooks
void  Class_ProgSwd::web_Init()	{
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	//stm32.html vvv
	ESPHTTPServer.on("/prog/diskinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_GetDiskInfoExe (request);
	});

	ESPHTTPServer.on("/prog/fileslist", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_GetFilesList (request);
	});

	ESPHTTPServer.on("/prog/delete", HTTP_DELETE, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileDelete(request);
	});

	ESPHTTPServer.on("/prog/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		web_FileUpload2FS( filename, index, data, len, final);
	});

	ESPHTTPServer.on("/prog/uploadstat", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUpload2FS_Status(request);
	});
	ESPHTTPServer.on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUpload2Chip(request);
	});
//stm32.html ^^^

}


// cfg section
int Class_ProgSwd::cfg_FileSaveFromWeb(CfgFile_ProgSwd_t &_inStruct)  {
    DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	CfgFile_ProgSwd	=  _inStruct;
	int _ret =  (int)cfg_FileSave();
	return _ret ;
}

int  Class_ProgSwd::cfg_FileStructGet(CfgFile_ProgSwd_t &_inStruct)  {
    DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
    progerr_t _ret = ERROR_OK;
    if(!cfg_FileLoad()) {  return ERR_CFG; }
    _inStruct = CfgFile_ProgSwd;
    return _ret ;
}

void Class_ProgSwd::cfg_SetDefault() {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	// CfgFile_ProgSwd.programmer_type	= DEFAULT_PROG_TYPE;
    CfgFile_ProgSwd.project_name  	= DEFAULT_PROG_PROJNAME;
    CfgFile_ProgSwd.chip_size      	= DEFAULT_chipsize;
}

bool Class_ProgSwd::cfg_FileLoad() {
	DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(CONFIG_PROG_JSON, jsonDoc) == false ){	return false;	}
	// CfgFile_ProgSwd.programmer_type	= jsonDoc["type"].as<const char *>();
    CfgFile_ProgSwd.project_name		= jsonDoc["project"].as<const char *>();
    CfgFile_ProgSwd.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
	return true;
}

bool Class_ProgSwd::cfg_FileSave(){
	DEBUGLOG("Save config PROJ\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= CfgFile_ProgSwd.programmer_type;
    jsonDoc["project"]		= CfgFile_ProgSwd.project_name;
    jsonDoc["chipsize"]     = CfgFile_ProgSwd.chip_size;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}


bool Class_ProgSwd::web_GetDiskInfoExe(String &_str)	{
	bool _ret = true;
	String values 	= 	"";
	size_t sizeAll	=	0;
	size_t sizeUsed	=	0;
	#if ESP32
	 sizeAll	=	_fs->totalBytes();
	 sizeUsed	=	_fs->usedBytes();
	#elif defined(ESP8266)
	// FIXME
	#endif

	size_t sizeFree = 0;

	if (sizeAll > sizeUsed ){ sizeFree = sizeAll - sizeUsed;	}

	values	+= "diskall|"	+ (String)(sizeAll)		+"|div\n";
	values	+= "diskused|"	+ (String)(sizeUsed) 	+"|div\n";
	values	+= "diskfree|"	+ (String)(sizeFree) 	+"|div\n";
	_str = values;
	return _ret;
}

bool Class_ProgSwd::web_GetFilesListExe(String &_str)	{
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
		if (
			//	(ftype == FILE_TYPE_HEX) || //TODO HEX file viewing when we will
			//di hexfile to swd
			(ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN))
		{
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

int  Class_ProgSwd::prog_Programm(String _path, String _fwTime)	{
	DEBUGLOGSWD(__PRETTY_FUNCTION__);    DEBUGLOGSWD("\r\n");
	DEBUGLOGSWD(" file %s time %s \r\n", _path.c_str(), _fwTime.c_str());
	int _res = ERR_OPENFILE;

	_res  = swdprog.stm32_ChipProgrammMain(_path );

	DEBUGLOGSWD("Programming end \r\n");
	return _res;
}



// stm32.html vvv
void Class_ProgSwd::web_GetFilesList (AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	String json = "";
	progSwd.web_GetFilesListExe(json);
	request->send(200, "text/json", json);
	json = "";
    DEBUGLOGSWD("List of *.hex *.bin *.binary files: %s \n\r", json);
}

void Class_ProgSwd::web_GetDiskInfoExe (AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	String values = "";
	progSwd.web_GetDiskInfoExe(values);
	request->send(200, "text/json", values);
	values = "";
    DEBUGLOGSWD("Disk info: %s \n\r", values);
}

void Class_ProgSwd::web_FileDelete(AsyncWebServerRequest *request) {
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
int Class_ProgSwd::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
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


void Class_ProgSwd::web_FileUpload2FS_Status(AsyncWebServerRequest *request) {
	DEBUGLOG(__FUNCTION__);	DEBUGLOG("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void Class_ProgSwd::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOG("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOG("\t upload status: %s\r\n", path.c_str());
	request->send(200, "text/plain", "");

	String ntpStr = "";

#if defined(MODULE_NTP)
	ntpStr = NTP.getTimeDateString();
#endif
	progSwd.prog_Programm(path, ntpStr );

	//здесь уже выход из программирования
}

// stm32.html ^^^




