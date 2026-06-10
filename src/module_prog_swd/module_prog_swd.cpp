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

// esp32 flash file system
#if defined(ESP32)
    void Class_ProgSwd::setFs(fs::SPIFFSFS* fs)
{	_fs = fs;	}
#endif

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

//project.html vvv
    // Project config page
    ESPHTTPServer.on("/project/info", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        web_ProjectInfo(request);
    });

    ESPHTTPServer.on("/project/save", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        web_ProjectSave(request);
    });

    ESPHTTPServer.on("/project/chips", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        web_ProjectChips(request);
    });

    ESPHTTPServer.on("/project/chipinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        web_ProjectChipInfo(request);
    });

    // Общий роут, отдающий HTML — последним
    ESPHTTPServer.on("/project", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        ESPHTTPServer.handleFileRead("/web/project.html", request);
    });
//project.html ^^^

}


// cfg section
int Class_ProgSwd::cfg_FileSaveFromWeb(CfgFile_ProgSwd_t &_inStruct)  {
    DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	CfgFile_ProgSwd	=  _inStruct;
	bool ret = cfg_FileSave();
	if (ret) {
		return 0;
	}
	return 1;
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
    CfgFile_ProgSwd.project_name  	= DEFAULT_PROG_PROJNAME;
    CfgFile_ProgSwd.chip_name      	= DEFAULT_CHIP_NAME;
}

bool Class_ProgSwd::cfg_FileLoad() {
	DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(CONFIG_PROG_JSON, jsonDoc) == false ){	return false;	}
    CfgFile_ProgSwd.project_name		= jsonDoc["project"].as<const char *>();
    CfgFile_ProgSwd.chip_name			= jsonDoc["chip_name"].as<const char *>();
	return true;
}

bool Class_ProgSwd::cfg_FileSave(){
	DEBUGLOGSWD("Save config PROJ\r\n");
	JsonDocument jsonDoc;
    jsonDoc["project"]		= CfgFile_ProgSwd.project_name;
    jsonDoc["chip_name"]    = CfgFile_ProgSwd.chip_name;
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
		 sizeAll	=	_fs->totalBytes();
		 sizeUsed	=	_fs->usedBytes();
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
#if defined(ESP32)
		File root = _fs->open("/");
		if (root) {
			File files = root.openNextFile();
			while (files) {
				std::string fname = files.name();
				size_t pos = fname.find_last_of(FILE_TYPE_COMMA);
				std::string ftype = fname.substr(pos + 1);
				if ((ftype == FILE_TYPE_BINARY) || (ftype == FILE_TYPE_BIN) || (ftype == FILE_TYPE_HEX)) {
					fileNames.push_back(String(fname.c_str()));
					fileTypes.push_back(String(ftype.c_str()));
					fileSizes.push_back((size_t)files.size());
				}
				files = root.openNextFile();
				esp_task_wdt_reset();
			}
		}
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
		String progTime = "";
		String progError = "";
		String progErrorStage = "";
		String progErrorPercent = "";
		if (listLoaded) {
			for (JsonObject entry : arr) {
				if (strcmp(entry["filename"].as<const char*>(), fname.c_str()) == 0) {
					fileMD5 = entry["md5"].as<const char*>();
					uploadDate = entry["upload_date"].as<const char*>();
					progDate = entry["prog_date"].as<const char*>();
					progStatus = entry["prog_status"].as<const char*>();
					progTime = entry["prog_time"].as<const char*>();
					progError = entry["prog_error"].as<const char*>();
					progErrorStage = entry["prog_error_stage"].as<const char*>();
					progErrorPercent = entry["prog_error_percent"].as<const char*>();
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
		json += ",\"prog_time\":\"";	json += progTime;			json += "\"";
		json += ",\"prog_error\":\"";	json += progError;			json += "\"";
		json += ",\"prog_error_stage\":\"";	json += progErrorStage;		json += "\"";
		json += ",\"prog_error_percent\":\""; json += progErrorPercent;	json += "\"";
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
		// Получаем текст ошибки из программатора
		String errorText = swdprog.getFlashErrorString();
		String errorStage = swdprog.getFlashErrorStage();
		uint8_t errorPercent = swdprog.getFlashErrorPercent();
		// Вычисляем время прошивки до ошибки (в миллисекундах для совместимости с фронтендом)
		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;  // в миллисекундах
		}
		// Сохраняем статус ошибки в filelist с текстом ошибки, стадией и процентом
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, (String)errorPercent);
		_progResult = 1;  // сигнал ошибки для web_FileUploadProgress
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGSWD("Programming error: %s, stage=%s, percent=%u, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent);
	} else {
		DEBUGLOGSWD("onFlashComplete: success for %s\r\n", _flashPath.c_str());
		
		// Вычисляем время прошивки (в миллисекундах для совместимости с фронтендом)
		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;  // в миллисекундах
		}
		
		// Сохраняем статус успеха в filelist с временем прошивки
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr);
		
		DEBUGLOGSWD("Programming success, saved prog date to filelist: %s, time=%sms\r\n", _flashNtpStr.c_str(), elapsedStr.c_str());
		
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

// Таймаут загрузки: если от последнего чанка прошло больше 30 секунд — считаем загрузку прерванной
#define UPLOAD_TIMEOUT_MS 30000

// Очистка "зависшей" загрузки: закрываем и удаляем недозагруженный файл
void Class_ProgSwd::_cleanupStaleUpload() {
	if (_fsUploadFile) {
		_fsUploadFile.close();
		_fsUploadFile = File();
	}
	if (_uploadFilename.length() > 0) {
		DEBUGLOGSWD("Cleanup: removing stale upload file %s\r\n", _uploadFilename.c_str());
		if (_fs && _fs->exists(_uploadFilename)) {
			_fs->remove(_uploadFilename);
		}
		filelist_RemoveEntry(_uploadFilename);
		_uploadFilename = "";
	}
	_fileUploadBytes = 0;
	_fileUploadError = false;
	_uploadPercent = 0;
	_uploadLastChunkTime = 0;
}

// загрузчик файла из фронтенда с контролем MD5
int Class_ProgSwd::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__);	DEBUGLOGSWD("\r\n");
	int  _ret= 0;
	_hexFileUploadStatus = "";
