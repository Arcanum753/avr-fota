#include <cstddef>
#include <cstring>
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <esp32-hal-gpio.h>
#include <LittleFS.h>
#include "esp_rom_md5.h"
#endif

#include "module_prog.h"
#include "StringArray.h"

Class_ProgBase::Class_ProgBase(uint8_t in): _in(in){ }

#if defined(ESP32)
void Class_ProgBase::setFs(fs::LittleFSFS* fs)
{	_fs = fs;	}
#endif

bool Class_ProgBase::begin() {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	cfg_SetDefault();
	_migrateOldFiles();
	if (cfg_FileLoad() == false) {	cfg_FileSave();	}

	chipSpecificInit();

	return true;
}

void Class_ProgBase::begin(ModContext& ctx) {
#if defined(ESP32)
	_fs = ctx.fs;
#endif
	begin();
}

int Class_ProgBase::cfg_FileSaveFromWeb(CfgFile_ProgBase_t &_inStruct)  {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	CfgFile_Prog = _inStruct;
	bool ret = cfg_FileSave();
	if (ret) {
		return 0;
	}
	return 1;
}

int Class_ProgBase::cfg_FileStructGet(CfgFile_ProgBase_t &_inStruct)  {
	DEBUGLOGPROG(__PRETTY_FUNCTION__); DEBUGLOGPROG("\r\n");
	progerr_t _ret = ERROR_OK;
	if(!cfg_FileLoad()) {  return ERR_CFG; }
	_inStruct = CfgFile_Prog;
	return _ret ;
}

void Class_ProgBase::cfg_SetDefault() {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	CfgFile_Prog.project_name  = DEFAULT_PROG_PROJNAME;
	CfgFile_Prog.chip_name     = DEFAULT_CHIP_NAME;
}

bool Class_ProgBase::cfg_FileLoad() {
	DEBUGLOGPROG(__PRETTY_FUNCTION__); DEBUGLOGPROG("\r\n");
	JsonDocument doc;
	if (!ModClassJson.jsonFileLoadDoc(CONFIG_PROG_JSON, doc)) return false;
	CfgFile_Prog.project_name = doc["project"].as<String>();
	CfgFile_Prog.chip_name = doc["chip_name"].as<String>();
	return true;
}

bool Class_ProgBase::cfg_FileSave(){
	DEBUGLOGPROG("Save config PROJ\r\n");
	JsonDocument doc;
	ModClassJson.jsonFileLoadDoc(CONFIG_PROG_JSON, doc);
	doc["project"] = CfgFile_Prog.project_name;
	doc["chip_name"] = CfgFile_Prog.chip_name;
	return ModClassJson.jsonFileSaveDoc(CONFIG_PROG_JSON, doc);
}

bool Class_ProgBase::web_GetDiskInfoExe(String &_str)	{
	bool _ret = true;
	String values = "";
	size_t sizeAll = 0;
	size_t sizeUsed = 0;
	if (_fs != nullptr) {
#if defined(ESP32)
		esp_task_wdt_reset();
		sizeAll = _fs->totalBytes();
		sizeUsed = _fs->usedBytes();
		esp_task_wdt_reset();
#endif
	}

	size_t sizeFree = 0;
	if (sizeAll > sizeUsed) { sizeFree = sizeAll - sizeUsed; }

	values += "diskall|"   + (String)(sizeAll)  + "|div\n";
	values += "diskused|"  + (String)(sizeUsed) + "|div\n";
	values += "diskfree|"  + (String)(sizeFree) + "|div\n";
	_str = values;
	return _ret;
}

