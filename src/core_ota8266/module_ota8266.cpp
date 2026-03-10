#include <FS.h>
#include <Updater.h>
#include "FSWebServerLib.h"
#include <ArduinoOTA.h>
#include <Arduino.h>
#include "version.h"
#include "module_ota8266.h"
#include "common.h"
// Подключаем глобальный сервер из вашей библиотеки

#ifndef U_FLASH
#define U_FLASH 0
#endif

#ifndef U_FS
#define U_FS 100
#endif

MODULE_OTA8266_CLASS modOta8266;

MODULE_OTA8266_CLASS::MODULE_OTA8266_CLASS() {
    fileUpadedpercent = 0;
    percentLoadedPrev = 0;
    uploadError = false;
    responseSent = false;
    totalFileSize = 0;
    updateStarted = false;
    maxSketchSpace = 0;
    freeSketchSpace = 0;
    fileSize = 0;
    typeOTAfile = FILE_TYPE_UNSUPPORTED;
}

void MODULE_OTA8266_CLASS::begin(String hostname, String password) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    prepareSizesForUpdate();
    ConfigureOTA(hostname, password);
}

void MODULE_OTA8266_CLASS::prepareSizesForUpdate() {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    maxSketchSpace = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
    freeSketchSpace = ESP.getFreeSketchSpace();
}

bool MODULE_OTA8266_CLASS::ConfigureOTA(String hostname, String password) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    
    if (hostname != "") {
        ArduinoOTA.setHostname(hostname.c_str());
        DEBUGOTA("OTA hostname set %s\n", hostname.c_str());
    }

    if (password != "") {
        ArduinoOTA.setPassword(password.c_str());
        DEBUGOTA("OTA password set %s\n", password.c_str());
    }

    ArduinoOTA.onStart([]() {
        DEBUGOTA("\r\n ArduinoOTA start. \r\n");
    });

    ArduinoOTA.onEnd([]() {
        DEBUGOTA("\r\n ArduinoOTA end. \r\n");
    });
    
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
    
    ArduinoOTA.begin();
    DEBUGOTA("\r\n ArduinoOTA Ready \r\n");
    return true;
}

void MODULE_OTA8266_CLASS::loopHandler() {
    ArduinoOTA.handle();
}

void MODULE_OTA8266_CLASS::webInit() {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    
    // Используем глобальный ESPHTTPServer для регистрации эндпоинтов
    // ТОЧНО КАК В ESP32 ВЕРСИИ!
    
    ESPHTTPServer.on("/update/setmd5", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { 
            return request->requestAuthentication(); 
        }
        html_md5_set(request);
    });

    ESPHTTPServer.on("/update/firmwarefilecheck", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { 
            return request->requestAuthentication(); 
        }
        html_filename_check(request);
    });

    ESPHTTPServer.on("/update/progress", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { 
            return request->requestAuthentication(); 
        }
        html_fileuploadProgress(request);
    });

    ESPHTTPServer.on("/update", HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { 
            return request->requestAuthentication(); 
        }
        if (!ESPHTTPServer.handleFileRead("/update.html", request)) { 
            request->send(404, "text/plain", "FileNotFound");
        }
    });

    ESPHTTPServer.on("/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!ESPHTTPServer.checkAuth(request)) { 
            return request->requestAuthentication(); 
        }
        updateFileExecute(request);
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        html_uploadUpdateFile(request, filename, index, data, len, final);
    });
    
    DEBUGOTA("OTA handlers registered with ESPHTTPServer\r\n");
}

void MODULE_OTA8266_CLASS::html_md5_set(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    _browserFileMD5 = "";
    fileSize = 0;
    _updateFileName = "";
    
    DEBUGOTA("Arg number: %d\r\n", request->args());
    
    for (uint8_t i = 0; i < request->args(); i++) {
        String argName = request->argName(i);
        String argValue = request->arg(i);
        
        DEBUGOTA("Arg %s: %s\r\n", argName.c_str(), argValue.c_str());
        
        if (argName == "md5") {
            _browserFileMD5 = urldecode(argValue);
            continue;
        }
        if (argName == "size") {
            fileSize = argValue.toInt();
            DEBUGOTA("Update size: %d\r\n", fileSize);
            continue;
        }
        if (argName == "name") {
            _updateFileName = argValue;
            DEBUGOTA("Update filename: %s\r\n", _updateFileName.c_str());
            continue;
        }
    }
    
    request->send(200, "text/html", "OK --> MD5: " + _browserFileMD5);
}