#if defined(ESP32)
	static md5_context_t _md5Ctx;
#endif
	static bool _md5Initialized = false;
	static size_t _expectedFileSize = 0;  // сохраняем ожидаемый размер локально

	// Проверка таймаута: если загрузка идёт, но чанков давно не было — очищаем
	if (index > 0 && _fsUploadFile && !final) {
		if (_uploadLastChunkTime > 0 && (millis() - _uploadLastChunkTime) > UPLOAD_TIMEOUT_MS) {
			DEBUGLOGSWD("UPLOAD TIMEOUT: no data for %u ms, cleaning up stale upload\r\n", (millis() - _uploadLastChunkTime));
			_cleanupStaleUpload();
			// Сбрасываем статические переменные
			_md5Initialized = false;
			_expectedFileSize = 0;
			// Продолжаем как новую загрузку (index всё ещё > 0, но _fsUploadFile уже сброшен)
		}
	}

	// Start
	if (!index) {
		// Если есть "зависшая" загрузка от предыдущего обрыва — очищаем
		_cleanupStaleUpload();

		_uploadPercent = 0;
		_fileUploadBytes = 0;
		_fileUploadError = false;
		_expectedFileSize = _browserFileSize;  // фиксируем размер на момент старта
		// если предыдущий файл не закрыт (например, загрузка прервана) — закрываем
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File();
			DEBUGLOGSWD("WARN: previous upload file was open, closed.\r\n");
		}
		DEBUGLOGSWD("Name: %s\r\n", filename.c_str());

		// Проверка длины имени файла (SPIFFS ограничение 32 байта)
		if (filename.length() > MAX_FILENAME_LEN) {
			DEBUGLOGSWD("ERROR: filename too long (%u > %u): %s\r\n", filename.length(), MAX_FILENAME_LEN, filename.c_str());
			return -1;
		}

		if (!filename.startsWith("/")) {filename = "/" + filename;}
		_uploadFilename = filename;  // запоминаем имя для очистки при таймауте
		_fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGSWD("First upload part.\r\n");
		
		// Инициализируем MD5-контекст
#if defined(ESP32)
		esp_rom_md5_init(&_md5Ctx);
