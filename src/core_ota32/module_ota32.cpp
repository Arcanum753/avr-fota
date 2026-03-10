#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#endif

#include "FSWebServerLib.h"
#include "version.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#include "module_ota32.h"
#include "common.h"

MODULE_OTA_CLASS modOtaClass(false);

MODULE_OTA_CLASS :: MODULE_OTA_CLASS (bool _in) { dumb = _in; }

#if defined(ESP32)
void MODULE_OTA_CLASS::setFs(fs::SPIFFSFS* fs)
#endif
{ _fs = fs; }

void MODULE_OTA_CLASS::begin(String _hostname, String _password){
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    prepareSizesForUpdate();
    ConfigureOTA(_hostname, _password);
}

void MODULE_OTA_CLASS::prepareSizesForUpdate (){
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
    freeSketchSpace  = ESP.getFreeSketchSpace();
}

bool MODULE_OTA_CLASS::ConfigureOTA(String _hostname, String _password) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    
    if (_hostname != "") {
        ArduinoOTA.setHostname(_hostname.c_str());
        DEBUGOTA("OTA password set %s\n", _password.c_str());
    } else { return false; }

    if (_password != "") {
        ArduinoOTA.setPassword(_password.c_str());
        DEBUGOTA("OTA password set %s\n", _password.c_str());
    } else { return false; }

    ArduinoOTA.onStart([]() {
        DEBUGOTA("\r\n ArduinoOTA start. \r\n");
    });

    ArduinoOTA.onEnd(std::bind([](fs::SPIFFSFS* fs) {
        fs->end();
        DEBUGOTA("\r\n ArduinoOTA end. \r\n");
    }, _fs));
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        DEBUGOTA("\t OTA update progress: %u%% \r\n", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        DEBUGOTA("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)           { DEBUGOTA("Auth Failed\r\n"); }
        else if (error == OTA_BEGIN_ERROR)      { DEBUGOTA("Begin Failed\r\n"); }
        else if (error == OTA_CONNECT_ERROR)    { DEBUGOTA("Connect Failed\r\n"); }
        else if (error == OTA_RECEIVE_ERROR)    { DEBUGOTA("Receive Failed\r\n"); }
        else if (error == OTA_END_ERROR)        { DEBUGOTA("End Failed\r\n"); }
    });
    DEBUGOTA("\r\n ArduinoOTA Ready \r\n");
    ArduinoOTA.begin();
    return true;
}

void MODULE_OTA_CLASS::loopHandler(){
    ArduinoOTA.handle();
}

