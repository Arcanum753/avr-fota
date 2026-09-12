#if defined(ESP32)
#include <LittleFS.h>
#elif defined(ESP8266)
#include <LittleFS.h>
#endif

#include"version.h"
#include <Update.h>
#include "core_web/FSWebServerLib.h"
#include "common/common.h"
#include "core_ota.h"
#include "core_ota_version.h"
#include "core_sys/core_sys.h"
#include "core_state/core_state.h"

CLASS_CORE_OTA core_ota;

static const char* const otaStateNames[5] = {
    "idle", "upload", "verify", "done", "error",
};

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

	core_state.signal("ota.state", BusValue::en(OTA_ST_IDLE));
	core_state.signal("ota.server_reachable", BusValue::bo(true));
 }

void CLASS_CORE_OTA::begin(ModContext& ctx){
	_fs = ctx.fs;
	begin(ctx.hostname, ctx.password);
}

// ============================================================
// register_resources()
// ============================================================
void CLASS_CORE_OTA::register_resources() {
	DEBUGOTA("%s\r\n", __FUNCTION__);

	core_state.regEnum("state", 5, otaStateNames, "OTA state");
	core_state.regState("server_reachable", BusValue::BOOL,
	                    "OTA server reachable", false);
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

        core_state.signal("ota.state", BusValue::en(OTA_ST_UPLOAD));
    }
    
    if (errorOccurred)  {
        core_state.signal("ota.state", BusValue::en(OTA_ST_ERROR));
        return;
    }
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
            core_state.signal("ota.state", BusValue::en(OTA_ST_DONE));
        } else {
            updateHash = Update.md5String();
            DEBUGOTA("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
#ifdef DEBUG_OTA
            Update.printError(DEBUGOTASER);
#endif
            ledMacrosUpdateError();
            core_state.signal("ota.state", BusValue::en(OTA_ST_ERROR));
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