#endif
		_md5Initialized = true;
	}
	// Continue
	if (_fsUploadFile && !_fileUploadError) {
		_uploadLastChunkTime = millis();  // обновляем время последнего чанка
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
#if defined(ESP32)
				esp_rom_md5_update(&_md5Ctx, data, len);
#endif
			}
		}
#if defined(ESP32)
		// Сбрасываем watchdog при каждом чанке, чтобы предотвратить перезагрузку
		// при загрузке больших файлов
		esp_task_wdt_reset();
#endif
	}
	// End
	if (final) {
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File(); // сбрасываем в "пустой" файл
		}
		_uploadFilename = "";  // загрузка завершена, имя больше не нужно для очистки
		_uploadLastChunkTime = 0;
		
		// Проверяем размер файла (используем _expectedFileSize, сохранённый на старте)
		if (!_fileUploadError && _expectedFileSize > 0 && _fileUploadBytes != _expectedFileSize) {
			_fileUploadError = true;
			DEBUGLOGSWD("SIZE MISMATCH! expected=%u received=%u, removing corrupted file %s\r\n", _expectedFileSize, _fileUploadBytes, filename.c_str());
			_fs->remove(filename);
			filelist_RemoveEntry(filename);
			_hexFileUploadStatus  = "";
			_hexFileUploadStatus  += "uploadstatus|error|div\n";
			_hexFileUploadStatus  += "file|"      + filename           +"|div\n";
			_hexFileUploadStatus  += "fileSize|" + (String)_fileUploadBytes   +"|div\n";
			_hexFileUploadStatus  += "sizeerror|Size mismatch - file corrupted and removed|div\n";
			_fileUploadBytes = 0;
			_expectedFileSize = 0;
			_md5Initialized = false;
			return -1;
		}
		
		// Финализируем MD5 и сравниваем
		if (_md5Initialized && !_fileUploadError) {
			String serverMD5 = "";

#if defined(ESP32)
			uint8_t hash[16];
			esp_rom_md5_final(hash, &_md5Ctx);
			char hex[33];
			for (int i = 0; i < 16; i++) {
				sprintf(hex + i * 2, "%02x", hash[i]);
			}
			hex[32] = '\0';
			serverMD5 = String(hex);
#endif
			_md5Initialized = false;
			
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
				_expectedFileSize = 0;
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
		_expectedFileSize = 0;
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
	
	// ===== Читаем IDCODE чипа (один раз) и определяем параметры =====
	uint32_t flashStart = DEFAULT_FLASH_START_ADDR;
	uint32_t chipMemSize = DEFAULT_PAGE_SIZE * 64;  // дефолтный размер (будет переопределён из swd_cfg.json)
	uint32_t pageSize = DEFAULT_PAGE_SIZE;
	uint32_t wordSize = DEFAULT_WORD_SIZE;
	uint32_t cswValue = DEFAULT_CSW_VALUE;
	
	String expectedChipName = CfgFile_ProgSwd.chip_name;
	
	swd_gpio_init();
	uint32_t idcode = swd_init();
	
	if (idcode == 0) {
		if (expectedChipName.length() > 0) {
			// В конфиге выбран чип, но чип не отвечает
			String errorText = "Chip offline - unable to read IDCODE";
			DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
			filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
			return request->send(423, "text/plain", errorText);
		}
		// chip_name пустой — чип не отвечает, используем дефолты
		DEBUGLOGSWD("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	} else {
		DEBUGLOGSWD("web_FileUpload2Chip: detected chip ID=0x%08x\n\r", idcode);
		
		// Ищем чип в swd_cfg.json
		ChipConfig_t chipCfg;
		bool found = chipCfg_FindById(idcode, chipCfg);
		
		if (found) {
			// Нашли чип в конфиге
			if (expectedChipName.length() > 0 && chipCfg.name != expectedChipName) {
				// Имя не совпадает с ожидаемым
				String errorText = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chipCfg.name + "'";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			// Имя совпадает (или chip_name пустой) — используем параметры из конфига
			flashStart = chipCfg.flash_start;
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			wordSize = chipCfg.word_size;
			cswValue = chipCfg.csw_value;
			swdprog.setChipFamily(chipCfg.family);
			DEBUGLOGSWD("web_FileUpload2Chip: using config for %s (family=%s flash=%u start=0x%08x page=%u word=%u csw=0x%08x)\n\r",
				chipCfg.name.c_str(), chipCfg.family.c_str(), chipMemSize, flashStart, pageSize, wordSize, cswValue);
		} else {
			// Чип не найден в конфиге
			if (expectedChipName.length() > 0) {
				// В конфиге выбран чип, но IDCODE не найден в swd_cfg.json
				char idStr[12];
				snprintf(idStr, sizeof(idStr), "0x%08x", idcode);
				String errorText = "Chip '" + expectedChipName + "' not found in swd_cfg.json (IDCODE=" + String(idStr) + ")";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			// chip_name пустой — используем дефолтные параметры
			swdprog.setChipFamily("stm32f1");
			DEBUGLOGSWD("web_FileUpload2Chip: chip ID=0x%08x not in swd_cfg.json, using defaults (family=stm32f1)\n\r", idcode);
		}
	}
	
	// Запускаем EERTOS-кооперативную прошивку с параметрами из конфига чипа
	if (!swdprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize, wordSize, cswValue)) {
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
#if defined(ESP32)
		sizeAll = _fs->totalBytes();
		sizeUsed = _fs->usedBytes();
#endif
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
		DEBUGLOGSWD("filelist_Load: %s not found, creating empty filelist\r\n", SWD_FILELIST_JSON);
		doc.clear();
		doc.to<JsonArray>();
		// Создаём пустой filelist на диске
		filelist_Save(doc);
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

bool Class_ProgSwd::filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error, const String &prog_time, const String &prog_error_stage, const String &prog_error_percent) {
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
			if (prog_error.length() > 0) {
				entry["prog_error"] = prog_error;
			} else {
				entry.remove("prog_error");
			}
			if (prog_time.length() > 0) {
				entry["prog_time"] = prog_time;
			} else {
				entry.remove("prog_time");
			}
			if (prog_error_stage.length() > 0) {
				entry["prog_error_stage"] = prog_error_stage;
			} else {
				entry.remove("prog_error_stage");
			}
			if (prog_error_percent.length() > 0) {
				entry["prog_error_percent"] = prog_error_percent;
			} else {
				entry.remove("prog_error_percent");
			}
			return filelist_Save(doc);
		}
	}

	// Если запись не найдена — создаём новую (на случай, если файл был на ФС до введения filelist)
	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["prog_date"] = prog_date;
	newEntry["prog_status"] = prog_status;
	if (prog_error.length() > 0) {
		newEntry["prog_error"] = prog_error;
	}
	if (prog_time.length() > 0) {
		newEntry["prog_time"] = prog_time;
	}
	if (prog_error_stage.length() > 0) {
		newEntry["prog_error_stage"] = prog_error_stage;
	}
	if (prog_error_percent.length() > 0) {
		newEntry["prog_error_percent"] = prog_error_percent;
	}
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

#if defined(ESP32)
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
#endif
}

void Class_ProgSwd::web_FileUploadProgress(AsyncWebServerRequest *request) {

	DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
	String values = "";
	
	// Сначала проверяем результат прошивки (done/error), чтобы не пропустить
	// финальный статус из-за race condition с isFlashBusy()
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
		// Получаем текст ошибки из filelist для этого файла
		String errorText = "";
		JsonDocument doc;
		if (filelist_Load(doc)) {
			JsonArray arr = doc.as<JsonArray>();
			String normalizedName = _flashPath;
			if (normalizedName.startsWith("/")) {
				normalizedName = normalizedName.substring(1);
			}
			for (JsonObject entry : arr) {
				if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
					const char* err = entry["prog_error"].as<const char*>();
					if (err && strlen(err) > 0) {
						errorText = String(err);
					}
					break;
				}
			}
		}
		if (errorText.length() > 0) {
			values += "progError|" + errorText + "|div\n";
		}
		_progResult = -1;  // сброс
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}
	
	// Если результат ещё не установлен — проверяем, идёт ли процесс
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