bool Class_ProgBase::web_GetFilesListExe(String &_str)	{
	bool _ret = true;

	JsonDocument listDoc;
	bool listLoaded = filelist_EnsureLoaded(listDoc);
	JsonArray arr;
	String lastSuccessFilename = "";
	if (listLoaded) {
		arr = listDoc.as<JsonArray>();
		lastSuccessFilename = filelist_GetLastSuccessFilename();
	}

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

	String json = "[";
	for (size_t i = 0; i < fileNames.size(); i++) {
		const String& fname = fileNames[i];
		const String& ftype = fileTypes[i];
		size_t fsize = fileSizes[i];

		String fileMD5 = "";
		String uploadDate = "";
		String progDate = "";
		String progStatus = "";
		String progTime = "";
		String progError = "";
		String progErrorStage = "";
		String progErrorPercent = "";
		String progSpeed = "";
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
					const char* ps = entry["prog_speed"].as<const char*>();
					if (ps) progSpeed = String(ps);
					break;
				}
			}
		}

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
		json += ",\"prog_speed\":\"";		json += progSpeed;			json += "\"";
		json += ",\"md5\":\"";			json += fileMD5;			json += "\"";
		json += ",\"is_last_success\":"; json += (isLastSuccess ? "true" : "false");
		json += "}";
	}

	json += "]";
	_str = json;
	return _ret;
}

// ========== File Upload ==========

#define UPLOAD_TIMEOUT_MS 30000

