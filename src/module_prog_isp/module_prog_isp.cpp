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

#include "prog_isp.h"
#include "module_prog_isp.h"
#include "common.h"
#include "module_prog_isp_version.h"
#include "StringArray.h"

Class_ProgIsp progIsp(0);
Class_ProgIsp::Class_ProgIsp(uint8_t in): _in(in){ }

// esp32 flash file system
#if defined(ESP32)
    void Class_ProgIsp::setFs(fs::SPIFFSFS* fs)
{	_fs = fs;	}
#endif

bool Class_ProgIsp::begin (){
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
    cfg_SetDefault();
    if (cfg_FileLoad() == false) {	cfg_FileSave();	}
	
	avrprog.begin();
	
	// прокидываем указатель на файловую систему в класс программатора
	avrprog.setFs(_fs);
	//TODO return init result
    return true;
}


// TODO навести тут порядок с именаяи GET/POST запросов.
// all about webAPI. Set hooks
void  Class_ProgIsp::web_Init()	{
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	//avr.html vvv
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
//avr.html ^^^

//avrcfg.html vvv
    // AVR-specific: fuses
    ESPHTTPServer.on("/avr/fuseread",HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        avrFusesRead(request);
    });
    ESPHTTPServer.on("/avr/fusewrite", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        avrWebFusesWrite(request);
    });

    // Сначала специфичные роуты /avr/*, потом общий /avrcfg
    ESPHTTPServer.on("/avr/info", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        web_AvrCfgInfo(request);
    });

    ESPHTTPServer.on("/avr/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        web_AvrCfgSave(request);
    });

    ESPHTTPServer.on("/avr/readsignature", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
		web_AvrCfgReadSignature(request);
    });

    // Общий роут, отдающий HTML — последним
    ESPHTTPServer.on("/avrcfg", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        ESPHTTPServer.handleFileRead("/web/avrcfg.html", request);
    });
//avrcfg.html ^^^

}


// cfg section
int Class_ProgIsp::cfg_FileSaveFromWeb(CfgFile_ProgIsp_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	CfgFile_ProgIsp	=  _inStruct;
	int _ret =  (int)cfg_FileSave();
	return _ret ;
}

int  Class_ProgIsp::cfg_FileStructGet(CfgFile_ProgIsp_t &_inStruct)  {
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    progerr_t _ret = ERROR_OK;
    if(!cfg_FileLoad()) {  return ERR_CFG; }
    _inStruct = CfgFile_ProgIsp;
    return _ret ;
}

void Class_ProgIsp::cfg_SetDefault() {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	// CfgFile_ProgIsp.programmer_type	= DEFAULT_PROG_TYPE;
    CfgFile_ProgIsp.project_name  	= DEFAULT_PROG_PROJNAME;
    CfgFile_ProgIsp.chip_size      	= DEFAULT_chipsize;
}

bool Class_ProgIsp::cfg_FileLoad() {
	DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
	JsonDocument jsonDoc;
	if (ModClassJson.load_jsonDoc(CONFIG_PROG_JSON, jsonDoc) == false ){	return false;	}
	// CfgFile_ProgIsp.programmer_type	= jsonDoc["type"].as<const char *>();
    CfgFile_ProgIsp.project_name		= jsonDoc["project"].as<const char *>();
    CfgFile_ProgIsp.chip_size			= jsonDoc["chipsize"].as<uint32_t>();
	return true;
}

bool Class_ProgIsp::cfg_FileSave(){
	DEBUGLOGISP("Save config PROJ\r\n");
	JsonDocument jsonDoc;
	// jsonDoc["type"]			= CfgFile_ProgIsp.programmer_type;
    jsonDoc["project"]		= CfgFile_ProgIsp.project_name;
    jsonDoc["chipsize"]     = CfgFile_ProgIsp.chip_size;
	return ModClassJson.save_jsonDoc(jsonDoc, CONFIG_PROG_JSON);
}


