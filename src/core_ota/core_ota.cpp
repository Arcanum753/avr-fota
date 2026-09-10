#if defined(ESP32)
#include <LittleFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#include <avr/pgmspace.h>
#endif

#include"version.h"
#include <ArduinoOTA.h>
#include "core_web/FSWebServerLib.h"
#include "common/common.h"
#include "core_ota.h"
#include "core_ota_version.h"
#include "core_sys/core_sys.h"
#include "core_led/core_led.h"

CLASS_CORE_OTA core_ota;

// Global flag to prevent double _fs->end() crashes
bool _ota_fsEndCalled = false;

// ============================================================
// Паттерны светодиодной индикации обновления (кассета модуля)
// ============================================================

static const char patOtaFw[]    PROGMEM = "*.*.*.*.*.*.*.*.*.*";
static const char patOtaFs[]    PROGMEM = "***...***...***...";
static const char patOtaErr[]   PROGMEM = "*.*.*";

#if ESP32
    void CLASS_CORE_OTA::setFs(fs::LittleFSFS* fs)
#elif defined(ESP8266)
    void CLASS_CORE_OTA::setFs(FS* fs)
#endif
{	_fs = fs;	}

// ============================================================
// begin()
// ============================================================

void CLASS_CORE_OTA::begin(String _hostname, String _password){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	prepareSizesForUpdate();
	ConfigureOTA(_hostname, _password);
 }

void CLASS_CORE_OTA::begin(ModContext& ctx){
	_fs = ctx.fs;
	begin(ctx.hostname, ctx.password);
}

// ============================================================
// web_Init()
// ============================================================

 void CLASS_CORE_OTA::registerCommonRoutes() {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");

    ESPHTTPServer.on("/update/setmd5", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        html_md5_set(request);
    });

    ESPHTTPServer.on("/update/firmwarefilecheck", [this](AsyncWebServerRequest *request) {
            if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
            html_filename_check(request);
    });

    ESPHTTPServer.on("/update/progress", [this](AsyncWebServerRequest *request) {
            if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
            html_fileuploadProgress(request);
    });



    ESPHTTPServer.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
        if (!ESPHTTPServer.handleFileRead("/update.html", request)) { request->send(404, "text/plain", "FileNotFound");	}
    });

    ESPHTTPServer.on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
            if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
            updateFileExecute (request);
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            html_uploadUpdateFile(request, filename, index, data, len, final);
    });

    ESPHTTPServer.on("/update/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

 }

 void CLASS_CORE_OTA::web_Init() {
    registerCommonRoutes();
    registerCustomRoutes();
 }

// ============================================================
// Веб-обработчики
// ============================================================

void CLASS_CORE_OTA::html_fileuploadProgress(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	String values = "";
    values += "percent|"    + (String)fileUpadedpercent + "|div\n";
    request->send(200, "text/plain", values);
}

void CLASS_CORE_OTA::html_md5_set(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	_browserFileMD5 = "";
	
	DEBUGOTA("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  {
		for (uint8_t i = 0; i < request->args(); i++) {
			DEBUGOTA("Arg %s: %s\r\n", request->argName(i).c_str(), request->arg(i).c_str());
			if (request->argName(i) == "md5") {
				_browserFileMD5 = urldecode(request->arg(i));
				Update.setMD5(_browserFileMD5.c_str());
				continue;
			}
			if (request->argName(i) == "size") {
				_updateFileSize = request->arg(i).toInt();
				DEBUGOTA("Update size: %d \r\n", _updateFileSize);
				continue;
			}
			if (request->argName(i) == "name") {
				_updateFileName = request->arg(i).c_str();
				DEBUGOTA("Update filename: %s \r\n", _updateFileName.c_str());
				continue;
			}
		}
		request->send(200, "text/html", "OK --> MD5: " + _browserFileMD5);
	}

}