// ========== Chip Config from swd_cfg.json ==========

bool Class_ProgSwd::chipCfg_Load() {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    if (!_fs) {
        DEBUGLOGSWD("chipCfg_Load: FS not initialized\n\r");
        return false;
    }
    if (!_fs->exists(SWD_CFG_JSON)) {
        DEBUGLOGSWD("chipCfg_Load: %s not found, using defaults\n\r", SWD_CFG_JSON);
        return false;
    }
    // Файл существует — это успех, данные будем читать в FindById
    return true;
}

bool Class_ProgSwd::chipCfg_FindById(uint32_t idcode, ChipConfig_t &cfg) {
    DEBUGLOGSWD("%s: searching for ID=0x%08x\n\r", __FUNCTION__, idcode);
    
    if (!_fs) {
        DEBUGLOGSWD("chipCfg_FindById: FS not initialized\n\r");
        return false;
    }
    if (!_fs->exists(SWD_CFG_JSON)) {
        DEBUGLOGSWD("chipCfg_FindById: %s not found\n\r", SWD_CFG_JSON);
        return false;
    }
    
    File file = _fs->open(SWD_CFG_JSON, "r");
    if (!file) {
        DEBUGLOGSWD("chipCfg_FindById: failed to open %s\n\r", SWD_CFG_JSON);
        return false;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        DEBUGLOGSWD("chipCfg_FindById: JSON parse error: %s\n\r", err.c_str());
        return false;
    }
    
    JsonArray chips = doc["chips"].as<JsonArray>();
    if (chips.isNull()) {
        DEBUGLOGSWD("chipCfg_FindById: no 'chips' array in %s\n\r", SWD_CFG_JSON);
        return false;
    }
    
    for (JsonObject chip : chips) {
        // IDCODE может быть строкой "0x..." или числом
        uint32_t chipIdcode = 0;
        if (chip["idcode"].is<const char*>()) {
            // Строковый hex-формат "0x2ba01477"
            chipIdcode = strtoul(chip["idcode"].as<const char*>(), NULL, 0);
        } else {
            chipIdcode = chip["idcode"].as<uint32_t>();
        }
        
        if (chipIdcode == idcode) {
            // Нашли чип — заполняем структуру
            cfg.idcode = chipIdcode;
            cfg.name = chip["name"].as<const char*>();
            cfg.family = chip["family"].as<const char*>();
            cfg.flash_size = chip["flash_size"].as<uint32_t>();
            
            // flash_start может быть строкой "0x..." или числом
            if (chip["flash_start"].is<const char*>()) {
                cfg.flash_start = strtoul(chip["flash_start"].as<const char*>(), NULL, 0);
            } else {
                cfg.flash_start = chip["flash_start"].as<uint32_t>();
            }
            
            cfg.page_size = chip["page_size"].as<uint32_t>();
            cfg.word_size = chip["word_size"].as<uint32_t>();
            
            // csw_value может быть строкой "0x..." или числом
            if (chip["csw_value"].is<const char*>()) {
                cfg.csw_value = strtoul(chip["csw_value"].as<const char*>(), NULL, 0);
            } else {
                cfg.csw_value = chip["csw_value"].as<uint32_t>();
            }
            
            DEBUGLOGSWD("chipCfg_FindById: found %s (family=%s) flash=%u start=0x%08x page=%u word=%u csw=0x%08x\n\r",
                cfg.name.c_str(), cfg.family.c_str(), cfg.flash_size, cfg.flash_start,
                cfg.page_size, cfg.word_size, cfg.csw_value);
            return true;
        }
    }
    
    DEBUGLOGSWD("chipCfg_FindById: ID=0x%08x not found in %s\n\r", idcode, SWD_CFG_JSON);
    return false;
}