void MODULE_OTA8266_CLASS::html_filename_check(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    String values = "";
    String updateOKstr = "";
    String updateFiletype = "";
    String updateFileMatched = "";
    
    updateFiletype = OTA_STR_UNSUPPORTED;   
    updateFileMatched = OTA_STR_NAMEDIFF;   

    if (_updateFileName.length() == 0 || !isValidFilename(_updateFileName)) {
        updateOKstr = "ERROR";
        values += "updStatus|" + updateOKstr + "|div\n";
        request->send(200, "text/plain", values);
        return;   
    }

    fileCompareResult result;
    fileNameCheck(_updateFileName, &result);
    
    if (result.fileType == FILE_TYPE_UNSUPPORTED)   { updateFiletype = OTA_STR_UNSUPPORTED; }
    if (result.fileType == FILE_TYPE_FIRMWARE)      { updateFiletype = OTA_STR_FIRMWARE; }
    if (result.fileType == FILE_TYPE_FILESYSTEM)    { updateFiletype = OTA_STR_FILESYSTEM; }
    if (result.nameMatch == 1) { updateFileMatched = OTA_STR_NAMEMATCH; }
    
    typeOTAfile = result.fileType;
    
    prepareSizesForUpdate();
    
    bool updateOK = true;
    if (typeOTAfile == FILE_TYPE_FIRMWARE || typeOTAfile == FILE_TYPE_FILESYSTEM) {
        updateOK = (fileSize <= freeSketchSpace);
    }
    updateOKstr = updateOK ? "OK" : "ERROR";

    DEBUGOTA("\t _updateFileName: %s\r\n", _updateFileName.c_str());
    DEBUGOTA("\t updStatus: %s\r\n", updateOKstr.c_str());
    DEBUGOTA("\t FreeSketchSpace: %d\r\n", freeSketchSpace);
    DEBUGOTA("\t MaxSketchSpace: %d\r\n", maxSketchSpace);
    DEBUGOTA("\t UpdateFiletype: %s\r\n", updateFiletype.c_str());

    values += "updStatus|"         + updateOKstr           + "|div\n";
    values += "updFileType|"       + updateFiletype        + "|div\n";
    values += "updSizeFree|"       + String(freeSketchSpace) + "|div\n";
    values += "updSizeMax|"        + String(maxSketchSpace)  + "|div\n";
    values += "updVerDiffName|"    + updateFileMatched     + "|div\n";
    values += "updVerDiffMaj|"     + String(result.majorDiff) + "|div\n";
    values += "updVerDiffCore|"    + String(result.minorDiff) + "|div\n";
    values += "updVerDiffMod|0|div\n";
    values += "updVerDiffBuild|"   + String(result.buildDiff) + "|div\n";

    request->send(200, "text/plain", values);
}

void MODULE_OTA8266_CLASS::html_fileuploadProgress(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    String values = "";
    values += "percent|" + String(fileUpadedpercent) + "|div\n";
    request->send(200, "text/plain", values);
}


void MODULE_OTA8266_CLASS::updateFileExecute(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
    
    String response;
    if (uploadError) {
        response = "FAIL";
        DEBUGOTA("Update failed\r\n");
        request->send(500, "text/html", response);
        return;
    }
    
    if (updateStarted) {
        if (Update.end(true)) {
            String updateHash = Update.md5String();
            DEBUGOTA("Update SUCCESS! MD5: %s\r\n", updateHash.c_str());
            
            response = "<META http-equiv=\"refresh\" content=\"15;URL=/update\">"
                       "Update correct. Restarting...";
            
            request->send(200, "text/html", response);
            responseSent = true;
            
            if (typeOTAfile == FILE_TYPE_FILESYSTEM) {
                SPIFFS.end();
            }
            
            ESPHTTPServer.restart_esp();
        } else {
            DEBUGOTA("Update failed at end\r\n");
            Update.printError(Serial);
            request->send(500, "text/html", "FAIL");
        }
    } else {
        request->send(200, "text/html", "No update started");
    }
}