void CLASS_CORE_OTA::html_filename_check(AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    String values = "";
    String updateOKstr = "";
    String updateFiletype = "";
    String updateFileMatcheD = "";
    String updateIsDebug = "";
    String fsVersionCompare = "";
    
    updateFiletype = OTA_STR_UNSUPPORTED;   
    updateFileMatcheD = OTA_STR_NAMEDIFF;   
    updateIsDebug = "0";
    fsVersionCompare = FS_VERSION_COMPARE_MISSING;

    if (_updateFileName.length() == 0 || !isValidFilename(_updateFileName)) {
        updateOKstr = "ERROR" ;
        values += "updStatus|" + updateOKstr + "|div\n";
        request->send(200, "text/plain", values);
        return;   
    }

    fileCompareResult result;
    fileNameCheck(_updateFileName, &result);
    
    // Determine file type
    if (result.fileType == FILE_TYPE_UNSUPPORTED)   { updateFiletype = OTA_STR_UNSUPPORTED; }
    if (result.fileType == FILE_TYPE_FIRMWARE)      { updateFiletype = OTA_STR_FIRMWARE; }
    if (result.fileType == FILE_TYPE_FILESYSTEM)    { updateFiletype = OTA_STR_FILESYSTEM; }
    
    // Name match check
    if (result.nameMatch == 1) { updateFileMatcheD = OTA_STR_NAMEMATCH; }
    
    // Debug flag
    updateIsDebug = String(result.isDebug);
    
    typeOTAfile = result.fileType;
    
    // NEW: Compare with current FS version (or firmware)
    compareWithCurrentFsVersion(&result, _updateFileName);
    
    // Format FS version comparison string for web
    if (result.fsVersionCompare == 0) {
        fsVersionCompare = FS_VERSION_COMPARE_SAME;
    } else if (result.fsVersionCompare == 1) {
        fsVersionCompare = FS_VERSION_COMPARE_NEWER;
    } else if (result.fsVersionCompare == -1) {
        fsVersionCompare = FS_VERSION_COMPARE_OLDER;
    } else if (result.fsVersionCompare == -2) {
        fsVersionCompare = FS_VERSION_COMPARE_MISSING;
    } else {
        fsVersionCompare = FS_VERSION_COMPARE_ERROR;
    }
    
    // Check free space (only for firmware)
    bool updateOK = true;
    if (typeOTAfile == FILE_TYPE_FIRMWARE) {
        updateOK = maxSketchSpace < freeSketchSpace;
    }
    if (updateOK == true) { 
        updateOKstr = "OK"; 
    } else { 
        updateOKstr = "ERROR"; 
    }

    DEBUGOTA("\t _updateFileName: %s\r\n", _updateFileName.c_str());
    DEBUGOTA("\t updStatus: %s\r\n", updateOKstr.c_str());
    DEBUGOTA("\t FreeSketchSpace: %d\r\n", freeSketchSpace);
    DEBUGOTA("\t MaxSketchSpace: %d\r\n", maxSketchSpace);
    DEBUGOTA("\t UpdateFiletype: %s\r\n", updateFiletype.c_str());
    DEBUGOTA("\t isDebug: %s\r\n", updateIsDebug.c_str());
    DEBUGOTA("\t FS Version Compare: %s\r\n", fsVersionCompare.c_str());
    DEBUGOTA("\t FS Current: %d.%d.%lld.%d\r\n", 
             result.fsCurrentMajor, result.fsCurrentMinor, 
             (long long)result.fsCurrentDate, result.fsCurrentBuild);
    DEBUGOTA("\t updVerDiffName: %s %d %d %lld %d\r\n",
             updateFileMatcheD.c_str(),
             result.majorDiff,
             result.minorDiff,
             (long long)result.dateDiff,
             result.buildDiff);

    // Build response
    values += "updStatus|"         + updateOKstr           + "|div\n";
    values += "updFileType|"       + updateFiletype        + "|div\n";
    values += "updSizeFree|"       + String(freeSketchSpace) + "|div\n";
    values += "updSizeMax|"        + String(maxSketchSpace)  + "|div\n";
    
    values += "updVerDiffName|"    + updateFileMatcheD     + "|div\n";
    values += "updVerDiffMaj|"     + String(result.majorDiff) + "|div\n";
    values += "updVerDiffMinor|"   + String(result.minorDiff) + "|div\n";
    values += "updVerDiffDate|"    + String(result.dateDiff)  + "|div\n";
    values += "updVerDiffBuild|"   + String(result.buildDiff) + "|div\n";
    values += "updIsDebug|"        + updateIsDebug          + "|div\n";
    
    // NEW: FS version compare fields
    values += "updFsCompare|"      + fsVersionCompare       + "|div\n";
    values += "updFsCurrentMajor|" + String(result.fsCurrentMajor) + "|div\n";
    values += "updFsCurrentMinor|" + String(result.fsCurrentMinor) + "|div\n";
    values += "updFsCurrentDate|"  + String(result.fsCurrentDate)  + "|div\n";
    values += "updFsCurrentBuild|" + String(result.fsCurrentBuild) + "|div\n";

    request->send(200, "text/plain", values);
}