void Class_ProgBase::_cleanupStaleUpload() {
	if (_fsUploadFile) {
		_fsUploadFile.close();
		_fsUploadFile = File();
	}
	if (_uploadFilename.length() > 0) {
		DEBUGLOGPROG("Cleanup: removing stale upload file %s\r\n", _uploadFilename.c_str());
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

int Class_ProgBase::web_FileUpload2FS(String filename, size_t index, uint8_t *data, size_t len, bool final) {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	int _ret = 0;
	_hexFileUploadStatus = "";
#if defined(ESP32)
	static md5_context_t _md5Ctx;
#endif
	static bool _md5Initialized = false;
	static size_t _expectedFileSize = 0;

	if (index > 0 && _fsUploadFile && !final) {
		if (_uploadLastChunkTime > 0 && (millis() - _uploadLastChunkTime) > UPLOAD_TIMEOUT_MS) {
			DEBUGLOGPROG("UPLOAD TIMEOUT: no data for %u ms, cleaning up stale upload\r\n", (millis() - _uploadLastChunkTime));
			_cleanupStaleUpload();
			_md5Initialized = false;
			_expectedFileSize = 0;
		}
	}

	if (!index) {
		_cleanupStaleUpload();

		_uploadPercent = 0;
		_fileUploadBytes = 0;
		_fileUploadError = false;
		_expectedFileSize = _browserFileSize;
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File();
			DEBUGLOGPROG("WARN: previous upload file was open, closed.\r\n");
		}
		DEBUGLOGPROG("Name: %s\r\n", filename.c_str());

		if (filename.length() > MAX_FILENAME_LEN) {
			DEBUGLOGPROG("ERROR: filename too long (%u > %u): %s\r\n", filename.length(), MAX_FILENAME_LEN, filename.c_str());
			return -1;
		}

		// Проверка расширения файла — только .hex, .bin, .binary
		String lowerName = filename;
		lowerName.toLowerCase();
		if (!lowerName.endsWith(".hex") && !lowerName.endsWith(".bin") && !lowerName.endsWith(".binary")) {
			DEBUGLOGPROG("ERROR: invalid file extension: %s\r\n", filename.c_str());
			return -1;
		}

		if (!filename.startsWith("/")) {filename = "/" + filename;}
		_uploadFilename = filename;
		_fsUploadFile = _fs->open(filename, "w");
		DEBUGLOGPROG("First upload part.\r\n");

#if defined(ESP32)
		esp_rom_md5_init(&_md5Ctx);
#endif
		_md5Initialized = true;
	}

	if (_fsUploadFile && !_fileUploadError) {
		_uploadLastChunkTime = millis();
		DEBUGLOGPROG("Continue upload part. Size = %u\r\n", len);
		if (_fsUploadFile.write(data, len) != len) {
			_fileUploadError = true;
			_hexFileUploadStatus += "uploadstatus|error|div\n";
			_hexFileUploadStatus += "file|"     + _hexfileCheck    + "|div\n";
			_hexFileUploadStatus += "fileSize|" + (String)_fileUploadBytes + "|div\n";
		} else {
			_fileUploadBytes += len;
			if (_uploadFileSize > 0) {
				_uploadPercent = (_fileUploadBytes * 100) / _uploadFileSize;
			}
			if (_md5Initialized) {
#if defined(ESP32)
				esp_rom_md5_update(&_md5Ctx, data, len);
#endif
			}
		}
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
	}

	if (final) {
		if (_fsUploadFile) {
			_fsUploadFile.close();
			_fsUploadFile = File();
		}
		_uploadFilename = "";
		_uploadLastChunkTime = 0;

		if (!_fileUploadError && _expectedFileSize > 0 && _fileUploadBytes != _expectedFileSize) {
			_fileUploadError = true;
			DEBUGLOGPROG("SIZE MISMATCH! expected=%u received=%u, removing corrupted file %s\r\n", _expectedFileSize, _fileUploadBytes, filename.c_str());
			_fs->remove(filename);
			filelist_RemoveEntry(filename);
			_hexFileUploadStatus = "";
			_hexFileUploadStatus += "uploadstatus|error|div\n";
			_hexFileUploadStatus += "file|"      + filename           + "|div\n";
			_hexFileUploadStatus += "fileSize|"  + (String)_fileUploadBytes + "|div\n";
			_hexFileUploadStatus += "sizeerror|Size mismatch - file corrupted and removed|div\n";
			_fileUploadBytes = 0;
			_expectedFileSize = 0;
			_md5Initialized = false;
			return -1;
		}

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

			DEBUGLOGPROG("MD5 check: browser='%s' server='%s'\r\n", _browserFileMD5.c_str(), serverMD5.c_str());

			if (serverMD5 != _browserFileMD5) {
				_fileUploadError = true;
				DEBUGLOGPROG("MD5 MISMATCH! Removing corrupted file %s\r\n", filename.c_str());
				_fs->remove(filename);
				filelist_RemoveEntry(filename);
				_hexFileUploadStatus = "";
				_hexFileUploadStatus += "uploadstatus|error|div\n";
				_hexFileUploadStatus += "file|"     + filename          + "|div\n";
				_hexFileUploadStatus += "fileSize|" + (String)_fileUploadBytes + "|div\n";
				_hexFileUploadStatus += "md5error|MD5 mismatch - file corrupted and removed|div\n";
				_fileUploadBytes = 0;
				_expectedFileSize = 0;
				return -1;
			}

			String dateStr = NTP.getTimeDateString();
			if (dateStr.length() == 0) {
				dateStr = "unknown";
			}
			filelist_AddEntry(filename, dateStr, serverMD5);
			DEBUGLOGPROG("MD5 OK, added to filelist: %s date=%s md5=%s\r\n", filename.c_str(), dateStr.c_str(), serverMD5.c_str());
		}

		_ret = _fileUploadBytes;
		DEBUGLOGPROG("HexFileUpload final Size: %u\n", _fileUploadBytes);
		_hexfileCheck = filename;
		if (!_fileUploadError) {
			_hexFileUploadStatus = "";
			_hexFileUploadStatus += "status|ok|div\n";
			_hexFileUploadStatus += "file|"     + _hexfileCheck     + "|div\n";
			_hexFileUploadStatus += "fileSize|" + (String)_fileUploadBytes + "|div\n";
		}
		_fileUploadBytes = 0;
		_expectedFileSize = 0;
	}
	return _ret;
}

void Class_ProgBase::web_FileUpload2FS_Status(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__FUNCTION__);	DEBUGLOGPROG("\r\n");
	request->send(200, "text/plain", _hexFileUploadStatus);
}

void Class_ProgBase::web_FileUploadSize(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__FUNCTION__);	DEBUGLOGPROG("\r\n");
	if (request->args() > 0) {
		for (uint8_t i = 0; i < request->args(); i++) {
			if (request->argName(i) == "size") {
				_uploadPercent = 0;
				_uploadFileSize = request->arg(i).toInt();
				DEBUGLOGPROG("Upload size set: %u\r\n", _uploadFileSize);
				break;
			}
		}
	}
	request->send(200, "text/plain", "OK");
}