void MODULE_OTA_CLASS::webInit() {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    
    ESPHTTPServer.on("/update/setmd5", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        html_md5_set(request);
    });

    ESPHTTPServer.on("/update/firmwarefilecheck", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        html_filename_check(request);
    });

    ESPHTTPServer.on("/update/progress", [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        html_fileuploadProgress(request);
    });

    ESPHTTPServer.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        if (!ESPHTTPServer.handleFileRead("/update.html", request)) { 
            request->send(404, "text/plain", "FileNotFound");
        }
    });

    ESPHTTPServer.on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
        updateFileExecute(request);
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        html_uploadUpdateFile(request, filename, index, data, len, final);
    });
}
void MODULE_OTA_CLASS::html_fileuploadProgress(AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    String values = "";
    values += "percent|" + (String)fileUpadedpercent + "|div\n";
    request->send(200, "text/plain", values);
}
void MODULE_OTA_CLASS::html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    String values = "";
    static long totalSize = 0;
    static bool errorOccurred = false;
    static bool responseSent = false;  
    int updatePartition = 1;
    
    DEBUGLOAD(" index= %u, len=%u, final=%d\n", index, len, final);
    
    if (index == 0) {
        errorOccurred = false;
        responseSent = false;  
        totalSize = 0;

        if (typeOTAfile == FILE_TYPE_UNSUPPORTED) {
            values = "OTA Update error UNSUPPORTED file!";
            DEBUGLOAD("%s\n", values.c_str());
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

        DEBUGLOAD("Update start: %s\r\n", filename.c_str());
        DEBUGLOAD("File size: %u\r\n", _updateFileSize);

        if (_browserFileMD5 != NULL && _browserFileMD5 != "") {
            Update.setMD5(_browserFileMD5.c_str());
            DEBUGLOAD("Hash from browser: %s\r\n", _browserFileMD5.c_str());
        } else {
            values = "OTA Update error no MD5 hash!";
            DEBUGLOAD("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }
        
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { 
            updatePartition = U_SPIFFS; 
        } else { 
            updatePartition = U_FLASH; 
        }

        if (_fs) { 
            _fs->end(); 
            delay(100);
        }

        DEBUGLOAD("Partition: %d, size: %u\n", updatePartition, _updateFileSize);
        
        if (Update.begin(_updateFileSize, updatePartition) == false) {
#ifdef DEBUGLOAD
            Update.printError(DEBUGOTASER);
#endif
            values = "OTA Update error at begin";
            DEBUGLOAD("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            
            if (_fs) {
                _fs->begin();
            }
            return;
        }
        
        DEBUGOTA("Update.begin() successful\n");
    }
    
    if (errorOccurred) { 
        return; 
    }
    if (responseSent) { 
        return; 
    }
    
    totalSize += len;
    
    uint16_t percentLoaded = (totalSize * 100) / _updateFileSize;
    fileUpadedpercent = percentLoaded;
    if ((percentLoaded % 5) == 0 && (percentLoaded != percentLoadedPrev)) {
        percentLoadedPrev = percentLoaded;
        DEBUGLOAD("Uploaded: %ld bytes %u %%\r\n", totalSize, percentLoaded);
    }

    size_t written = Update.write(data, len);
    if (written != len) {
        values = "OTA Update error data load!";
        DEBUGLOAD("%s len=%d written=%d total=%ld\n", values.c_str(), len, written, totalSize);
        request->send(500, "text/plain", values);
        errorOccurred = true;
        responseSent = true;  
        Update.abort();
        if (_fs) {
            _fs->begin();
        }
        return;
    }
    
    if (final) {
        if (errorOccurred) {
            return;
        }
        
        String updateHash;
        DEBUGLOAD("Applying update...");
        if (Update.end(true)) {
            updateHash = Update.md5String();
            DEBUGLOAD("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
            DEBUGLOAD("Update Success: %u\nRebooting...\r\n", request->contentLength());

            values = "Update successful! Device will restart in 3 seconds..."; 
            request->send(200, "text/plain", values);  
            responseSent = true;  
            delay(100); 
        } else {
            updateHash = Update.md5String();
            DEBUGLOAD("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
#ifdef DEBUGLOAD
            Update.printError(DEBUGOTASER);
#endif
            if (_fs) {
                _fs->begin();
            }
        }
    }
}

void MODULE_OTA_CLASS::html_md5_set(AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    _browserFileMD5 = "";
    
    DEBUGOTA("Arg number: %d\r\n", request->args());
    if (request->args() > 0) {
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

void MODULE_OTA_CLASS::html_filename_check(AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    String values = "";
    String updateOKstr = "";
    String updateFiletype = "";
    String updateFileMatcheD = "";
    
    updateFiletype = OTA_STR_UNSUPPORTED;   
    updateFileMatcheD = OTA_STR_NAMEDIFF;   

    if (_updateFileName.length() == 0 || !isValidFilename(_updateFileName)) {
        updateOKstr = "ERROR" ;
        values += "updStatus|" + updateOKstr + "|div\n";
        request->send(200, "text/plain", values);
        return;   
    }

    fileCompareResult result;
    fileNameCheck(_updateFileName, &result);
    
    if (result.fileType == FILE_TYPE_UNSUPPORTED)   { updateFiletype = OTA_STR_UNSUPPORTED; }
    if (result.fileType == FILE_TYPE_FIRMWARE)      { updateFiletype = OTA_STR_FIRMWARE; }
    if (result.fileType == FILE_TYPE_FILESYSTEM)    { updateFiletype = OTA_STR_FILESYSTEM; }
    if (result.nameMatch == 1) { updateFileMatcheD = OTA_STR_NAMEMATCH; }
    
    typeOTAfile = result.fileType;
    
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
    DEBUGOTA("\t updStatus: %s\r\n", updateOKstr);
    DEBUGOTA("\t FreeSketchSpace: %d\r\n", freeSketchSpace);
    DEBUGOTA("\t MaxSketchSpace: %d\r\n", maxSketchSpace);
    DEBUGOTA("\t UpdateFiletype: %s\r\n", updateFiletype.c_str());
    DEBUGOTA("\t updVerDiffName: %s %d %d %d %d\r\n",
             updateFileMatcheD.c_str(),
             result.majorDiff,
             result.minorDiff,
             result.dateDiff,
             result.buildDiff);

    values += "updStatus|"         + updateOKstr           + "|div\n";
    values += "updFileType|"       + updateFiletype        + "|div\n";
    values += "updSizeFree|"       + String(freeSketchSpace) + "|div\n";
    values += "updSizeMax|"        + String(maxSketchSpace)  + "|div\n";
    values += "updVerDiffName|"    + updateFileMatcheD     + "|div\n";
    values += "updVerDiffMaj|"     + String(result.majorDiff) + "|div\n";
    values += "updVerDiffMinor|"   + String(result.minorDiff) + "|div\n";
    values += "updVerDiffDate|"    + String(result.dateDiff)  + "|div\n";
    values += "updVerDiffBuild|"   + String(result.buildDiff) + "|div\n";

    request->send(200, "text/plain", values);
}

void MODULE_OTA_CLASS::updateFileExecute (AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    AsyncWebServerResponse *response = request->beginResponse(200, "text/html", 
        (Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting..."
    );
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    if (this->_fs) { this->_fs->end(); }
    ESPHTTPServer.restart_esp();
}

int8_t MODULE_OTA_CLASS::fileNameCheck(String filename, fileCompareResult* result) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    int8_t _ret = -1;
    
    result->nameMatch = -1;
    result->majorDiff = 0;
    result->minorDiff = 0;
    result->dateDiff = 0;
    result->buildDiff = 0;
    result->isDebug = 0;
    result->fileType = FILE_TYPE_UNSUPPORTED;
    
    if (filename.length() == 0) { 
        return _ret;  
    }

    String cleanFilename = "";
    for (int i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        if (c >= 32 && c <= 126) {
            cleanFilename += c;
        }
    }
    filename = cleanFilename;

    if (filename.endsWith(".bin")) {
        if (filename.indexOf("_fs-") > 0) {
            result->fileType = FILE_TYPE_FILESYSTEM;
        } else if (filename.indexOf("-") > 0) {
            result->fileType = FILE_TYPE_FIRMWARE;
        }
    }
    
    if (filename.startsWith(BUILD_ENV) == false) {
        DEBUGOTA("\t Wrong device: expected %s, got %s\r\n", BUILD_ENV, filename.substring(0, strlen(BUILD_ENV)).c_str());
        return _ret;
    }
    
    String separator;
    if (result->fileType == FILE_TYPE_FILESYSTEM) {    
        separator = String(BUILD_ENV) + "_fs-"; 
    } else if (result->fileType == FILE_TYPE_FIRMWARE) {    
        separator = String(BUILD_ENV) + "-"; 
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
    for (int i = 0; i < versionStr.length(); i++) {
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
    String dateStr = versionStr.substring(secondDot + 1, (thirdDot > 0) ? thirdDot : versionStr.length());
    String buildStr = (thirdDot > 0) ? versionStr.substring(thirdDot + 1) : "";

    DEBUGOTA("\t Parsed: major=%s, minor=%s, date=%s, build=%s\r\n", 
             majorStr.c_str(), minorStr.c_str(), dateStr.c_str(), buildStr.c_str());
    
    int32_t fileMajor = majorStr.toInt();
    int32_t fileMinor = minorStr.toInt();
    int32_t fileDate  = dateStr.toInt();
    int32_t fileBuild = buildStr.toInt();
    
    int32_t currentMajor = VERSION_MAJOR;
    int32_t currentMinor = VERSION_MINOR;
    int32_t currentBuild = VERSION_BUILD;
    
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    char currentDateStr[13];
    sprintf(currentDateStr, "%04d%02d%02d%02d%02d", 
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min);
    int32_t currentDate = atol(currentDateStr);
    
    DEBUGOTA("\t Current: major=%d, minor=%d, date=%d, build=%d\r\n", 
             currentMajor, currentMinor, currentDate, currentBuild);
    
    result->majorDiff = fileMajor - currentMajor;
    result->minorDiff = fileMinor - currentMinor;
    result->dateDiff = fileDate - currentDate;
    result->buildDiff = (result->isDebug) ? (fileBuild - currentBuild) : 0;
    
    DEBUGOTA("\t Diffs: major=%d, minor=%d, date=%d, build=%d\r\n", 
             result->majorDiff, result->minorDiff, result->dateDiff, result->buildDiff);
    
    bool canUpdate = (result->majorDiff >= 0) && (result->minorDiff >= 0);
    
    if (canUpdate) {
        _ret = 1;
        DEBUGOTA("\t File is valid for update\r\n");
    } else {
        DEBUGOTA("\t File is NOT valid for update (older major/minor)\r\n");
    }
    
    return _ret;
}

bool MODULE_OTA_CLASS::isValidFilename(const String& filename) {
    if (filename.length() == 0 || filename.length() > 100) return false;
    
    for (int i = 0; i < filename.length(); i++) {
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