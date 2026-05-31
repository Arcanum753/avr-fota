
#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#endif
#if defined(ESP8266)
#include <FS.h>
#endif

#include "FSWebServerLib.h"
#include "debug.h"



#include "core_ntp/core_ntp.h"


#include "core_json/core_json.h"

#include "prog_swd.h"
#include "module_prog_swd.h"
#include "common.h"
#include "module_prog_swd_version.h"

Class_ProgSwd progSwd(0);
Class_ProgSwd::Class_ProgSwd(uint8_t in): _in(in){ }

#if defined(ESP32)
    void Class_ProgSwd::setFs(fs::SPIFFSFS* fs)
#endif
#if defined(ESP8266)
    void Class_ProgSwd::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{	_fs = fs;	}

bool Class_ProgSwd::begin (){
    DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {	cfg_FileSave();	}
	swdprog.stm32Fx_begin();
	// прокидываем указатель на файловую систему в класс программатора
	swdprog.setFs(_fs);
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

	ESPHTTPServer.on("/prog/delete", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileDelete(request);
	});

	ESPHTTPServer.on("/prog/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		_uploadFileSize = request->contentLength();
		fileUpadedpercent = 0;
		request->send(200, "text/plain", "");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		web_FileUpload2FS( filename, index, data, len, final);
	});
	ESPHTTPServer.on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUpload2Chip(request);
	});

	ESPHTTPServer.on("/prog/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

	// Эндпоинт прогресса прошивки STM32 (для прогресс-бара на stm32.html)
	ESPHTTPServer.on("/prog/progress", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_GetProgress(request);
	});

	// Эндпоинт определения платформы (esp32 / esp8266) для выбора поведения прогресс-бара на stm32.html
	ESPHTTPServer.on("/prog/platform", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		String platform = "";
#if defined(ESP32)
		platform = "esp32";
#elif defined(ESP8266)
		platform = "esp8266";
#endif
		request->send(200, "text/plain", platform);
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
    CfgFile_ProgSwd.last_prog_file  = "";
    CfgFile_ProgSwd.last_prog_date  = "";
}

bool Class_ProgSwd::cfg_FileLoad() {
	DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(CONFIG_PROG_JSON, jsonDoc) == false ){	return false;	}
	// CfgFile_ProgSwd.programmer_type	= jsonDoc["type"].as<const char *>();
    CfgFile_ProgSwd.project_name		= jsonDoc["project"].as<const char *>();
    CfgFile_ProgSwd.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
    CfgFile_ProgSwd.last_prog_file		= jsonDoc["last_prog_file"].as<const char *>();
    CfgFile_ProgSwd.last_prog_date		= jsonDoc["last_prog_date"].as<const char *>();
	return true;
}

bool Class_ProgSwd::cfg_FileSave(){
	DEBUGLOGSWD("Save config PROJ\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= CfgFile_ProgSwd.programmer_type;
    jsonDoc["project"]		= CfgFile_ProgSwd.project_name;
    jsonDoc["chipsize"]     = CfgFile_ProgSwd.chip_size;
    jsonDoc["last_prog_file"] = CfgFile_ProgSwd.last_prog_file;
    jsonDoc["last_prog_date"] = CfgFile_ProgSwd.last_prog_date;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}


bool Class_ProgSwd::web_GetDiskInfoExe(String &_str)	{
	bool _ret = true;
	String values 	= 	"";
	size_t sizeAll	=	0;
	size_t sizeUsed	=	0;
	#if defined(ESP32)
	 if (_fs != nullptr) {
		 sizeAll	=	_fs->totalBytes();
		 sizeUsed	=	_fs->usedBytes();
	 }
	#endif
	#if defined(ESP8266)
		FSInfo fs_info;
		if (_fs && _fs->info(fs_info)) {
			sizeAll  = fs_info.totalBytes;
			sizeUsed = fs_info.usedBytes;
		}
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
	if (_fs == nullptr) { _ret = false; }
	else {
		Dir files = _fs->openDir("/");
		while (files.next()) {
			fname = files.fileName().c_str() ;
			pos = fname.find_last_of(FILE_TYPE_COMMA);
			ftype = fname.substr(pos + 1);
			if ((ftype == FILE_TYPE_HEX) || (ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)  ) {
				size_t fsize = files.fileSize();
			// Определяем дату прошивки: если имя файла совпадает с last_prog_file — подставляем дату
			String progDate = "";
			if (strcmp(fname.c_str(), CfgFile_ProgSwd.last_prog_file.c_str()) == 0) {
				progDate = CfgFile_ProgSwd.last_prog_date;
			}
			if (i) json += ",";
			json += "{";
			json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
			json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
			json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
			json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
			json += ",\"progchip\":\"";									json += "\"";
			json += ",\"progactual\":\"";								json += "\"";
			json += ",\"progdate\":\"";		json += progDate;			json += "\"";
			json += "}";
			i++;
			}
		}
	}
#endif
#if defined(ESP32)
	if (_fs == nullptr) { _ret = false; }// Если ФС не инициализирована — выходим
	else {
		File root =  _fs->open("/");
		if (root) {
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
					// Определяем дату прошивки: если имя файла совпадает с last_prog_file — подставляем дату
					String progDate = "";
					if (strcmp(fname.c_str(), CfgFile_ProgSwd.last_prog_file.c_str()) == 0) {
						progDate = CfgFile_ProgSwd.last_prog_date;
					}
					if (i) json += ",";
					json += "{";
					json +=  "\"filename\":\""; 	json += fname.c_str();		json += "\"";
					json += ",\"filetype\":\""; 	json += ftype.c_str();		json += "\"";
					json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
					json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
					json += ",\"progchip\":\"";									json += "\"";
					json += ",\"progactual\":\"";								json += "\"";
					json += ",\"progdate\":\"";		json += progDate;			json += "\"";
					json += "}";
					i++;
				}
				files = root.openNextFile();
			}
		}
	}