int8_t MODULE_OTA8266_CLASS::fileNameCheck(String filename, fileCompareResult* result) {
    DEBUGOTA("%s\r\n", __FUNCTION__);
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

    // Очистка имени файла от непечатных символов
    String cleanFilename = "";
    for (int i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        if (c >= 32 && c <= 126) {
            cleanFilename += c;
        }
    }
    filename = cleanFilename;

    // Определение типа файла
    if (filename.endsWith(".bin")) {
        if (filename.indexOf("_fs-") > 0) {
            result->fileType = FILE_TYPE_FILESYSTEM;
        } else if (filename.indexOf("-") > 0) {
            result->fileType = FILE_TYPE_FIRMWARE;
        }
    }
    
    // Проверка соответствия устройству
    if (filename.startsWith(BUILD_ENV) == false) {
        DEBUGOTA("\t Wrong device: expected %s, got %s\r\n", 
                 BUILD_ENV, filename.substring(0, strlen(BUILD_ENV)).c_str());
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
    
    int firstDot = versionStr.indexOf('.');
    int secondDot = versionStr.indexOf('.', firstDot + 1);
    int thirdDot = versionStr.indexOf('.', secondDot + 1);
    
    if (firstDot < 0 || secondDot < 0) {
        return _ret;
    }
    
    String majorStr = versionStr.substring(0, firstDot);
    String minorStr = versionStr.substring(firstDot + 1, secondDot);
    String dateStr = versionStr.substring(secondDot + 1, (thirdDot > 0) ? thirdDot : versionStr.length());
    String buildStr = (thirdDot > 0) ? versionStr.substring(thirdDot + 1) : "";

    int32_t fileMajor = majorStr.toInt();
    int32_t fileMinor = minorStr.toInt();
    int32_t fileDate  = dateStr.toInt();
    int32_t fileBuild = buildStr.toInt();
    
    int32_t currentMajor = VERSION_MAJOR;
    int32_t currentMinor = VERSION_MINOR;
    int32_t currentBuild = VERSION_BUILD;
    
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    int32_t currentDate = 0;
    if (timeinfo && timeinfo->tm_year > 70) {
        char currentDateStr[13];
        sprintf(currentDateStr, "%04d%02d%02d%02d%02d", 
                timeinfo->tm_year + 1900,
                timeinfo->tm_mon + 1,
                timeinfo->tm_mday,
                timeinfo->tm_hour,
                timeinfo->tm_min);
        currentDate = atol(currentDateStr);
    }
    
    result->majorDiff = fileMajor - currentMajor;
    result->minorDiff = fileMinor - currentMinor;
    result->dateDiff = fileDate - currentDate;
    result->buildDiff = (result->isDebug) ? (fileBuild - currentBuild) : 0;
    
    bool canUpdate = (result->majorDiff >= 0) && (result->minorDiff >= 0);
    
    if (canUpdate) {
        _ret = 1;
    }
    
    return _ret;
}

bool MODULE_OTA8266_CLASS::isValidFilename(const String& filename) {
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

void MODULE_OTA8266_CLASS::html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    
    DEBUGLOAD("index=%u, len=%u, final=%d\r\n", index, len, final);
    
    if (index == 0) {
        DEBUGOTA("===== UPLOAD START =====\r\n");
        DEBUGOTA("File: %s\r\n", filename.c_str());
        
        uploadError = false;
        updateStarted = false;
        responseSent = false;
        totalFileSize = 0;
        fileUpadedpercent = 0;
        percentLoadedPrev = 0;
        
        prepareSizesForUpdate();
        
        if (typeOTAfile == FILE_TYPE_UNSUPPORTED) {
            DEBUGOTA("ERROR: UNSUPPORTED file type!\r\n");
            uploadError = true;
            return;
        }
        
        if (fileSize > 0) {
            DEBUGOTA("Using pre-received file size: %d bytes\r\n", fileSize);
            
            if (fileSize > freeSketchSpace) {
                DEBUGOTA("ERROR: File too big! %d > %u\r\n", fileSize, freeSketchSpace);
                uploadError = true;
                return;
            }
            
            int updatePartition = U_FLASH;
            if (typeOTAfile == FILE_TYPE_FILESYSTEM) {
                updatePartition = U_FS;
            }
            
            DEBUGOTA("Starting update with size: %d, partition: %d\r\n", fileSize, updatePartition);
            
            if (!_browserFileMD5.isEmpty()) {
                Update.setMD5(_browserFileMD5.c_str());
                DEBUGOTA("MD5 set: %s\r\n", _browserFileMD5.c_str());
            }
            
            // Завершаем файловую систему перед обновлением
            if (typeOTAfile == FILE_TYPE_FILESYSTEM) {
                SPIFFS.end();
                delay(100);
            }
            
            // КЛЮЧЕВОЙ МОМЕНТ: Включаем асинхронный режим Update
            Update.runAsync(true);
            
            if (Update.begin(fileSize, updatePartition)) {
                DEBUGOTA("Update started OK\r\n");
                updateStarted = true;
            } else {
                DEBUGOTA("Update start FAILED\r\n");
                Update.printError(Serial);
                uploadError = true;
                return;
            }
        } else {
            DEBUGOTA("ERROR: No file size received from client!\r\n");
            uploadError = true;
            return;
        }
    }
    
    if (updateStarted && !uploadError && !responseSent) {
        size_t written = Update.write(data, len);
        if (written != len) {
            DEBUGOTA("Write error: written=%d, expected=%d\r\n", written, len);
            uploadError = true;
        } else {
            totalFileSize += written;
            
            if (fileSize > 0) {
                uint16_t percentLoaded = (totalFileSize * 100) / fileSize;
                fileUpadedpercent = percentLoaded;
                
                if ((percentLoaded % 5) == 0 && (percentLoaded != percentLoadedPrev)) {
                    percentLoadedPrev = percentLoaded;
                    DEBUGOTA("Uploaded: %ld bytes %u %%\r\n", totalFileSize, percentLoaded);
                }
            }
        }
    }
    
    if (final) {
        DEBUGOTA("Upload complete, total %ld bytes\r\n", totalFileSize);
        
        if (updateStarted && !uploadError && !responseSent) {
            if (totalFileSize == fileSize) {
                DEBUGOTA("Update ready to finalize\r\n");
            } else {
                DEBUGOTA("WARNING: Size mismatch! Got %ld, expected %d\r\n", 
                         totalFileSize, fileSize);
                uploadError = true;
            }
        }
    }
}