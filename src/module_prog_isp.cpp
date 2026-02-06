#include "main.h"
#ifdef  PROGTYPE_ISP

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
// #include "debug.h"
#include "module_ntp.h"
#include "prog_isp.h"

#include "module_prog_isp.h"
#include "common.h"

Class_ProgIsp progIsp(0);
Class_ProgIsp::Class_ProgIsp(uint8_t in): _in(in){ }

#if ESP32
    void Class_ProgIsp::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void Class_ProgIsp::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool Class_ProgIsp::begin (){
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {	cfg_FileSave();	}
	
	avrprog.begin();

	//TODO return init result
    return true;
}


// TODO навести тут порядок с именаяи GET/POST запросов.
// all about webAPI. Set hooks
void  Class_ProgIsp::web_Init()	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
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


// cfg section
int Class_ProgIsp::cfg_FileSaveFromWeb(CfgFile_progIsp_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");

	CfgFile_progIsp	=  _inStruct;
	int _ret =  (int)cfg_FileSave();
	return _ret ;
}

int  Class_ProgIsp::cfg_FileStructGet(CfgFile_progIsp_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");

    progerr_t _ret = ERROR_OK;
    if(!cfg_FileLoad()) {  return ERR_CFG; }
    _inStruct = CfgFile_progIsp;
    return _ret ;
}

void Class_ProgIsp::cfg_SetDefault() {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	// CfgFile_progIsp.programmer_type	= DEFAULT_PROG_TYPE;
    CfgFile_progIsp.project_name  	= DEFAULT_PROG_PROJNAME;
    CfgFile_progIsp.chip_size      	= DEFAULT_chipsize;
}

bool Class_ProgIsp::cfg_FileLoad() {
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");

	JsonDocument jsonDoc;
	if (!ESPHTTPServer.load_jsonDoc(CONFIG_PROG_JSON, jsonDoc)){	return false;	}
	// CfgFile_progIsp.programmer_type	= jsonDoc["type"].as<const char *>();
    CfgFile_progIsp.project_name		= jsonDoc["project"].as<const char *>();
    CfgFile_progIsp.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
	return true;
}

bool Class_ProgIsp::cfg_FileSave(){
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= CfgFile_progIsp.programmer_type;
    jsonDoc["project"]		= CfgFile_progIsp.project_name;
    jsonDoc["chipsize"]     = CfgFile_progIsp.chip_size;
	return ESPHTTPServer.save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}


bool Class_ProgIsp::web_GetDiskInfoExe(String &_str)	{
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
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

bool Class_ProgIsp::web_GetFilesListExe(String &_str)	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");

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

int  Class_ProgIsp::prog_Programm(String _path, String _fwTime)	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");

	DEBUGLOGISP(" file %s time %s \r\n", _path.c_str(), _fwTime.c_str());
	int _res = ERR_OPENFILE;

	//_res  = swdprog.stm32_ChipProgrammMain(_path );

	DEBUGLOGISP("Programming end \r\n");
	return _res;
}



// avr.html vvv
void Class_ProgIsp::web_GetFilesList (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");

	String json = "";
	progIsp.web_GetFilesListExe(json);
	request->send(200, "text/json", json);
	json = "";
    DEBUGLOGISP("List of *.hex *.bin *.binary files: %s \n\r", json);
}

void Class_ProgIsp::web_GetDiskInfoExe (AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String values = "";
	progIsp.web_GetDiskInfoExe(values);
	request->send(200, "text/json", values);
	values = "";
    DEBUGLOGISP("Disk info: %s \n\r", values);
}