bool Class_ProgIsp::web_GetDiskInfoExe(String &_str)	{
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

bool Class_ProgIsp::web_GetFilesListExe(String &_str)	{
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
					const char* pt = entry["prog_time"].as<const char*>();
					if (pt) progTime = String(pt);
					const char* pe = entry["prog_error"].as<const char*>();
					if (pe) progError = String(pe);
					const char* pes = entry["prog_error_stage"].as<const char*>();
					if (pes) progErrorStage = String(pes);
					const char* pep = entry["prog_error_percent"].as<const char*>();
					if (pep) progErrorPercent = String(pep);
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
		json += ",\"prog_error_percent\":\"";	json += progErrorPercent;	json += "\"";
		json += ",\"md5\":\"";			json += fileMD5;			json += "\"";
		json += ",\"is_last_success\":"; json += (isLastSuccess ? "true" : "false");
		json += "}";

	}

	json += "]";
	_str = json;
	return _ret;
}

// Callback после завершения EERTOS-кооперативной прошивки (успех или ошибка)
void Class_ProgIsp::onFlashComplete() {
	if (avrprog.isFlashError()) {
		DEBUGLOGISP("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		// Получаем текст ошибки из программатора
		String errorText = avrprog.getFlashErrorString();
		// Получаем стадию и процент ошибки из программатора
		String errorStage = avrprog.getFlashErrorStage();
		String errorPercent = String(avrprog.getFlashErrorPercent());
		// Вычисляем затраченное время (даже при ошибке)
		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		// Сохраняем статус ошибки в filelist с текстом ошибки, стадией и процентом
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, errorPercent);
		_progResult = 1;  // сигнал ошибки для web_FileUploadProgress
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGISP("Programming error: %s, stage=%s, pct=%s, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent.c_str());
	} else {
		DEBUGLOGISP("onFlashComplete: success for %s\r\n", _flashPath.c_str());
		
		// Вычисляем затраченное время
		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		// Сохраняем статус успеха в filelist с временем прошивки
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr);
		
		DEBUGLOGISP("Programming success, saved prog date to filelist: %s, time=%sms\r\n", _flashNtpStr.c_str(), elapsedStr.c_str());
		
		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;
		
		DEBUGLOGISP("Programming end \r\n");
	}
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
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (path == "/")			{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 	{path = "/" + path;}
	DEBUGLOGISP("handleFileDelete: %s\r\n", path.c_str());
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
	DEBUGLOGISP("handleFileDelete: removed '%s' from filelist\r\n", path.c_str());
	request->send(200, "text/plain", "");
}

// загрузчик файла из фронтенда с контролем MD5
int Class_ProgIsp::web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
	int  _ret= 0;
	_hexFileUploadStatus = "";
#if defined(ESP32)
	static md5_context_t _md5Ctx;
#endif
	static bool _md5Initialized = false;
	static size_t _expectedFileSize = 0;  // сохраняем ожидаемый размер локально
	// Start
	if (!index) {
		_uploadPercent = 0;
		_fileUploadBytes = 0;
		_fileUploadError = false;
		_expectedFileSize = _browserFileSize;  // фиксируем размер на момент старта
		// если предыдущий файл не закрыт (например, загрузка прервана) — закрываем
		if (_fsUploadFile) {
			_fsUploadFile.close();
			DEBUGLOGISP("WARN: previous upload file was open, closed.\r\n");
		}
		DEBUGLOGISP("Name: %s\r\n", filename.c_str());

		// Проверка длины имени файла (SPIFFS ограничение 32 байта)
		if (filename.length() > MAX_FILENAME_LEN) {
			DEBUGLOGISP("ERROR: filename too long (%u > %u): %s\r\n", filename.length(), MAX_FILENAME_LEN, filename.c_str());
			return -1;
		}

		if (!filename.startsWith("/")) {filename = "/" + filename;}
		_fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGISP("First upload part.\r\n");
		
		// Инициализируем MD5-контекст
#if defined(ESP32)
		esp_rom_md5_init(&_md5Ctx);
#endif
		_md5Initialized = true;
	}
	// Continue
	if (_fsUploadFile && !_fileUploadError) {
		DEBUGLOGISP("Continue upload part. Size = %u\r\n", len);
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
	}
	// End
	if (final) {
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File(); // сбрасываем в "пустой" файл
		}
		
		// Проверяем размер файла (используем _expectedFileSize, сохранённый на старте)
		if (!_fileUploadError && _expectedFileSize > 0 && _fileUploadBytes != _expectedFileSize) {
			_fileUploadError = true;
			DEBUGLOGISP("SIZE MISMATCH! expected=%u received=%u, removing corrupted file %s\r\n", _expectedFileSize, _fileUploadBytes, filename.c_str());
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
			
			DEBUGLOGISP("MD5 check: browser='%s' server='%s'\r\n", _browserFileMD5.c_str(), serverMD5.c_str());
			
		if (serverMD5 != _browserFileMD5) {
				// MD5 не совпадает — удаляем файл, сообщаем об ошибке
				_fileUploadError = true;
				DEBUGLOGISP("MD5 MISMATCH! Removing corrupted file %s\r\n", filename.c_str());
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
			DEBUGLOGISP("MD5 OK, added to filelist: %s date=%s md5=%s\r\n", filename.c_str(), dateStr.c_str(), serverMD5.c_str());
		}
		
		_ret = _fileUploadBytes;
		DEBUGLOGISP("HexFileUpload final Size: %u\n", _fileUploadBytes);
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


void Class_ProgIsp::web_FileUpload2FS_Status(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void Class_ProgIsp::web_FileUpload2Chip(AsyncWebServerRequest *request) {

	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	
	// Защита от повторного входа — если прошивка уже идёт
	if (avrprog.isFlashBusy()) {
		DEBUGLOGISP("web_FileUpload2Chip: BUSY — programming already in progress\r\n");
		return request->send(423, "text/plain", "busy");
	}
	if (_progRunning) {
		DEBUGLOGISP("web_FileUpload2Chip: _progRunning already true\r\n");
		return request->send(423, "text/plain", "busy");
	}
	
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGISP("\t upload status: %s\r\n", path.c_str());
	
	// Сохраняем параметры для onFlashComplete()
	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();
	
	// ===== Новая логика: читаем сигнатуру AVR, ищем в avrisp_cfg.json, подставляем параметры =====
	uint32_t flashStart = 0;  // AVR всегда с адреса 0
	uint32_t chipMemSize = CfgFile_ProgIsp.chip_size;
	uint32_t pageSize = 128;  // дефолтный размер страницы
	
	// Синхронно читаем сигнатуру чипа (быстрая SPI-транзакция)
	String signature = avrprog.chipSignRead();
	
	if (signature.length() > 0 && signature != "0x000000") {
		DEBUGLOGISP("web_FileUpload2Chip: detected chip signature=%s\n\r", signature.c_str());
		
		// Ищем чип в avrisp_cfg.json
		ChipConfigAvr_t chipCfg;
		if (chipCfg_FindBySignature(signature, chipCfg)) {
			// Нашли — используем параметры из конфига
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			DEBUGLOGISP("web_FileUpload2Chip: using config for %s (flash=%u page=%u)\n\r",
				chipCfg.name.c_str(), chipMemSize, pageSize);
		} else {
			// Чип не найден в конфиге — используем дефолтные параметры
			DEBUGLOGISP("web_FileUpload2Chip: chip signature=%s not in avrisp_cfg.json, using defaults\n\r", signature.c_str());
		}
	} else {
		DEBUGLOGISP("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	}
	
	// Запускаем EERTOS-кооперативную прошивку с параметрами из конфига чипа
	if (!avrprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize)) {
		DEBUGLOGISP("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}
	
	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();
	
	// Регистрируем задачу в EERTOS — она будет вызываться каждый loop()
	avrprog.beginFlashStep();
	
	// Отвечаем сразу, не блокируя HTTP
	request->send(200, "text/plain", "ok");
	DEBUGLOGISP("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}


void Class_ProgIsp::web_FileUploadSize(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");
	if (request->args() > 0) {
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "size") {
				_uploadPercent = 0;	// сброс процента при установке нового размера файла
				_uploadFileSize = request->arg(i).toInt();
				DEBUGLOGISP("Upload size set: %u\r\n", _uploadFileSize);
				break;
			}
		}
	}
	request->send(200, "text/plain", "OK");
}

// ========== MD5 & Pre-upload Validation ==========
void Class_ProgIsp::web_setMD5(AsyncWebServerRequest *request) {
	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");
	_browserFileMD5 = "";
	_browserFileSize = 0;
	_browserFileName = "";
	_fileUploadError = false;

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
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
		DEBUGLOGISP("setMD5: filename '%s' already exists!\r\n", _browserFileName.c_str());
		request->send(200, "text/plain", "ERROR|FILENAME_EXISTS|Filename already exists in filesystem");
		return;
	}

	// Если файла нет в filelist, но он физически есть в ФС — тоже блокируем
	if (_fs && _fs->exists(_browserFileName)) {
		DEBUGLOGISP("setMD5: filename '%s' physically exists in FS!\r\n", _browserFileName.c_str());
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
		DEBUGLOGISP("setMD5: not enough space! need %u, free %u\r\n", _browserFileSize, sizeFree);
		request->send(200, "text/plain", "ERROR|NO_SPACE|Not enough free space on filesystem");
		return;
	}

	DEBUGLOGISP("setMD5: OK md5=%s size=%u name=%s\r\n", _browserFileMD5.c_str(), _browserFileSize, _browserFileName.c_str());
	request->send(200, "text/plain", "OK");
}

// ========== Filelist Management ==========

bool Class_ProgIsp::filelist_Load(JsonDocument &doc) {
	if (!_fs) return false;
	File file = _fs->open(ISP_FILELIST_JSON, "r");
	if (!file) {
		DEBUGLOGISP("filelist_Load: %s not found, creating empty filelist\r\n", ISP_FILELIST_JSON);
		doc.clear();
		doc.to<JsonArray>();
		// Создаём пустой filelist на диске
		filelist_Save(doc);
		return true; // нет файла — не ошибка, пустой массив
	}

	DeserializationError err = deserializeJson(doc, file);
	file.close();
	if (err) {
		DEBUGLOGISP("filelist_Load: JSON parse error: %s\r\n", err.c_str());
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

bool Class_ProgIsp::filelist_Save(JsonDocument &doc) {
	if (!_fs) return false;
	File file = _fs->open(ISP_FILELIST_JSON, "w");
	if (!file) {
		DEBUGLOGISP("filelist_Save: failed to open %s for writing\r\n", ISP_FILELIST_JSON);
		return false;
	}
	serializeJson(doc, file);
	file.flush();
	file.close();
	DEBUGLOGISP("filelist_Save: saved %d entries\r\n", doc.as<JsonArray>().size());
	return true;
}

bool Class_ProgIsp::filelist_AddEntry(const String &filename, const String &upload_date, const String &md5) {
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

bool Class_ProgIsp::filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error, const String &prog_time, const String &prog_error_stage, const String &prog_error_percent) {
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
			if (prog_time.length() > 0) {
				entry["prog_time"] = prog_time;
			} else {
				entry.remove("prog_time");
			}
			if (prog_error.length() > 0) {
				entry["prog_error"] = prog_error;
			} else {
				entry.remove("prog_error");
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
	if (prog_time.length() > 0) {
		newEntry["prog_time"] = prog_time;
	}
	if (prog_error.length() > 0) {
		newEntry["prog_error"] = prog_error;
	}
	if (prog_error_stage.length() > 0) {
		newEntry["prog_error_stage"] = prog_error_stage;
	}
	if (prog_error_percent.length() > 0) {
		newEntry["prog_error_percent"] = prog_error_percent;
	}
	DEBUGLOGISP("filelist_SetProgStatus: created new entry for %s (was not in filelist)\r\n", normalizedName.c_str());
	return filelist_Save(doc);
}

String Class_ProgIsp::filelist_GetLastSuccessFilename() {
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

bool Class_ProgIsp::filelist_RemoveEntry(const String &filename) {
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

bool Class_ProgIsp::filelist_FileExists(const String &filename) {
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

String Class_ProgIsp::file_ComputeMD5(const String &path) {
	if (!_fs || !_fs->exists(path)) {
		DEBUGLOGISP("file_ComputeMD5: file not found %s\r\n", path.c_str());
		return "";
	}

	File f = _fs->open(path, "r");
	if (!f) {
		DEBUGLOGISP("file_ComputeMD5: cannot open %s\r\n", path.c_str());
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

	DEBUGLOGISP("file_ComputeMD5: %s -> %s\r\n", path.c_str(), hex);
	return String(hex);
#endif
}

void Class_ProgIsp::web_FileUploadProgress(AsyncWebServerRequest *request) {

	DEBUGLOGISP(__FUNCTION__);	DEBUGLOGISP("\r\n");
	String values = "";
	
	// Если идёт программирование AVR — отдаём статус и процент
	if (_progRunning || avrprog.isFlashBusy()) {
		uint8_t pct = avrprog.getPercent();
		_uploadPercent = pct;
		
		// Если процент 0 и прошивка только началась — отдаём "starting"
		if (pct == 0 && avrprog.isFlashBusy()) {
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

	// Обычный процент загрузки файла
	values += "percent|" + (String)_uploadPercent + "|div\n";
	request->send(200, "text/plain", values);
}

// avr.html ^^^

String Class_ProgIsp::getVersionStr(){ return String(MODULE_PROG_ISP_VERSION); }
String Class_ProgIsp::getGeneratedTime(){ return String(MODULE_PROG_ISP_GENERATED_TIME); }
String Class_ProgIsp::getCommitDateStr(){ return String(MODULE_PROG_ISP_COMMIT_DATE_STR);	}

void Class_ProgIsp::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    String values = "";
    values += "ispversion|"     + getVersionStr()    + "|dev\n";
    values += "ispgentime|"     + getGeneratedTime() + "|dev\n";
    values += "ispgendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}

// ========== Chip Config from avrisp_cfg.json ==========

bool Class_ProgIsp::chipCfg_Load() {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    if (!_fs) {
        DEBUGLOGISP("chipCfg_Load: FS not initialized\n\r");
        return false;
    }
    if (!_fs->exists(AVRISP_CFG_JSON)) {
        DEBUGLOGISP("chipCfg_Load: %s not found, using defaults\n\r", AVRISP_CFG_JSON);
        return false;
    }
    // Файл существует — это успех, данные будем читать в FindBySignature
    return true;
}

bool Class_ProgIsp::chipCfg_FindBySignature(const String &signature, ChipConfigAvr_t &cfg) {
    DEBUGLOGISP("%s: searching for signature=%s\n\r", __FUNCTION__, signature.c_str());
    
    if (!_fs) {
        DEBUGLOGISP("chipCfg_FindBySignature: FS not initialized\n\r");
        return false;
    }
    if (!_fs->exists(AVRISP_CFG_JSON)) {
        DEBUGLOGISP("chipCfg_FindBySignature: %s not found\n\r", AVRISP_CFG_JSON);
        return false;
    }
    
    File file = _fs->open(AVRISP_CFG_JSON, "r");
    if (!file) {
        DEBUGLOGISP("chipCfg_FindBySignature: failed to open %s\n\r", AVRISP_CFG_JSON);
        return false;
    }
    
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    
    if (err) {
        DEBUGLOGISP("chipCfg_FindBySignature: JSON parse error: %s\n\r", err.c_str());
        return false;
    }
    
    JsonArray chips = doc["chips"].as<JsonArray>();
    if (chips.isNull()) {
        DEBUGLOGISP("chipCfg_FindBySignature: no 'chips' array in %s\n\r", AVRISP_CFG_JSON);
        return false;
    }
    
    // Нормализуем сигнатуру для сравнения: убираем "0x" префикс если есть, приводим к нижнему регистру
    String searchSig = signature;
    if (searchSig.startsWith("0x") || searchSig.startsWith("0X")) {
        searchSig = searchSig.substring(2);
    }
    searchSig.toLowerCase();
    
    for (JsonObject chip : chips) {
        String chipSig = chip["signature"].as<const char*>();
        if (chipSig.startsWith("0x") || chipSig.startsWith("0X")) {
            chipSig = chipSig.substring(2);
        }
        chipSig.toLowerCase();
        
        if (chipSig == searchSig) {
            // Нашли чип — заполняем структуру
            cfg.signature = chip["signature"].as<const char*>();
            cfg.name = chip["name"].as<const char*>();
            cfg.flash_size = chip["flash_size"].as<uint32_t>();
            cfg.page_size = chip["page_size"].as<uint32_t>();
            
            DEBUGLOGISP("chipCfg_FindBySignature: found %s (signature=%s) flash=%u page=%u\n\r",
                cfg.name.c_str(), cfg.signature.c_str(), cfg.flash_size, cfg.page_size);
            return true;
        }
    }
    
    DEBUGLOGISP("chipCfg_FindBySignature: signature=%s not found in %s\n\r", signature.c_str(), AVRISP_CFG_JSON);
    return false;
}

// ========== AVR Fuses ==========

void Class_ProgIsp::avrFusesRead(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    String values = "";
    
    AVRISP_fuses_t fuses;
    avrprog.chipFusesRead(fuses);
    
    values += "avrfuselow|"   + String(fuses.low, HEX)  + "|div\n";
    values += "avrfusehigh|"  + String(fuses.high, HEX) + "|div\n";
    values += "avrfuseext|"   + String(fuses.ext, HEX)  + "|div\n";
    values += "avrfuseprot|"  + String(fuses.lock, HEX) + "|div\n";
    
    request->send(200, "text/plain", values);
}

void Class_ProgIsp::avrWebFusesWrite(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    
    if (request->args() == 0) {
        request->send(500, "text/plain", "BAD ARGS");
        return;
    }
    
    uint8_t high = 0, low = 0, lock = 0, ext = 0;
    bool hasHigh = false, hasLow = false, hasLock = false, hasExt = false;
    
    for (uint8_t i = 0; i < request->args(); i++) {
        if (request->argName(i) == "avrfusehigh") {
            high = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
            hasHigh = true;
        } else if (request->argName(i) == "avrfuselow") {
            low = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
            hasLow = true;
        } else if (request->argName(i) == "avrfuseprot") {
            lock = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
            hasLock = true;
        } else if (request->argName(i) == "avrfuseext") {
            ext = (uint8_t)strtoul(request->arg(i).c_str(), NULL, 16);
            hasExt = true;
        }
    }
    
    // Если ни один фьюз не передан — ошибка
    if (!hasHigh && !hasLow && !hasLock && !hasExt) {
        request->send(500, "text/plain", "BAD ARGS: no fuse values provided");
        return;
    }
    
    avrprog.chipFusesWrite(high, low, lock, ext);
    request->send(200, "text/plain", "OK");
}

// ========== AVR Config Page (avrcfg.html) ==========

void Class_ProgIsp::web_AvrCfgInfo(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    String values = "";

    // Загружаем конфигурацию проекта
    CfgFile_ProgIsp_t cfg;
    cfg_FileStructGet(cfg);
    values += "projname|" + cfg.project_name + "|input\n";
    values += "chipsize|" + (String)cfg.chip_size + "|input\n";

    // Информация о подключенном чипе из _chipIdstr (обновляется при вызове web_AvrCfgReadSignature)
    if (_chipIdstr.length() > 0 && _chipIdstr != "0x000000") {
        values += "signature|" + _chipIdstr + "|div\n";
        // Ищем имя чипа в конфиге
        ChipConfigAvr_t chipCfg;
        if (chipCfg_FindBySignature(_chipIdstr, chipCfg)) {
            values += "chipname|" + chipCfg.name + "|div\n";
        } else {
            values += "chipname|Unknown|div\n";
        }
    } else {
        values += "signature|N/A|div\n";
        values += "chipname|N/A|div\n";
    }

    request->send(200, "text/plain", values);
}

void Class_ProgIsp::web_AvrCfgSave(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    
    if (request->args() == 0) {
        request->send(500, "text/plain", "BAD ARGS");
        return;
    }

    CfgFile_ProgIsp_t newCfg;
    // Загружаем текущую конфигурацию как базовую
    cfg_FileStructGet(newCfg);

    for (uint8_t i = 0; i < request->args(); i++) {
        DEBUGLOGISP("Arg %d: %s = %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
        if (request->argName(i) == "projname") {
            newCfg.project_name = urldecode(request->arg(i));
            continue;
        }
        if (request->argName(i) == "chipsize") {
            newCfg.chip_size = request->arg(i).toInt();
            continue;
        }
    }

    if (newCfg.project_name.length() == 0) {
        request->send(500, "text/plain", "ERROR|Project name cannot be empty");
        return;
    }
    if (newCfg.chip_size == 0) {
        request->send(500, "text/plain", "ERROR|Invalid chip memory size");
        return;
    }

    if (cfg_FileSaveFromWeb(newCfg) == 0) {
        request->send(200, "text/plain", "OK");
        DEBUGLOGISP("web_AvrCfgSave: saved project='%s' chipsize=%u\r\n",
            newCfg.project_name.c_str(), newCfg.chip_size);
    } else {
        request->send(500, "text/plain", "ERROR|Failed to save configuration");
    }
}

void Class_ProgIsp::web_AvrCfgReadSignature(AsyncWebServerRequest *request) {
    DEBUGLOGISP("%s\n\r", __FUNCTION__);
    String values = "";
	_chipIdstr =  avrprog.chipSignRead();
    values += "signature|" + _chipIdstr + "|div\n";
    request->send(200, "text/plain", values);
}