#endif

	json += "]";
	_str = json;
	return _ret;
}

int  Class_ProgSwd::prog_Programm(String _path, String _fwTime)	{
	DEBUGLOGSWD(__PRETTY_FUNCTION__);    DEBUGLOGSWD("\r\n");
	DEBUGLOGSWD(" file %s time %s \r\n", _path.c_str(), _fwTime.c_str());

	// проверка на инициализированность файловой системы
	if (!_fs) { return ERR_CFG; }
	// проверка наличия файла прошивки
	if (!_fs->exists(_path)) { return ERR_NOFILE; }

	int _res = ERR_OPENFILE;

	_res  = swdprog.stm32_ChipProgrammMain(_path );

	// Если прошивка успешна — сохраняем имя файла и дату в конфиг
	if (_res == 0) {
		CfgFile_ProgSwd.last_prog_file = _path;
		CfgFile_ProgSwd.last_prog_date = _fwTime;
		cfg_FileSave();
		DEBUGLOGSWD("Programming success, saved prog date: %s\r\n", _fwTime.c_str());
	}

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
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (path == "/")			{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 	{path = "/" + path;}
	DEBUGLOGSWD("handleFileDelete: %s\r\n", path.c_str());
	if (!_fs->exists(path)) 	{	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
	request->send(200, "text/plain", "");
}

// загрузчик файла из фронтенда
int Class_ProgSwd::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	int  _ret= 0;
	static File fsUploadFile;
	static size_t totalSize = 0;
	// Start
	if (!index) {
		totalSize = 0;
		fileUpadedpercent = 0;
		DEBUGLOGSWD("Name: %s\r\n", filename.c_str());

		// Проверка длины имени файла (SPIFFS ограничение 32 байта)
		if (filename.length() > MAX_FILENAME_LEN) {
			DEBUGLOGSWD("ERROR: filename too long (%u > %u): %s\r\n", filename.length(), MAX_FILENAME_LEN, filename.c_str());
			return -1;
		}

		if (!filename.startsWith("/")) {filename = "/" + filename;}
		fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGSWD("First upload part.\r\n");
	}
	// Continue
	if (fsUploadFile) {
		DEBUGLOGSWD("Continue upload part. Size = %u\r\n", len);
		if (fsUploadFile.write(data, len) == len) {
			totalSize += len;
			if (_uploadFileSize > 0) {
				fileUpadedpercent = (uint16_t)((totalSize * 100) / _uploadFileSize);
			}
		}
	}
	// End
	if (final) {
		if (fsUploadFile) {	fsUploadFile.close();	}
		_ret = totalSize;
		fileUpadedpercent = 100;
		DEBUGLOGSWD("HexFileUpload final Size: %u\n", totalSize);
		_hexfileCheck = filename;
		totalSize = 0;
	}
	return _ret;
}

void Class_ProgSwd::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGSWD("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	
	// Переключаем ESP8266 в AP режим на время прошивки STM32,
	// чтобы WiFi стек не разрушался при отключённом watchdog
	#if defined(ESP8266)
	WiFi.mode(WIFI_AP);
	#endif
	
	
	// После прошивки переключаемся обратно в STA и переподключаемся к роутеру
	#if defined(ESP8266)
	WiFi.mode(WIFI_STA);
	WiFi.reconnect();
	#endif
	
	String ntpStr = "";
	ntpStr = NTP.getTimeDateString();
	progSwd.prog_Programm(path, ntpStr );
	//здесь уже выход из программирования
	
	DEBUGLOGSWD("\t upload status: %s\r\n", path.c_str());
	request->send(200, "text/plain", "");
}

// stm32.html ^^^

// Эндпоинт прогресса — возвращает "percent|X|div\n" для JS polling
// Используется как для upload файла, так и для прошивки STM32
void Class_ProgSwd::web_GetProgress(AsyncWebServerRequest *request) {
	String values = "";
	values += "percent|" + (String)fileUpadedpercent + "|div\n";
	request->send(200, "text/plain", values);
}



String Class_ProgSwd::getVersionStr(){ return String(MODULE_PROG_SWD_VERSION); }
String Class_ProgSwd::getGeneratedTime(){ return String(MODULE_PROG_SWD_GENERATED_TIME); }
String Class_ProgSwd::getCommitDateStr(){ return String(MODULE_PROG_SWD_COMMIT_DATE_STR);	}

void Class_ProgSwd::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    String values = "";
    values += "swdversion|"     + getVersionStr()    + "|dev\n";
    values += "swdgentime|"     + getGeneratedTime() + "|dev\n";
    values += "swdgendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}
