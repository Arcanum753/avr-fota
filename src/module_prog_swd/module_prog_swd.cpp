#include <cstddef>
#include <cstring>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <SPIFFS.h>
#include "esp_rom_md5.h"
#endif

#include "FSWebServerLib.h"
#include "debug.h"



#include "core_ntp/core_ntp.h"


#include "core_json/core_json.h"

#include "prog_swd.h"
#include "module_prog_swd.h"
#include "common.h"
#include "module_prog_swd_version.h"
#include "StringArray.h"

Class_ProgSwd progSwd(0);
Class_ProgSwd::Class_ProgSwd(uint8_t in): _in(in){ }

    void Class_ProgSwd::setFs(fs::SPIFFSFS* fs)
{	_fs = fs;	}

bool Class_ProgSwd::begin (){
    DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {	cfg_FileSave();	}
	swd_gpio_init();
	
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

	ESPHTTPServer.on("/prog/setmd5", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_setMD5(request);
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

	ESPHTTPServer.on("/prog/chipstatus", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_CheckChipStatus(request);
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
	DEBUGLOGSWD("Save config PROJ\r\n");
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
	 if (_fs != nullptr) {
#if defined(ESP32)
		 esp_task_wdt_reset();
#endif
		 sizeAll	=	_fs->totalBytes();
		 sizeUsed	=	_fs->usedBytes();
#if defined(ESP32)
		 esp_task_wdt_reset();
#endif
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

	// Шаг 1: Один раз загружаем filelist JSON
	JsonDocument listDoc;
	bool listLoaded = filelist_Load(listDoc);
	JsonArray arr;
	String lastSuccessFilename = "";
	if (listLoaded) {
		arr = listDoc.as<JsonArray>();
		lastSuccessFilename = filelist_GetLastSuccessFilename();
	}

	// Шаг 2: Первый проход по ФС — собираем данные файлов в три параллельных массива
	// Используем std::vector для эффективного доступа по индексу
	std::vector<String> fileNames;
	std::vector<String> fileTypes;
	std::vector<size_t> fileSizes;

	if (_fs == nullptr) { _ret = false; }
	else {
		File root = _fs->open("/");
		if (root) {
			File files = root.openNextFile();
			while (files) {
				std::string fname = files.name();
				size_t pos = fname.find_last_of(FILE_TYPE_COMMA);
				std::string ftype = fname.substr(pos + 1);
				if ((ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN)) {
					fileNames.push_back(String(fname.c_str()));
					fileTypes.push_back(String(ftype.c_str()));
					fileSizes.push_back((size_t)files.size());
				}
				files = root.openNextFile();
#if defined(ESP32)
				esp_task_wdt_reset();
#endif
			}
		}
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
	}

	// Шаг 3: Второй проход по индексу — собираем JSON с метаданными
	String json = "[";
	for (size_t i = 0; i < fileNames.size(); i++) {
		const String& fname = fileNames[i];
		const String& ftype = fileTypes[i];
		size_t fsize = fileSizes[i];

		// Получаем метаданные из filelist
		String fileMD5 = "";
		String uploadDate = "";
		String progDate = "";
		String progStatus = "";
		if (listLoaded) {
			for (JsonObject entry : arr) {
				if (strcmp(entry["filename"].as<const char*>(), fname.c_str()) == 0) {
					fileMD5 = entry["md5"].as<const char*>();
					uploadDate = entry["upload_date"].as<const char*>();
					progDate = entry["prog_date"].as<const char*>();
					progStatus = entry["prog_status"].as<const char*>();
					break;
				}
			}
		}

		// Определяем, является ли этот файл последним успешно прошитым
		bool isLastSuccess = (lastSuccessFilename.length() > 0 && strcmp(fname.c_str(), lastSuccessFilename.c_str()) == 0);

		if (i > 0) json += ",";
		json += "{";
		json +=  "\"filename\":\""; 	json += fname;				json += "\"";
		json += ",\"filetype\":\""; 	json += ftype;				json += "\"";
		json += ",\"filesizestr\":\"";	json += formatBytes(fsize); json += "\"";
		json += ",\"filesizebyte\":\"";	json += (String)fsize;		json += "\"";
		json += ",\"progchip\":\"";									json += "\"";
		json += ",\"progactual\":\"";								json += "\"";
		json += ",\"upload_date\":\"";	json += uploadDate;			json += "\"";
		json += ",\"prog_date\":\"";	json += progDate;			json += "\"";
		json += ",\"prog_status\":\"";	json += progStatus;			json += "\"";
		json += ",\"md5\":\"";			json += fileMD5;			json += "\"";
		json += ",\"is_last_success\":"; json += (isLastSuccess ? "true" : "false");
		json += "}";
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
	swdprog.stm32Fx_begin();
	_res  = swdprog.stm32_ChipProgrammMain(_path );

	// Обновляем статус прошивки в filelist (вместо сохранения в конфиг)
	String progStatus = (_res == 0) ? "ok" : "error";
	filelist_SetProgStatus(_path, _fwTime, progStatus);
	DEBUGLOGSWD("Programming %s, saved prog status to filelist: %s date=%s\r\n", 
		(_res == 0) ? "success" : "failed", _path.c_str(), _fwTime.c_str());

	DEBUGLOGSWD("Programming end \r\n");
	return _res;
}

// Callback после завершения EERTOS-кооперативной прошивки (успех или ошибка)
void Class_ProgSwd::onFlashComplete() {
	if (swdprog.isFlashError()) {
		DEBUGLOGSWD("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		// Сохраняем статус ошибки в filelist
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error");
		_progResult = 1;  // сигнал ошибки для web_FileUploadProgress
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGSWD("Programming error, saved prog status to filelist\r\n");
	} else {
		DEBUGLOGSWD("onFlashComplete: success for %s\r\n", _flashPath.c_str());
		
		// Сохраняем статус успеха в filelist (вместо конфига)
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok");
		
		DEBUGLOGSWD("Programming success, saved prog date to filelist: %s\r\n", _flashNtpStr.c_str());
		
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
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	if (!_fs->exists(path)) 	{	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	// Также удаляем запись из filelist
	filelist_RemoveEntry(path);
	DEBUGLOGSWD("handleFileDelete: removed '%s' from filelist\r\n", path.c_str());
	request->send(200, "text/plain", "");
}

// загрузчик файла из фронтенда с контролем MD5
int Class_ProgSwd::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	int  _ret= 0;
	_hexFileUploadStatus = "";
	static md5_context_t _md5Ctx;
	static bool _md5Initialized = false;
	// Start
	if (!index) {
		_uploadPercent = 0;
		_fileUploadBytes = 0;
		_fileUploadError = false;
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
		
		// Инициализируем MD5-контекст
		esp_rom_md5_init(&_md5Ctx);
		_md5Initialized = true;
	}
	// Continue
	if (_fsUploadFile && !_fileUploadError) {
		DEBUGLOGSWD("Continue upload part. Size = %u\r\n", len);
		if (_fsUploadFile.write(data, len) != len) {
			_fileUploadError = true;
			_hexFileUploadStatus  += "uploadstatus|error|div\n";
			_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
			_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes 	+"|div\n";
		}
		else {
			_fileUploadBytes += len;
			if (_uploadFileSize > 0) {
				_uploadPercent = (_fileUploadBytes * 100) / _uploadFileSize;
			}
			// Обновляем MD5 при каждом чанке
			if (_md5Initialized) {
				esp_rom_md5_update(&_md5Ctx, data, len);
			}
		}
	}
	// End
	if (final) {
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File(); // сбрасываем в "пустой" файл
		}
		
		// Финализируем MD5 и сравниваем
		if (_md5Initialized && !_fileUploadError) {
			uint8_t hash[16];
			esp_rom_md5_final(hash, &_md5Ctx);
			_md5Initialized = false;
			
			char hex[33];
			for (int i = 0; i < 16; i++) {
				sprintf(hex + i * 2, "%02x", hash[i]);
			}
			hex[32] = '\0';
			String serverMD5 = String(hex);
			
			DEBUGLOGSWD("MD5 check: browser='%s' server='%s'\r\n", _browserFileMD5.c_str(), serverMD5.c_str());
			
		if (serverMD5 != _browserFileMD5) {
				// MD5 не совпадает — удаляем файл, сообщаем об ошибке
				_fileUploadError = true;
				DEBUGLOGSWD("MD5 MISMATCH! Removing corrupted file %s\r\n", filename.c_str());
				_fs->remove(filename);
				// Также удаляем запись из filelist, если она была (например, от предыдущей загрузки)
				filelist_RemoveEntry(filename);
				_hexFileUploadStatus  = ""; // сброс
				_hexFileUploadStatus  += "uploadstatus|error|div\n";
				_hexFileUploadStatus  += "file|"	  + filename			+"|div\n";
				_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes 	+"|div\n";
				_hexFileUploadStatus  += "md5error|MD5 mismatch - file corrupted and removed|div\n";
				_fileUploadBytes = 0;
				return -1;
			}
			
			// MD5 совпал — добавляем запись в filelist
			String dateStr = NTP.getTimeDateString();
			if (dateStr.length() == 0) {
				dateStr = "unknown";
			}
			filelist_AddEntry(filename, dateStr, serverMD5);
			DEBUGLOGSWD("MD5 OK, added to filelist: %s date=%s md5=%s\r\n", filename.c_str(), dateStr.c_str(), serverMD5.c_str());
		}
		
		_ret = _fileUploadBytes;
		DEBUGLOGSWD("HexFileUpload final Size: %u\n", _fileUploadBytes);
		_hexfileCheck = filename;
		if (!_fileUploadError) {
			_hexFileUploadStatus  = ""; // сброс
			_hexFileUploadStatus  += "status|ok|div\n";
			_hexFileUploadStatus  += "file|"	  + _hexfileCheck		+"|div\n";
			_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes 	+"|div\n";
		}
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

// ========== MD5 & Pre-upload Validation ==========
void Class_ProgSwd::web_setMD5(AsyncWebServerRequest *request) {
	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	_browserFileMD5 = "";
	_browserFileSize = 0;
	_browserFileName = "";
	_fileUploadError = false;

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGSWD("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
		if (request->argName(i) == "md5") {
			_browserFileMD5 = urldecode(request->arg(i));
			continue;
		}
		if (request->argName(i) == "size") {
			_browserFileSize = request->arg(i).toInt();
			continue;
		}
		if (request->argName(i) == "name") {
			_browserFileName = urldecode(request->arg(i));
			if (!_browserFileName.startsWith("/")) {
				_browserFileName = "/" + _browserFileName;
			}
			continue;
		}
	}

	// Проверка: существует ли уже файл с таким именем
	if (filelist_FileExists(_browserFileName)) {
		DEBUGLOGSWD("setMD5: filename '%s' already exists!\r\n", _browserFileName.c_str());
		request->send(200, "text/plain", "ERROR|FILENAME_EXISTS|Filename already exists in filesystem");
		return;
	}

	// Если файла нет в filelist, но он физически есть в ФС — тоже блокируем
	if (_fs && _fs->exists(_browserFileName)) {
		DEBUGLOGSWD("setMD5: filename '%s' physically exists in FS!\r\n", _browserFileName.c_str());
		request->send(200, "text/plain", "ERROR|FILENAME_EXISTS|Filename already exists in filesystem");
		return;
	}

	// Проверка свободного места
	size_t sizeAll = 0;
	size_t sizeUsed = 0;
	if (_fs != nullptr) {
		sizeAll = _fs->totalBytes();
		sizeUsed = _fs->usedBytes();
	}
	size_t sizeFree = (sizeAll > sizeUsed) ? (sizeAll - sizeUsed) : 0;

	if (_browserFileSize > sizeFree) {
		DEBUGLOGSWD("setMD5: not enough space! need %u, free %u\r\n", _browserFileSize, sizeFree);
		request->send(200, "text/plain", "ERROR|NO_SPACE|Not enough free space on filesystem");
		return;
	}

	DEBUGLOGSWD("setMD5: OK md5=%s size=%u name=%s\r\n", _browserFileMD5.c_str(), _browserFileSize, _browserFileName.c_str());
	request->send(200, "text/plain", "OK");
}

// ========== Filelist Management ==========

bool Class_ProgSwd::filelist_Load(JsonDocument &doc) {
	if (!_fs) return false;
	File file = _fs->open(SWD_FILELIST_JSON, "r");
	if (!file) {
		DEBUGLOGSWD("filelist_Load: %s not found, returning empty array\r\n", SWD_FILELIST_JSON);
		doc.clear();
		doc.to<JsonArray>();
		return true; // нет файла — не ошибка, пустой массив
	}
	DeserializationError err = deserializeJson(doc, file);
	file.close();
	if (err) {
		DEBUGLOGSWD("filelist_Load: JSON parse error: %s\r\n", err.c_str());
		doc.clear();
		doc.to<JsonArray>();
		return false;
	}
	if (!doc.is<JsonArray>()) {
		doc.clear();
		doc.to<JsonArray>();
	}
	return true;
}

bool Class_ProgSwd::filelist_Save(JsonDocument &doc) {
	if (!_fs) return false;
	File file = _fs->open(SWD_FILELIST_JSON, "w");
	if (!file) {
		DEBUGLOGSWD("filelist_Save: failed to open %s for writing\r\n", SWD_FILELIST_JSON);
		return false;
	}
	serializeJson(doc, file);
	file.flush();
	file.close();
	DEBUGLOGSWD("filelist_Save: saved %d entries\r\n", doc.as<JsonArray>().size());
	return true;
}

bool Class_ProgSwd::filelist_AddEntry(const String &filename, const String &upload_date, const String &md5) {
	JsonDocument doc;
	filelist_Load(doc);
	JsonArray arr = doc.as<JsonArray>();

	// Нормализуем имя файла — убираем ведущий слеш для единообразия
	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	// Проверяем, нет ли уже такой записи (дубликат)
	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			// Обновляем существующую запись
			entry["upload_date"] = upload_date;
			entry["md5"] = md5;
			return filelist_Save(doc);
		}
	}

	// Создаём новую запись
	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["upload_date"] = upload_date;
	newEntry["md5"] = md5;

	return filelist_Save(doc);
}

bool Class_ProgSwd::filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status) {
	JsonDocument doc;
	filelist_Load(doc);
	JsonArray arr = doc.as<JsonArray>();

	// Нормализуем имя файла — убираем ведущий слеш для единообразия
	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			entry["prog_date"] = prog_date;
			entry["prog_status"] = prog_status;
			return filelist_Save(doc);
		}
	}

	// Если запись не найдена — создаём новую (на случай, если файл был на ФС до введения filelist)
	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["prog_date"] = prog_date;
	newEntry["prog_status"] = prog_status;
	DEBUGLOGSWD("filelist_SetProgStatus: created new entry for %s (was not in filelist)\r\n", normalizedName.c_str());
	return filelist_Save(doc);
}

String Class_ProgSwd::filelist_GetLastSuccessFilename() {
	JsonDocument doc;
	if (!filelist_Load(doc)) return "";
	JsonArray arr = doc.as<JsonArray>();

	String bestFilename = "";
	String bestDate = "";
	for (JsonObject entry : arr) {
		const char* status = entry["prog_status"].as<const char*>();
		if (status && strcmp(status, "ok") == 0) {
			const char* date = entry["prog_date"].as<const char*>();
			if (date && strlen(date) > 0) {
				// Сравниваем даты: выбираем самую позднюю
				if (bestDate.length() == 0 || strcmp(date, bestDate.c_str()) > 0) {
					bestDate = String(date);
					bestFilename = entry["filename"].as<const char*>();
				}
			}
		}
	}
	return bestFilename;
}

bool Class_ProgSwd::filelist_RemoveEntry(const String &filename) {
	JsonDocument doc;
	filelist_Load(doc);
	JsonArray arr = doc.as<JsonArray>();

	// Нормализуем имя файла — убираем ведущий слеш для единообразия
	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	int idx = -1;
	for (size_t i = 0; i < arr.size(); i++) {
		if (strcmp(arr[i]["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			idx = (int)i;
			break;
		}
	}

	if (idx < 0) return false; // не найдено

	arr.remove(idx);
	return filelist_Save(doc);
}

bool Class_ProgSwd::filelist_FileExists(const String &filename) {
	JsonDocument doc;
	filelist_Load(doc);
	JsonArray arr = doc.as<JsonArray>();

	// Нормализуем имя файла — убираем ведущий слеш для единообразия
	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			return true;
		}
	}
	return false;
}

String Class_ProgSwd::file_ComputeMD5(const String &path) {
	if (!_fs || !_fs->exists(path)) {
		DEBUGLOGSWD("file_ComputeMD5: file not found %s\r\n", path.c_str());
		return "";
	}

	File f = _fs->open(path, "r");
	if (!f) {
		DEBUGLOGSWD("file_ComputeMD5: cannot open %s\r\n", path.c_str());
		return "";
	}

	md5_context_t md5Ctx;
	esp_rom_md5_init(&md5Ctx);

	uint8_t buf[256];
	size_t bytesRead;
	while ((bytesRead = f.read(buf, sizeof(buf))) > 0) {
		esp_rom_md5_update(&md5Ctx, buf, bytesRead);
	}

	uint8_t hash[16];
	esp_rom_md5_final(hash, &md5Ctx);
	f.close();

	char hex[33];
	for (int i = 0; i < 16; i++) {
		sprintf(hex + i * 2, "%02x", hash[i]);
	}
	hex[32] = '\0';

	DEBUGLOGSWD("file_ComputeMD5: %s -> %s\r\n", path.c_str(), hex);
	return String(hex);
}

void Class_ProgSwd::web_FileUploadProgress(AsyncWebServerRequest *request) {

	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	String values = "";
	
	// Если идёт программирование STM32 — отдаём статус и процент
	if (_progRunning || swdprog.isFlashBusy()) {
		uint8_t pct = swdprog.getPercent();
		_uploadPercent = pct;
		
		// Если процент 0 и прошивка только началась — отдаём "starting"
		if (pct == 0 && swdprog.isFlashBusy()) {
			values += "progStatus|starting|div\n";
		} else {
			values += "progStatus|running|div\n";
		}
		values += "progPercent|" + (String)pct + "|div\n";
		request->send(200, "text/plain", values);
		return;
	}
	if (_progResult == 0) {
		values += "progStatus|done|div\n";
		values += "progPercent|100|div\n";
		// Добавляем время прошивки
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			values += "progTime|" + (String)elapsed + "|div\n";
		}
		_progResult = -1;  // сброс, чтобы следующий запрос не видел done
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}
	if (_progResult > 0 || _progResult < -1) {
		values += "progStatus|error|div\n";
		// Добавляем время прошивки даже при ошибке
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			values += "progTime|" + (String)elapsed + "|div\n";
		}
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

// ========== Chip Status Check ==========

bool Class_ProgSwd::chip_IsConnected() {
    // Если прошивка идёт — считаем, что чип на связи
    if (_progRunning || swdprog.isFlashBusy()) {
        return true;
    }
    // Проверка таймаута
    if (!_chipConnected) return false;
    if ((millis() - _chipStatusTime) >= (CHIP_STATUS_TIMEOUT * 1000UL)) {
        _chipConnected = false;
        return false;
    }
    return true;
}

void Class_ProgSwd::web_CheckChipStatus(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    String values = "";

    // Если прошивка идёт — не дёргаем SWD-линию
    if (_progRunning || swdprog.isFlashBusy()) {
        values += "status|busy|div\n";
        request->send(200, "text/plain", values);
        return;
    }

    // Если проверка чипа уже запущена через EERTOS — сообщаем об этом
    if (swdprog.isChipCheckBusy()) {
        values += "status|checking|div\n";
        request->send(200, "text/plain", values);
        return;
    }

    // Запускаем EERTOS-кооперативную проверку чипа
    // Браузер будет делать polling, пока не получит connected/disconnected
    swdprog.startChipCheck();
    
    values += "status|checking|div\n";
    request->send(200, "text/plain", values);
    DEBUGLOGSWD("web_CheckChipStatus: EERTOS chip check started\r\n");
}

// Callback после завершения EERTOS-кооперативной проверки чипа
void Class_ProgSwd::onChipCheckComplete(uint32_t chipId) {
    DEBUGLOGSWD("%s: chipId=0x%08x\n\r", __FUNCTION__, chipId);
    
    _chipStatusTime = millis();
    
    if (chipId != 0) {
        // Чип найден
        _chipConnected = true;
        _chipId = chipId;
        DEBUGLOGSWD("onChipCheckComplete: chip connected, ID=0x%08x\r\n", chipId);
    } else {
        // Чип не обнаружен
        _chipConnected = false;
        _chipId = 0;
        DEBUGLOGSWD("onChipCheckComplete: chip NOT detected\r\n");
    }
}