// ========== Project Config Page (project.html) ==========

void Class_ProgSwd::web_ProjectInfo(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    String values = "";

    // Загружаем конфигурацию проекта
    CfgFile_ProgSwd_t cfg;
    cfg_FileStructGet(cfg);
    values += "progproj|" + cfg.project_name + "|input\n";
    values += "progchip|" + cfg.chip_name + "|select\n";

    request->send(200, "text/plain", values);
}

void Class_ProgSwd::web_ProjectSave(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    
    if (request->args() == 0) {
        request->send(500, "text/plain", "BAD ARGS");
        return;
    }

    CfgFile_ProgSwd_t newCfg;
    // Загружаем текущую конфигурацию как базовую
    cfg_FileStructGet(newCfg);

    for (uint8_t i = 0; i < request->args(); i++) {
        DEBUGLOGSWD("Arg %d: %s = %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
        if (request->argName(i) == "progproj") {
            String val = urldecode(request->arg(i));
            // Ограничение длины имени проекта
            if (val.length() > PROJECT_NAME_MAX_LEN) {
                val = val.substring(0, PROJECT_NAME_MAX_LEN);
            }
            newCfg.project_name = val;
            continue;
        }
        if (request->argName(i) == "progchip") {
            newCfg.chip_name = urldecode(request->arg(i));
            continue;
        }
    }

    if (newCfg.project_name.length() == 0) {
        request->send(500, "text/plain", "ERROR|Project name cannot be empty");
        return;
    }

    if (cfg_FileSaveFromWeb(newCfg) == 0) {
        request->send(200, "text/plain", "OK");
        DEBUGLOGSWD("web_ProjectSave: saved project='%s' chip='%s'\r\n",
            newCfg.project_name.c_str(), newCfg.chip_name.c_str());
    } else {
        request->send(500, "text/plain", "ERROR|Failed to save configuration");
    }
}

void Class_ProgSwd::web_ProjectChips(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    
    if (!_fs) {
        request->send(500, "text/plain", "ERROR|FS not initialized");
        return;
    }
    if (!_fs->exists(SWD_CFG_JSON)) {
        request->send(200, "text/json", "[]");
        return;
    }
    
    File file = _fs->open(SWD_CFG_JSON, "r");
    if (!file) {
        request->send(500, "text/plain", "ERROR|Failed to open chip config");
        return;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        request->send(500, "text/plain", "ERROR|JSON parse error");
        return;
    }
    
    // Формируем JSON-массив имён чипов для фронтенда
    String json = "[";
    JsonArray chips = doc["chips"].as<JsonArray>();
    if (!chips.isNull()) {
        bool first = true;
        for (JsonObject chip : chips) {
            if (!first) json += ",";
            json += "\"" + String(chip["name"].as<const char*>()) + "\"";
            first = false;
        }
    }
    json += "]";
    
    request->send(200, "text/json", json);
}

void Class_ProgSwd::web_ProjectChipInfo(AsyncWebServerRequest *request) {
    DEBUGLOGSWD("%s\n\r", __FUNCTION__);
    
    String chipName = "";
    if (request->args() > 0) {
        for (uint8_t i = 0; i < request->args(); i++) {
            if (request->argName(i) == "name") {
                chipName = urldecode(request->arg(i));
                break;
            }
        }
    }
    
    if (chipName.length() == 0) {
        request->send(500, "text/plain", "ERROR|No chip name provided");
        return;
    }
    
    if (!_fs || !_fs->exists(SWD_CFG_JSON)) {
        request->send(500, "text/plain", "ERROR|Chip config not found");
        return;
    }
    
    File file = _fs->open(SWD_CFG_JSON, "r");
    if (!file) {
        request->send(500, "text/plain", "ERROR|Failed to open chip config");
        return;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        request->send(500, "text/plain", "ERROR|JSON parse error");
        return;
    }
    
    JsonArray chips = doc["chips"].as<JsonArray>();
    if (chips.isNull()) {
        request->send(500, "text/plain", "ERROR|No chips array");
        return;
    }
    
    String values = "";
    for (JsonObject chip : chips) {
        if (strcmp(chip["name"].as<const char*>(), chipName.c_str()) == 0) {
            values += "chipinfo_name|" + String(chip["name"].as<const char*>()) + "|div\n";
            values += "chipinfo_idcode|" + String(chip["idcode"].as<const char*>()) + "|div\n";
            values += "chipinfo_family|" + String(chip["family"].as<const char*>()) + "|div\n";
            values += "chipinfo_flash|" + String(chip["flash_size"].as<uint32_t>()) + "|div\n";
            values += "chipinfo_page|" + String(chip["page_size"].as<uint32_t>()) + "|div\n";
            break;
        }
    }
    
    if (values.length() == 0) {
        values += "chipinfo_name|Unknown|div\n";
    }
    
    request->send(200, "text/plain", values);
}