void Class_ProgBase::web_setMD5(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__FUNCTION__);	DEBUGLOGPROG("\r\n");
	_browserFileMD5 = "";
	_browserFileSize = 0;
	_browserFileName = "";
	_fileUploadError = false;

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGPROG("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
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

	if (filelist_FileExists(_browserFileName)) {
		DEBUGLOGPROG("setMD5: filename '%s' already exists!\r\n", _browserFileName.c_str());
		request->send(200, "text/plain", "ERROR|FILENAME_EXISTS|Filename already exists in filesystem");
		return;
	}

	if (_fs && _fs->exists(_browserFileName)) {
		DEBUGLOGPROG("setMD5: filename '%s' physically exists in FS!\r\n", _browserFileName.c_str());
		request->send(200, "text/plain", "ERROR|FILENAME_EXISTS|Filename already exists in filesystem");
		return;
	}

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
		DEBUGLOGPROG("setMD5: not enough space! need %u, free %u\r\n", _browserFileSize, sizeFree);
		request->send(200, "text/plain", "ERROR|NO_SPACE|Not enough free space on filesystem");
		return;
	}

	DEBUGLOGPROG("setMD5: OK md5=%s size=%u name=%s\r\n", _browserFileMD5.c_str(), _browserFileSize, _browserFileName.c_str());
	request->send(200, "text/plain", "OK");
}

// ========== Filelist Management ==========

bool Class_ProgBase::filelist_EnsureLoaded(JsonDocument &doc) {
	if (!_fs) return false;
	if (!_fs->exists(PROG_FILELIST_JSON)) {
		doc.clear();
		doc.to<JsonArray>();
		return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
	}
	if (!ModClassJson.jsonFileLoadDoc(PROG_FILELIST_JSON, doc)) {
		doc.clear();
		doc.to<JsonArray>();
		ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		return false;
	}
	if (!doc.is<JsonArray>()) {
		doc.clear();
		doc.to<JsonArray>();
	}
	return true;
}

void Class_ProgBase::filelist_Clear() {
	JsonDocument doc;
	doc.to<JsonArray>();
	ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_AddEntry(const String &filename, const String &upload_date, const String &md5) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

	String normalizedName = filename;
	if (normalizedName.startsWith("/")) {
		normalizedName = normalizedName.substring(1);
	}

	for (JsonObject entry : arr) {
		if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
			entry["upload_date"] = upload_date;
			entry["md5"] = md5;
			return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		}
	}

	JsonObject newEntry = arr.add<JsonObject>();
	newEntry["filename"] = normalizedName;
	newEntry["upload_date"] = upload_date;
	newEntry["md5"] = md5;

	return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error, const String &prog_time, const String &prog_error_stage, const String &prog_error_percent, const String &prog_speed) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

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
		if (prog_speed.length() > 0) {
			entry["prog_speed"] = prog_speed;
		} else {
			entry.remove("prog_speed");
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
			return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
		}
	}

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
	if (prog_speed.length() > 0) {
		newEntry["prog_speed"] = prog_speed;
	}
	DEBUGLOGPROG("filelist_SetProgStatus: created new entry for %s (was not in filelist)\r\n", normalizedName.c_str());
	return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

String Class_ProgBase::filelist_GetLastSuccessFilename() {
	JsonDocument doc;
	if (!filelist_EnsureLoaded(doc)) return "";
	JsonArray arr = doc.as<JsonArray>();

	String bestFilename = "";
	String bestDate = "";
	for (JsonObject entry : arr) {
		const char* status = entry["prog_status"].as<const char*>();
		if (status && strcmp(status, "ok") == 0) {
			const char* date = entry["prog_date"].as<const char*>();
			if (date && strlen(date) > 0) {
				if (bestDate.length() == 0 || strcmp(date, bestDate.c_str()) > 0) {
					bestDate = String(date);
					bestFilename = entry["filename"].as<const char*>();
				}
			}
		}
	}
	return bestFilename;
}

bool Class_ProgBase::filelist_RemoveEntry(const String &filename) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

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

	if (idx < 0) return false;

	arr.remove(idx);
	return ModClassJson.jsonFileSaveDoc(PROG_FILELIST_JSON, doc);
}