void Class_ProgIsp::web_FileDelete(AsyncWebServerRequest *request) {
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = request->arg(0U);
	DEBUGLOGISP("handleFileDelete: %s\r\n", path.c_str());
	if (path == "/")		{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) {path = "/" + path;}
	if (!_fs->exists(path)) {	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
	request->send(200, "text/plain", "");
}

// загрузчик файла из фронтенда
int Class_ProgIsp::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	int  _ret= 0;
	_hexFileUploadStatus = "";
	static File fsUploadFile;
	static size_t fileSize = 0;
	// Start
	if (!index) {
		DEBUGLOGISP("Name: %s\r\n", filename.c_str());
		if (!filename.startsWith("/")) {filename = "/" + filename;}
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGISP("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOGISP("Continue upload part. Size = %u\r\n", len);
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
		DEBUGLOGISP("HexFileUpload final Size: %u\n", fileSize);
		_hexfileCheck = filename;
		_hexFileUploadStatus  += "status|ok|div\n";
		_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
		_hexFileUploadStatus  += "fileSize|" + (String)fileSize 	+"|div\n";
		fileSize = 0;
	}
	return _ret;
}


void Class_ProgIsp::web_FileUpload2FS_Status(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");
	// TODO STATUS OUT!
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void Class_ProgIsp::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGISP("\t upload status: %s\r\n", path.c_str());
	request->send(200, "text/plain", "");

	prog_Programm(path,  NTP.getTimeDateString());

	//здесь уже выход из программирования
}

// avr.html ^^^




// avr.html vvv
void  Class_ProgIsp::avrGetActualFWInfo(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");

	String values = "";
	AVRISP_CfgFile_t AVRISP_HexFiles_Web;
	int _res0 ;
	_res0 = avrprog.cfgFileStructGet(AVRISP_HexFiles_Web);
	CfgFile_progIsp_t Prog_CfgFile;
	 int _res1 = cfg_FileStructGet(Prog_CfgFile);

	if (_res0 < ERROR_OK || _res1 < ERROR_OK) {
		values+= "getinfoerror|Can't open cfg file.|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	values += "proj|"   +			Prog_CfgFile.project_name				+ "|div\n";
	values += "chsize|" + 	(String)Prog_CfgFile.chip_size 					+ "|div\n";
	//values += "signcfg|"   +		AVRISP_HexFiles_Web.avr_signature		+ "|div\n";
	values += "signcon|"   +			avrprog.avrChipSignGet()			+ "|div\n";
	values += "hnamen|" + 			AVRISP_HexFiles_Web.hex_filename		+ "|div\n";
	values += "hvern|"  + 			AVRISP_HexFiles_Web.hex_version			+ "|div\n";
	values += "htimen|" +			AVRISP_HexFiles_Web.hex_buildtime		+ "|div\n";
	values += "flashtime|" +  		AVRISP_HexFiles_Web.fwTS 				+ "|div\n";
	request->send(200, "text/plain", values);
}

void  Class_ProgIsp::avrProg(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");

	String values = "";
	DEBUGLOGISP("_hexfilename  %s \n\r", _hexfileProg.c_str()); // что программируем
	int _res  = avrprog.avr_ChipProgrammMain(_hexfileProg, NTP.getTimeDateString() );
	// int _res  = prog_Programm(_hexfileProg,  NTP.getTimeDateString());

	DEBUGLOGISP("avrProg  %d \n\r", _res);
	values	+= "avrprogres|"+(String) _res+"|div\n";
	request->send(200, "text/plain", values);
}


void  Class_ProgIsp::avrProgStatus(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String values = "";
 	values += "avrprogver|" ;
	values += avrprog.chipFlashVerificationResultGet() ;
	values += "|div\n";

	request->send(200, "text/plain", values);
}

void  Class_ProgIsp::avrFusesRead(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");

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
}


void  Class_ProgIsp::avrWebFusesWrite(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	String s_high = "";  	uint8_t high = 0;
	String s_low  = "";		uint8_t low  = 0;
	String s_lock = "";		uint8_t lock = 0;
	String s_ext  = "";		uint8_t ext  = 0;
	if (request->args() > 0)  // Save Settings
	{
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGLOGISP("Arg %d: %s %s\r\n", i, request->argName(i).c_str() ,request->arg(i).c_str() );
			if (request->argName(i) == "avrfusehigh") 	{ s_high = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuselow") 	{ s_low  = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseprot") 	{ s_lock = urldecode(request->arg(i));	continue; }
			if (request->argName(i) == "avrfuseext") 	{ s_ext  = urldecode(request->arg(i));	continue; }
		}
		request->send_P(200, "text/html", Page_AvrRefresh);

		high =	hex2bin(s_high[0]);
		if (s_high[1])	{  	high = (high<<4) + 	hex2bin(s_high[1]);	}
		low =	hex2bin(s_low[0]);
    	if (s_low[1])	{	low = (low<<4) + 	hex2bin(s_low[1]);	}
		lock =	hex2bin(s_lock[0]);
    	if (s_lock[1])	{lock = (lock<<4) + 	hex2bin(s_lock[1]);	}
		ext =	hex2bin(s_ext[0]);
    	if (s_ext[1])	{	ext = (ext<<4) + 	hex2bin(s_ext[1]);	}

		avrprog.chipFusesWrite(high, low, lock, ext);
	}
	else {
		ESPHTTPServer.handleFileRead(request->url(), request);
	}
}
// avr.html ^^^








#endif

//EOF//
