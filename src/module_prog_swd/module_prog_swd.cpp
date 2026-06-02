#include <cstddef>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
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

    void Class_ProgSwd::setFs(fs::SPIFFSFS* fs)
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
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		web_FileUpload2FS( filename, index, data, len, final);
	});

	ESPHTTPServer.on("/prog/uploadstat", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUpload2FS_Status(request);
	});

	ESPHTTPServer.on("/prog/uploadsize", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUploadSize(request);
	});

	ESPHTTPServer.on("/prog/progress", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUploadProgress(request);
	});

	ESPHTTPServer.on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_FileUpload2Chip(request);
	});

	ESPHTTPServer.on("/prog/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
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
	 if (_fs != nullptr) {
		 sizeAll	=	_fs->totalBytes();
		 sizeUsed	=	_fs->usedBytes();
	 }

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

// Callback после завершения EERTOS-кооперативной прошивки (успех или ошибка)
void Class_ProgSwd::onFlashComplete() {
	if (swdprog.isFlashError()) {
		DEBUGLOGSWD("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		_progResult = 1;  // сигнал ошибки для web_FileUploadProgress
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGSWD("Programming error \r\n");
	} else {
		DEBUGLOGSWD("onFlashComplete: saving config for %s\r\n", _flashPath.c_str());
		
		// Сохраняем имя файла и дату в конфиг
		CfgFile_ProgSwd.last_prog_file = _flashPath;
		CfgFile_ProgSwd.last_prog_date = _flashNtpStr;
		cfg_FileSave();
		
		DEBUGLOGSWD("Programming success, saved prog date: %s\r\n", _flashNtpStr.c_str());
		
		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;
		
		DEBUGLOGSWD("Programming end \r\n");
	}
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
	_hexFileUploadStatus = "";
	// Start
	if (!index) {
		_uploadPercent = 0;
		_fileUploadBytes = 0;
		// если предыдущий файл не закрыт (например, загрузка прервана) — закрываем
		if (_fsUploadFile) {
			_fsUploadFile.close();
			DEBUGLOGSWD("WARN: previous upload file was open, closed.\r\n");
		}
		DEBUGLOGSWD("Name: %s\r\n", filename.c_str());

		// Проверка длины имени файла (SPIFFS ограничение 32 байта)
		if (filename.length() > MAX_FILENAME_LEN) {
			DEBUGLOGSWD("ERROR: filename too long (%u > %u): %s\r\n", filename.length(), MAX_FILENAME_LEN, filename.c_str());
			return -1;
		}

		if (!filename.startsWith("/")) {filename = "/" + filename;}
		_fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGSWD("First upload part.\r\n");
	}
	// Continue
	if (_fsUploadFile) {
		DEBUGLOGSWD("Continue upload part. Size = %u\r\n", len);
		if (_fsUploadFile.write(data, len) != len) {
			_hexFileUploadStatus  += "uploadstatus|error|div\n";
			_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
			_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes 	+"|div\n";
		}
		else {
			_fileUploadBytes += len;
			if (_uploadFileSize > 0) {
				_uploadPercent = (_fileUploadBytes * 100) / _uploadFileSize;
			}
		}
	}
	// End
	if (final) {
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File(); // сбрасываем в "пустой" файл
		}
		_ret = _fileUploadBytes;
		DEBUGLOGSWD("HexFileUpload final Size: %u\n", _fileUploadBytes);
		_hexfileCheck = filename;
		_hexFileUploadStatus  += "status|ok|div\n";
		_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
		_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes 	+"|div\n";
		_fileUploadBytes = 0;
	}
	return _ret;
}


void Class_ProgSwd::web_FileUpload2FS_Status(AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void Class_ProgSwd::web_FileUpload2Chip(AsyncWebServerRequest *request) {

	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	
	// Защита от повторного входа — если прошивка уже идёт
	if (swdprog.isFlashBusy()) {
		DEBUGLOGSWD("web_FileUpload2Chip: BUSY — programming already in progress\r\n");
		return request->send(423, "text/plain", "busy");
	}
	if (_progRunning) {
		DEBUGLOGSWD("web_FileUpload2Chip: _progRunning already true\r\n");
		return request->send(423, "text/plain", "busy");
	}
	
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGSWD("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGSWD("\t upload status: %s\r\n", path.c_str());
	
	// Сохраняем параметры для onFlashComplete()
	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();
	
	// Запускаем EERTOS-кооперативную прошивку
	if (!swdprog.startFlash(FLASH_START_ADDR, _flashPath)) {
		DEBUGLOGSWD("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}
	
	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();
	
	// Регистрируем задачу в EERTOS — она будет вызываться каждый loop()
	swdprog.beginFlashStep();
	
	// Отвечаем сразу, не блокируя HTTP
	request->send(200, "text/plain", "ok");
	DEBUGLOGSWD("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}

void Class_ProgSwd::web_FileUploadSize(AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	if (request->args() > 0) {
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "size") {
				_uploadPercent = 0;	// сброс процента при установке нового размера файла
				_uploadFileSize = request->arg(i).toInt();
				DEBUGLOGSWD("Upload size set: %u\r\n", _uploadFileSize);
				break;
			}
		}
	}
	request->send(200, "text/plain", "OK");
}

void Class_ProgSwd::web_FileUploadProgress(AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	String values = "";
	
	// Если идёт программирование STM32 — отдаём статус и процент
	if (_progRunning || swdprog.isFlashBusy()) {
		values += "progStatus|running|div\n";
		// Берём процент напрямую из swdprog, так как EERTOS обновляет его в реальном времени
		uint8_t pct = swdprog.getPercent();
		_uploadPercent = pct;
		values += "progPercent|" + (String)pct + "|div\n";
		request->send(200, "text/plain", values);
		return;
	}
	if (_progResult == 0) {
		values += "progStatus|done|div\n";
		values += "progPercent|100|div\n";
		_progResult = -1;  // сброс, чтобы следующий запрос не видел done
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}
	if (_progResult > 0 || _progResult < -1) {
		values += "progStatus|error|div\n";
		_progResult = -1;  // сброс
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}
	// Обычный процент загрузки файла
	values += "percent|" + (String)_uploadPercent + "|div\n";
	request->send(200, "text/plain", values);
}

// stm32.html ^^^

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