bool Class_ProgBase::filelist_FileExists(const String &filename) {
	JsonDocument doc;
	filelist_EnsureLoaded(doc);
	JsonArray arr = doc.as<JsonArray>();

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

String Class_ProgBase::file_ComputeMD5(const String &path) {
	if (!_fs || !_fs->exists(path)) {
		DEBUGLOGPROG("file_ComputeMD5: file not found %s\r\n", path.c_str());
		return "";
	}

	File f = _fs->open(path, "r");
	if (!f) {
		DEBUGLOGPROG("file_ComputeMD5: cannot open %s\r\n", path.c_str());
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

	DEBUGLOGPROG("file_ComputeMD5: %s -> %s\r\n", path.c_str(), hex);
	return String(hex);
#endif
}

void Class_ProgBase::web_FileUploadProgress(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__FUNCTION__);	DEBUGLOGPROG("\r\n");
	String values = "";

	if (_progResult == 0) {
		values += "progStatus|done|div\n";
		values += "progPercent|100|div\n";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			values += "progTime|" + (String)elapsed + "|div\n";
		}
		// Скорость из filelist (свежая запись уже сохранена)
		JsonDocument speedDoc;
		String speedStr = "";
		if (filelist_EnsureLoaded(speedDoc)) {
			JsonArray arr = speedDoc.as<JsonArray>();
			String normalizedName = _flashPath;
			if (normalizedName.startsWith("/")) normalizedName = normalizedName.substring(1);
			for (JsonObject entry : arr) {
				if (strcmp(entry["filename"].as<const char*>(), normalizedName.c_str()) == 0) {
					const char* ps = entry["prog_speed"].as<const char*>();
					if (ps) speedStr = String(ps);
					break;
				}
			}
		}
		if (speedStr.length() > 0) {
			values += "progSpeed|" + speedStr + "|div\n";
		}
		_progResult = -1;
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}
	if (_progResult > 0 || _progResult < -1) {
		values += "progStatus|error|div\n";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			values += "progTime|" + (String)elapsed + "|div\n";
		}
		String errorText = "";
		JsonDocument doc;
		if (filelist_EnsureLoaded(doc)) {
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
		_progResult = -1;
		_uploadPercent = 0;
		request->send(200, "text/plain", values);
		return;
	}

	if (_progRunning || isFlashBusy()) {
		uint8_t pct = getFlashPercent();
		_uploadPercent = pct;

		if (pct == 0 && isFlashBusy()) {
			values += "progStatus|starting|div\n";
		} else {
			values += "progStatus|running|div\n";
		}
		values += "progPercent|" + (String)pct + "|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	values += "percent|" + (String)_uploadPercent + "|div\n";
	request->send(200, "text/plain", values);
}

// ========== Chip Status ==========

bool Class_ProgBase::chip_IsConnected() {
	if (_chipStatusTime > 0 && (millis() - _chipStatusTime) < (CHIP_STATUS_TIMEOUT * 1000)) {
		return _chipConnected;
	}
	// Кеш устарел — сбрасываем состояние
	_chipConnected = false;
	return false;
}

void Class_ProgBase::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);

	if (_progRunning || isFlashBusy()) {
		request->send(200, "text/plain", "chipstatus|busy|div\n");
		return;
	}

	request->send(200, "text/plain", "chipstatus|checking|div\n");
}

// ========== Handlers ==========

void Class_ProgBase::web_GetFilesList(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	String json = "";
	web_GetFilesListExe(json);
	request->send(200, "text/json", json);
	json = "";
	DEBUGLOGPROG("List of *.hex *.bin *.binary files: %s \n\r", json);
}

void Class_ProgBase::web_GetDiskInfo(AsyncWebServerRequest *request) {
	DEBUGLOGPROG(__PRETTY_FUNCTION__);	DEBUGLOGPROG("\r\n");
	String values = "";
	web_GetDiskInfoExe(values);
	request->send(200, "text/json", values);
	values = "";
	DEBUGLOGPROG("Disk info: %s \n\r", values);
}