void CLASS_CORE_OTA::updateFileExecute (AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	
	String message;
	bool needReboot = true;
	
	if (!Update.hasError()) {
#if defined(ESP32)
		if (typeOTAfile == FILE_TYPE_FILESYSTEM) {
			needReboot = true;
			message = "UPDATE_COMPLETE_REBOOT";
			DEBUGOTA("FS update on ESP32: reboot needed\n");
			core_sys.invalidateFsVersionCache();
		}
#endif
	}
	
	AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "UPDATE_COMPLETE_REBOOT");
	response->addHeader("Connection", "close");
	response->addHeader("Access-Control-Allow-Origin", "*");
	request->send(response);
	
	delay(100);
	
	if (needReboot && !Update.hasError()) {
		core_sys.invalidateFsVersionCache();
		ESPHTTPServer.restart_esp();
	}
}

void CLASS_CORE_OTA::html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    String values = "";
    static long totalSize = 0;
    static bool errorOccurred = false;
    static bool responseSent = false;  
    int updatePartition = 1;
    
    DEBUGLOAD("index=%u, len=%u, final=%d\r\n", index, len, final);
    
    if (index == 0) {
        DEBUGOTA("===== UPLOAD START =====\r\n");
        DEBUGOTA("File: %s\r\n", filename.c_str());
        
        errorOccurred = false;
        responseSent = false;  
        totalSize = 0;
        
        prepareSizesForUpdate();
        
        if (typeOTAfile == FILE_TYPE_UNSUPPORTED) {
            values = "OTA Update error UNSUPPORTED file!";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }
        
        if (!isValidFilename(filename)) {
            values = "Invalid filename";
            DEBUGOTA("%s: %s\n", values.c_str(), filename.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }

        if (typeOTAfile == FILE_TYPE_FIRMWARE) {
            if (_updateFileSize > freeSketchSpace) {
                values = "Firmware too large for available space!";
                DEBUGOTA("%s %u > %u\n", values.c_str(), _updateFileSize, freeSketchSpace);
                request->send(500, "text/plain", values);
                errorOccurred = true;
                return;
            }
        }

        DEBUGOTA("Update start: %s\r\n", filename.c_str());
        DEBUGOTA("Free sketch space: %u\r\n", freeSketchSpace);
        DEBUGOTA("New sketch size: %u\r\n", _updateFileSize);

        if (_browserFileMD5 == NULL || _browserFileMD5 == "") {
            values = "OTA Update error no MD5 hash!";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }
        
#if defined(ESP32)
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { updatePartition = U_SPIFFS; }
#elif defined(ESP8266)
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { updatePartition = U_FS; }
#endif
        if (typeOTAfile == FILE_TYPE_FIRMWARE) { updatePartition = U_FLASH; }
        
        DEBUGOTA("Update partition: %d\r\n", updatePartition);
        
        fsEnd();
        
#if defined(ESP8266)
        DEBUGOTA("Enabling async mode for ESP8266\n");
        Update.runAsync(true);
#endif
        
        if (Update.begin(_updateFileSize, updatePartition) == false) {
#ifdef DEBUG_OTA
            Update.printError(DEBUGOTASER);
#endif
            values = "OTA Update error at begin";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            ledMacrosUpdateError();
            
            fsRemount();
            return;
        }
        
        // Set MD5 AFTER Update.begin() - begin() resets the MD5 internally!
        // Setting it before was useless - it was always cleared by begin()
        Update.setMD5(_browserFileMD5.c_str());
        DEBUGOTA("Hash from browser: %s\r\n", _browserFileMD5.c_str());

        // Моргание обновления: прошивка или файловая система
        if (typeOTAfile == FILE_TYPE_FIRMWARE) { ledMacrosUpdateFirmware(); }
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { ledMacrosUpdateFilesystem(); }
    }
    
    if (errorOccurred)  { return; }
    if (responseSent)   { return; }
    
    totalSize += len;
    
    uint16_t percentLoaded = (totalSize * 100) / _updateFileSize;
    fileUpadedpercent = percentLoaded;
    if ((percentLoaded % 5) == 0 && (percentLoaded != percentLoadedPrev)) {
        percentLoadedPrev = percentLoaded;
        DEBUGOTA("Uploaded: %ld bytes %u %%\r\n", totalSize, percentLoaded);
    }

    size_t written = Update.write(data, len);
    if (written != len) {
        values = "OTA Update error data load!";
        DEBUGOTA("%s len=%d written=%d total=%ld\n", values.c_str(), len, written, totalSize);
        request->send(500, "text/plain", values);
        errorOccurred = true;
        responseSent = true;  
        ledMacrosUpdateError();
        
#if defined(ESP32)
        Update.abort();
#elif defined(ESP8266)
        Update.end();
#endif
        
        fsRemount();
        return;
    }
    
    if (final) {
        if (errorOccurred) {
            return;
        }
        
        String updateHash;
        DEBUGOTA("Applying update...\n");
        if (Update.end(true)) {
            updateHash = Update.md5String();
            DEBUGOTA("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
            
#if defined(ESP32)
            if (typeOTAfile == FILE_TYPE_FILESYSTEM) {
                DEBUGOTA("FS update on ESP32: will reboot\n");
                core_sys.invalidateFsVersionCache();
            }
#endif
            // FIX: use _updateFileSize instead of request->contentLength()
            // request->contentLength() includes HTTP overhead, not just the file size
            DEBUGOTA("Update Success: %u\nRebooting...\r\n", _updateFileSize);
            // Do NOT send response here - updateFileExecute() will handle it
            // request->send() removed to prevent double-response with updateFileExecute()
        } else {
            updateHash = Update.md5String();
            DEBUGOTA("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
#ifdef DEBUG_OTA
            Update.printError(DEBUGOTASER);
#endif
            ledMacrosUpdateError();
            fsRemount();
        }
    }
}

// ============================================================
// Версионные методы
// ============================================================

String CLASS_CORE_OTA::getVersionStr(){
    return String(CORE_OTA_VERSION);
}

String CLASS_CORE_OTA::getGeneratedTime(){
    return String(CORE_OTA_GENERATED_TIME);
}

String CLASS_CORE_OTA::getCommitDateStr(){
    return String(CORE_OTA_COMMIT_DATE_STR);
}

void CLASS_CORE_OTA::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaversion|"     + getVersionStr()    + "|div\n";
    values += "otagentime|"     + getGeneratedTime() + "|div\n";
    values += "otagendate|"     + getCommitDateStr() + "|div\n";
    
    // Current firmware version (from version.h macros)
    values += "fwVersion|"      + String(FIRMWARE_VERSION) + "|div\n";
    
    // Current filesystem version (единый источник — core_sys)
    values += "fsVersion|"      + core_sys.getFsVersionStr() + "|div\n";
    
    request->send(200, "text/plain", values);
}

// ============================================================
// Конкретная логика модуля
// ============================================================

// ============================================================
// Светодиодная индикация обновления
// ============================================================

void ledMacrosUpdateFirmware()		{	ledSetState(LED_PRIO_OTA, patOtaFw, -1); }
void ledMacrosUpdateFilesystem()	{	ledSetState(LED_PRIO_OTA, patOtaFs, -1); }
void ledMacrosUpdateError()			{	ledSetState(LED_PRIO_OTA, patOtaErr, 2); }

void CLASS_CORE_OTA::prepareSizesForUpdate (){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	freeSketchSpace  = ESP.getFreeSketchSpace();
}

bool  CLASS_CORE_OTA::ConfigureOTA( String _hostname, String _password) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	
	if (_hostname != "") {
	ArduinoOTA.setHostname(_hostname.c_str());
	DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else { return false;	}

	if (_password != "") {
		ArduinoOTA.setPassword(_password.c_str());
		DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else { return false;	}	


	ArduinoOTA.onStart([]() {
		ledMacrosUpdateFirmware();	// ArduinoOTA обновляет только прошивку
		DEBUGOTA("\r\n ArduinoOTA start. \r\n");
	});

#if defined(ESP32)
	ArduinoOTA.onEnd(std::bind([](fs::LittleFSFS* fs)
#elif defined(ESP8266)
	ArduinoOTA.onEnd(std::bind([](FS* fs)
#endif
	{
		fs->end();
		DEBUGOTA("\r\n ArduinoOTA end. \r\n");
	}, _fs));
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		DEBUGOTA("\t OTA update progress: %u%% \r\n", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		ledMacrosUpdateError();
		DEBUGOTA("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) 			{DEBUGOTA("Auth Failed\r\n");		}
		else if (error == OTA_BEGIN_ERROR) 		{DEBUGOTA("Begin Failed\r\n");		}
		else if (error == OTA_CONNECT_ERROR)	{DEBUGOTA("Connect Failed\r\n");	}
		else if (error == OTA_RECEIVE_ERROR) 	{DEBUGOTA("Receive Failed\r\n");	}
		else if (error == OTA_END_ERROR) 		{DEBUGOTA("End Failed\r\n");		}
	});
	DEBUGOTA("\r\n ArduinoOTA Ready \r\n");
	ArduinoOTA.begin();

	return true;
}

void CLASS_CORE_OTA::fsEnd() {
    if (_fs) {
        DEBUGOTA("Ending filesystem...\n");
        _fs->end();
        _ota_fsEndCalled = true;
        delay(100);
    }
}

void CLASS_CORE_OTA::fsRemount() {
    if (_fs) {
        DEBUGOTA("Remounting filesystem...\n");
#if defined(ESP32)
        _fs->begin(true);
#elif defined(ESP8266)
        _fs->begin();
#endif
    }
}

int8_t CLASS_CORE_OTA::compareVersionDiffs(int32_t majorDiff, int32_t minorDiff, int64_t dateDiff, int32_t buildDiff) {
    if (majorDiff > 0) return 1;
    if (majorDiff < 0) return -1;
    if (minorDiff > 0) return 1;
    if (minorDiff < 0) return -1;
    if (dateDiff > 0) return 1;
    if (dateDiff < 0) return -1;
    if (buildDiff > 0) return 1;
    if (buildDiff < 0) return -1;
    return 0;
}

 void CLASS_CORE_OTA::loop(){
	 ArduinoOTA.handle();
 }

// ============================================================
// Compare file version with current FS JSON or firmware
// (FS-версия читается централизованно в core_sys)
// ============================================================

int8_t CLASS_CORE_OTA::compareWithCurrentFsVersion(fileCompareResult* result, const String& filename) {
    if (result->fileType == FILE_TYPE_FIRMWARE) {
        result->fsCurrentMajor = VERSION_MAJOR;
        result->fsCurrentMinor = VERSION_MINOR;
        result->fsCurrentDate = VERSION_DATE;
        result->fsCurrentBuild = VERSION_BUILD;
    } else {
        int64_t fsDate = 0;
        int32_t fsBuild = 0, fsMajor = 0, fsMinor = 0;
        if (!core_sys.getFsVersion(fsDate, fsBuild, fsMajor, fsMinor)) {
            result->fsVersionCompare = -2;
            DEBUGOTA("compareWithCurrentFsVersion: FS version data invalid\n");
            return result->fsVersionCompare;
        }
        result->fsCurrentMajor = fsMajor;
        result->fsCurrentMinor = fsMinor;
        result->fsCurrentDate = fsDate;
        result->fsCurrentBuild = fsBuild;
    }
    
    result->fsVersionCompare = compareVersionDiffs(result->majorDiff, result->minorDiff, result->dateDiff, result->buildDiff);
    
    DEBUGOTA("compareWithCurrentFsVersion (%s): current %d.%d.%lld.%d, diff %d\n",
             (result->fileType == FILE_TYPE_FIRMWARE) ? "Firmware" : "FS",
             result->fsCurrentMajor, result->fsCurrentMinor,
             (long long)result->fsCurrentDate, result->fsCurrentBuild,
             result->fsVersionCompare);
    
    return result->fsVersionCompare;
}

int8_t CLASS_CORE_OTA::fileNameCheck(String filename, fileCompareResult* result) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    int8_t _ret = -1;
    
    result->nameMatch = -1;
    result->majorDiff = 0;
    result->minorDiff = 0;
    result->dateDiff = 0;
    result->buildDiff = 0;
    result->isDebug = 0;
    result->fileType = FILE_TYPE_UNSUPPORTED;
    result->fsVersionCompare = 0;
    result->fsCurrentDate = 0;
    result->fsCurrentBuild = 0;
    result->fsCurrentMajor = 0;
    result->fsCurrentMinor = 0;
    
    if (filename.length() == 0) { 
        return _ret;  
    }

    String cleanFilename = "";
    for (size_t i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        if (c >= 32 && c <= 126) {
            cleanFilename += c;
        }
    }
    filename = cleanFilename;

    if (filename.endsWith(".bin")) {
        if (filename.indexOf(OTA_STR_SEPARATOR_FILESYSTEM) > 0) {
            result->fileType = FILE_TYPE_FILESYSTEM;
        } else if (filename.indexOf(OTA_STR_SEPARATOR_FIRMWARE) > 0) {
            result->fileType = FILE_TYPE_FIRMWARE;
        }
    }
    
    if (filename.startsWith(BUILD_ENV) == false) {
        DEBUGOTA("\t Wrong device: expected %s, got %s\r\n", BUILD_ENV, filename.substring(0, strlen(BUILD_ENV)).c_str());
        return _ret;
    }
    
    String separator;
    if (result->fileType == FILE_TYPE_FILESYSTEM) {
        separator = String(BUILD_ENV) + OTA_STR_SEPARATOR_FILESYSTEM;
    } else if (result->fileType == FILE_TYPE_FIRMWARE) {
        separator = String(BUILD_ENV) + OTA_STR_SEPARATOR_FIRMWARE;
    } else {
        DEBUGOTA("\t Unknown file type\r\n");
        return _ret;
    }
    
    if (!filename.startsWith(separator)) {
        DEBUGOTA("\t Wrong separator\r\n");
        return _ret;
    }
    
    result->nameMatch = 1;
    
    String versionPart = filename.substring(separator.length());
    
    int binPos = versionPart.lastIndexOf(".bin");
    if (binPos <= 0) {
        DEBUGOTA("\t No .bin extension\r\n");
        return _ret;
    }
    
    String versionStr = versionPart.substring(0, binPos);
    
    int dotCount = 0;
    for (size_t i = 0; i < versionStr.length(); i++) {
        if (versionStr.charAt(i) == '.') dotCount++;
    }
    
    result->isDebug = (dotCount >= 3) ? 1 : 0;
    
    DEBUGOTA("\t Version string: %s, dots=%d, isDebug=%d\r\n", 
             versionStr.c_str(), dotCount, result->isDebug);
    
    int firstDot = versionStr.indexOf('.');
    int secondDot = versionStr.indexOf('.', firstDot + 1);
    int thirdDot = versionStr.indexOf('.', secondDot + 1);
    
    if (firstDot < 0 || secondDot < 0) {
        DEBUGOTA("\t Invalid version format (need at least 3 parts)\r\n");
        return _ret;
    }
    
    String majorStr = versionStr.substring(0, firstDot);
    String minorStr = versionStr.substring(firstDot + 1, secondDot);
    
    int32_t fileMajor = majorStr.toInt();
    int32_t fileMinor = minorStr.toInt();
    int64_t fileDate = 0;
    int32_t fileBuild = 0;
    
    if (thirdDot > 0) {
        String dateStr = versionStr.substring(secondDot + 1, thirdDot);
        String buildStr = versionStr.substring(thirdDot + 1);
        
        dateStr.replace("_", "");
        fileDate = atoll(dateStr.c_str());
        fileBuild = buildStr.toInt();
    } else {
        String lastPart = versionStr.substring(secondDot + 1);
        lastPart.replace("_", "");
        fileDate = atoll(lastPart.c_str());
        fileBuild = 0;
    }

    DEBUGOTA("\t Parsed: major=%d, minor=%d, date=%lld, build=%d\r\n", 
             fileMajor, fileMinor, fileDate, fileBuild);
    
    int32_t currentMajor, currentMinor;
    int64_t currentDate;
    int32_t currentBuild;
    
    if (result->fileType == FILE_TYPE_FILESYSTEM) {
        if (!core_sys.getFsVersion(currentDate, currentBuild, currentMajor, currentMinor)) {
            DEBUGOTA("\t FS version data invalid, update blocked\r\n");
            return -1;
        }
        DEBUGOTA("\t Current (FS): major=%d, minor=%d, date=%lld, build=%d\r\n", 
                 currentMajor, currentMinor, currentDate, currentBuild);
    } else {
        currentMajor = VERSION_MAJOR;
        currentMinor = VERSION_MINOR;
        currentDate = VERSION_DATE;
        currentBuild = VERSION_BUILD;
        DEBUGOTA("\t Current (FW): major=%d, minor=%d, date=%lld, build=%d\r\n", 
                 currentMajor, currentMinor, currentDate, currentBuild);
    }
    
    result->majorDiff = fileMajor - currentMajor;
    result->minorDiff = fileMinor - currentMinor;
    result->dateDiff = fileDate - currentDate;
    result->buildDiff = (result->isDebug) ? (fileBuild - currentBuild) : 0;
    
    DEBUGOTA("\t Diffs: major=%d, minor=%d, date=%lld, build=%d\r\n", 
             result->majorDiff, result->minorDiff, result->dateDiff, result->buildDiff);
    
    // Если major или minor строго больше — всегда разрешаем обновление
    bool canUpdate = (result->majorDiff > 0) || (result->minorDiff > 0);
    // Если major и minor совпадают — проверяем build/date
    if (!canUpdate && result->majorDiff >= 0 && result->minorDiff >= 0) {
        if (result->isDebug) {
            canUpdate = (result->buildDiff > 0);
        } else {
            canUpdate = (result->dateDiff > 0);
        }
    }
    
    if (canUpdate) {
        _ret = 1;
        DEBUGOTA("\t File is valid for update\r\n");
    } else {
        DEBUGOTA("\t File is NOT valid for update (older version)\r\n");
    }
    
    return _ret;
}

bool CLASS_CORE_OTA::isValidFilename(const String& filename) {
    if (filename.length() == 0 || filename.length() > 100) return false;
    
    for (size_t i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        if (!((c >= 'a' && c <= 'z') || 
              (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || 
              c == '.' || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}