void Class_ProgBase::web_FileDelete(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}
	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (path == "/")			{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 	{path = "/" + path;}
	// Защита от path traversal
	if (path.indexOf("..") >= 0) { return request->send(400, "text/plain", "BAD PATH"); }
	DEBUGLOGPROG("handleFileDelete: %s\r\n", path.c_str());
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	if (!_fs->exists(path)) 	{	return request->send(404, "text/plain", "FileNotFound");	}
	_fs->remove(path);
#if defined(ESP32)
	esp_task_wdt_reset();
#endif
	filelist_RemoveEntry(path);
	DEBUGLOGPROG("handleFileDelete: removed '%s' from filelist\r\n", path.c_str());
	request->send(200, "text/plain", "");
}

// ========== Version ==========

void Class_ProgBase::html_ver_get(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);
	String values = "";
	values += "version|"    + getVersionStr()    + "|div\n";
	values += "gentime|"    + getGeneratedTime() + "|div\n";
	values += "gendate|"    + getCommitDateStr() + "|div\n";
	request->send(200, "text/plain", values);
}

// ========== Project Config Page ==========

void Class_ProgBase::web_ProjectInfo(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);
	String values = "";

	CfgFile_ProgBase_t cfg;
	cfg_FileStructGet(cfg);
	values += "progproj|" + cfg.project_name + "|input\n";
	values += "progchip|" + cfg.chip_name + "|select\n";

	request->send(200, "text/plain", values);
}

void Class_ProgBase::web_ProjectSave(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);

	if (request->args() == 0) {
		request->send(500, "text/plain", "BAD ARGS");
		return;
	}

	CfgFile_ProgBase_t newCfg;
	cfg_FileStructGet(newCfg);

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGPROG("Arg %d: %s = %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
		if (request->argName(i) == "progproj") {
			String val = urldecode(request->arg(i));
			if (val.length() > PROJECT_NAME_MAX_LEN) {
				val = val.substring(0, PROJECT_NAME_MAX_LEN);
			}
			newCfg.project_name = val;
			continue;
		}
		if (request->argName(i) == "progchip") {
			String val = urldecode(request->arg(i));
			if (val.length() > PROJECT_NAME_MAX_LEN) {
				val = val.substring(0, PROJECT_NAME_MAX_LEN);
			}
			newCfg.chip_name = val;
			continue;
		}
	}

	if (newCfg.project_name.length() == 0) {
		request->send(500, "text/plain", "ERROR|Project name cannot be empty");
		return;
	}

	if (cfg_FileSaveFromWeb(newCfg) == 0) {
		request->send(200, "text/plain", "OK");
		DEBUGLOGPROG("web_ProjectSave: saved project='%s' chip='%s'\r\n",
			newCfg.project_name.c_str(), newCfg.chip_name.c_str());
	} else {
		request->send(500, "text/plain", "ERROR|Failed to save configuration");
	}
}

void Class_ProgBase::web_ProjectChips(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);

	if (!_fs) {
		request->send(500, "text/plain", "ERROR|FS not initialized");
		return;
	}
	const char* cfgPath = getChipCfgJsonPath();
	if (!_fs->exists(cfgPath)) {
		request->send(200, "text/json", "[]");
		return;
	}

	File file = _fs->open(cfgPath, "r");
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

void Class_ProgBase::web_ProjectChipInfo(AsyncWebServerRequest *request) {
	DEBUGLOGPROG("%s\n\r", __FUNCTION__);

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

	const char* cfgPath = getChipCfgJsonPath();
	if (!_fs || !_fs->exists(cfgPath)) {
		request->send(500, "text/plain", "ERROR|Chip config not found");
		return;
	}

	File file = _fs->open(cfgPath, "r");
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

	JsonDocument resp;
	JsonObject out = resp.to<JsonObject>();
	for (JsonObject chip : chips) {
		if (strcmp(chip["name"].as<const char*>(), chipName.c_str()) == 0) {
			out["name"] = chip["name"].as<const char*>();
			_chipInfoAppendFields(out, chip);
			break;
		}
	}

	if (out.size() == 0) {
		out["name"] = "Unknown";
	}

	String jsonResp;
	serializeJson(out, jsonResp);
	request->send(200, "application/json", jsonResp);
}

// Переопределяется в субмодулях для добавления специфичных полей (signature, family, ...)
void Class_ProgBase::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
}

// ========== Web Init ==========

void Class_ProgBase::registerCommonRoutes() {
	ESPHTTPServer.on("/prog/diskinfo", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_GetDiskInfo(request);
	});

	ESPHTTPServer.on("/prog/fileslist", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_GetFilesList(request);
	});

	ESPHTTPServer.on("/prog/delete", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_FileDelete(request);
	});

	ESPHTTPServer.on("/prog/uploadfile", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		request->send(200, "text/plain", "uploadstatus|begin|div");
	}, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
		web_FileUpload2FS(filename, index, data, len, final);
	});

	ESPHTTPServer.on("/prog/uploadstat", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_FileUpload2FS_Status(request);
	});

	ESPHTTPServer.on("/prog/setmd5", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_setMD5(request);
	});

	ESPHTTPServer.on("/prog/uploadsize", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_FileUploadSize(request);
	});

	ESPHTTPServer.on("/prog/progress", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_FileUploadProgress(request);
	});

	ESPHTTPServer.on("/prog/flash", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_FileUpload2Chip(request);
	});

	ESPHTTPServer.on("/prog/ver", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		html_ver_get(request);
	});

	ESPHTTPServer.on("/prog/chipstatus", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_CheckChipStatus(request);
	});

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

	ESPHTTPServer.on("/project", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		ESPHTTPServer.handleFileRead("/web/project.html", request);
	});

	ESPHTTPServer.on("/prog", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		ESPHTTPServer.handleFileRead("/web/prog.html", request);
	});

	registerCustomRoutes();
}

void Class_ProgBase::web_Init() {
	registerCommonRoutes();
}

// Миграция старых файлов конфигов и filelist в новые пути
void Class_ProgBase::_migrateOldFiles() {
	if (!_fs) return;

	// Старые пути config (ISPs и SWD)
	const char* oldConfigs[] = {"/config_prog_isp.json", "/config_prog_swd.json"};
	const char* oldFilelists[] = {"/isp_filelist.json", "/swd_filelist.json"};

	// Миграция конфига
	if (!_fs->exists(CONFIG_PROG_JSON)) {
		for (auto oldPath : oldConfigs) {
			if (_fs->exists(oldPath)) {
				DEBUGLOGPROG("_migrateOldFiles: migrating config %s -> %s\r\n", oldPath, CONFIG_PROG_JSON);
				File src = _fs->open(oldPath, "r");
				if (src) {
					JsonDocument doc;
					DeserializationError err = deserializeJson(doc, src);
					src.close();
					if (!err) {
						File dst = _fs->open(CONFIG_PROG_JSON, "w");
						if (dst) {
							serializeJson(doc, dst);
							dst.flush();
							dst.close();
							_fs->remove(oldPath);
							DEBUGLOGPROG("_migrateOldFiles: config migrated and old file removed\r\n");
						}
					}
				}
				break;
			}
		}
	}

	// Миграция filelist
	if (!_fs->exists(PROG_FILELIST_JSON)) {
		for (auto oldPath : oldFilelists) {
			if (_fs->exists(oldPath)) {
				DEBUGLOGPROG("_migrateOldFiles: migrating filelist %s -> %s\r\n", oldPath, PROG_FILELIST_JSON);
				File src = _fs->open(oldPath, "r");
				if (src) {
					JsonDocument doc;
					DeserializationError err = deserializeJson(doc, src);
					src.close();
					if (!err && doc.is<JsonArray>()) {
						File dst = _fs->open(PROG_FILELIST_JSON, "w");
						if (dst) {
							serializeJson(doc, dst);
							dst.flush();
							dst.close();
							_fs->remove(oldPath);
							DEBUGLOGPROG("_migrateOldFiles: filelist migrated and old file removed\r\n");
						}
					}
				}
				break;
			}
		}
	}
}
