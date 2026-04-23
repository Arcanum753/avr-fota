#if defined(ESP32)
#include <SPIFFS.h>
#include <esp32-hal-gpio.h>
#elif defined(ESP8266)
#include <FS.h>
#endif

#include"version.h"
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include "FSWebServerLib.h"
#include "common.h"
#include "core_ota.h"
#include "core_ota_version.h"

CORE_OTA_CLASS modOtaClass(false);

CORE_OTA_CLASS :: CORE_OTA_CLASS (bool _in) {
	 dumb = _in;
 }
 
#if ESP32
    void CORE_OTA_CLASS::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void CORE_OTA_CLASS::setFs(FS* fs)	// esp8266/esp32 flash file system
#endif
{	_fs = fs;	}


void CORE_OTA_CLASS::begin(String _hostname, String _password){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	prepareSizesForUpdate();
	ConfigureOTA(_hostname, _password);
 }

void CORE_OTA_CLASS::prepareSizesForUpdate (){
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	maxSketchSpace   = (ESP.getSketchSize() - 0x1000) & 0xFFFFF000;
	freeSketchSpace  = ESP.getFreeSketchSpace();
}


bool  CORE_OTA_CLASS::ConfigureOTA( String _hostname, String _password) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	
	// No authentication by default
	if (_hostname != "") {
	ArduinoOTA.setHostname(_hostname.c_str());
	DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else { return false;	}

	if (_password != "") {
		ArduinoOTA.setPassword(_password.c_str());
		DEBUGOTA("OTA password set %s\n", _password.c_str());
	} else { return false;	}	


#ifndef RELEASE
	ArduinoOTA.onStart([]() {
		DEBUGOTA("\r\n ArduinoOTA start. \r\n");
	});

#if defined(ESP32)
	ArduinoOTA.onEnd(std::bind([](fs::SPIFFSFS* fs)
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
		DEBUGOTA("Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) 			{DEBUGOTA("Auth Failed\r\n");		}
		else if (error == OTA_BEGIN_ERROR) 		{DEBUGOTA("Begin Failed\r\n");		}
		else if (error == OTA_CONNECT_ERROR)	{DEBUGOTA("Connect Failed\r\n");	}
		else if (error == OTA_RECEIVE_ERROR) 	{DEBUGOTA("Receive Failed\r\n");	}
		else if (error == OTA_END_ERROR) 		{DEBUGOTA("End Failed\r\n");		}
	});
	DEBUGOTA("\r\n ArduinoOTA Ready \r\n");
#endif // RELEASE
	ArduinoOTA.begin();

	return true;
}


 void CORE_OTA_CLASS::loopHandler(){
	 ArduinoOTA.handle();
 }


 void CORE_OTA_CLASS::webInit() {
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
        //what do when we finish
            if (!ESPHTTPServer.checkAuth(request)) {	return request->requestAuthentication(); };
            updateFileExecute (request);
    }, [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        // uploading
            html_uploadUpdateFile(request, filename, index, data, len, final);
    });

    ESPHTTPServer.on("/update/ver", [this](AsyncWebServerRequest *request) {
        html_ver_get(request);
    });

 }

void CORE_OTA_CLASS::html_fileuploadProgress(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	String values = "";
    values += "percent|"    + (String)fileUpadedpercent + "|div\n";
    request->send(200, "text/plain", values);
}


void CORE_OTA_CLASS::html_md5_set(AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	_browserFileMD5 = "";
	
	DEBUGOTA("Arg number: %d\r\n", request->args());
	if (request->args() > 0)  {	// Read hash
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

void CORE_OTA_CLASS::html_filename_check(AsyncWebServerRequest *request) {
    DEBUGOTA(__FUNCTION__); DEBUGOTA("\r\n");
    String values = "";
    String updateOKstr = "";
    String updateFiletype = "";
    String updateFileMatcheD = "";
    String updateIsDebug = "";
    
    updateFiletype = OTA_STR_UNSUPPORTED;   
    updateFileMatcheD = OTA_STR_NAMEDIFF;   
    updateIsDebug = "0";

    if (_updateFileName.length() == 0 || !isValidFilename(_updateFileName)) {
        updateOKstr = "ERROR" ;
        values += "updStatus|" + updateOKstr + "|div\n";
        request->send(200, "text/plain", values);
        return;   
    }

    fileCompareResult result;
    fileNameCheck(_updateFileName, &result);
    
    // Определяем тип файла
    if (result.fileType == FILE_TYPE_UNSUPPORTED)   { updateFiletype = OTA_STR_UNSUPPORTED; }
    if (result.fileType == FILE_TYPE_FIRMWARE)      { updateFiletype = OTA_STR_FIRMWARE; }
    if (result.fileType == FILE_TYPE_FILESYSTEM)    { updateFiletype = OTA_STR_FILESYSTEM; }
    
    // Проверка имени
    if (result.nameMatch == 1) { updateFileMatcheD = OTA_STR_NAMEMATCH; }
    
    // Флаг отладочной версии
    updateIsDebug = String(result.isDebug);
    
    typeOTAfile = result.fileType;
    
    // Проверка свободного места (только для прошивки)
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
    DEBUGOTA("\t isDebug: %s\r\n", updateIsDebug.c_str());
    DEBUGOTA("\t updVerDiffName: %s %d %d %d %d\r\n",
             updateFileMatcheD.c_str(),
             result.majorDiff,
             result.minorDiff,      // теперь minor вместо core
             result.dateDiff,        // дата
             result.buildDiff);

    // Формируем ответ
    values += "updStatus|"         + updateOKstr           + "|div\n";
    values += "updFileType|"       + updateFiletype        + "|div\n";
    values += "updSizeFree|"       + String(freeSketchSpace) + "|div\n";
    values += "updSizeMax|"        + String(maxSketchSpace)  + "|div\n";
    
    values += "updVerDiffName|"    + updateFileMatcheD     + "|div\n";
    values += "updVerDiffMaj|"     + String(result.majorDiff) + "|div\n";
    values += "updVerDiffMinor|"   + String(result.minorDiff) + "|div\n";  // переименовано
    values += "updVerDiffDate|"    + String(result.dateDiff)  + "|div\n";  // новый
    values += "updVerDiffBuild|"   + String(result.buildDiff) + "|div\n";
    values += "updIsDebug|"        + updateIsDebug          + "|div\n";    // новый

    request->send(200, "text/plain", values);
}



void CORE_OTA_CLASS::updateFileExecute (AsyncWebServerRequest *request) {
	DEBUGOTA(__FUNCTION__);	DEBUGOTA("\r\n");
	AsyncWebServerResponse *response = request->beginResponse(200, "text/html", 
		(Update.hasError()) ? "FAIL" : "<META http-equiv=\"refresh\" content=\"15;URL=/update\">Update correct. Restarting..."
	);
	response->addHeader("Connection", "close");
	response->addHeader("Access-Control-Allow-Origin", "*");
	request->send(response);
	if (this->_fs) { this->_fs->end(); } //this->_fs->end();
	ESPHTTPServer.restart_esp();

}


int8_t CORE_OTA_CLASS::fileNameCheck(String filename, fileCompareResult* result) {
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
    
    int32_t currentMajor = VERSION_MAJOR;
    int32_t currentMinor = VERSION_MINOR;
    int64_t currentDate = VERSION_DATE;
    int32_t currentBuild = VERSION_BUILD;
    
    DEBUGOTA("\t Current: major=%d, minor=%d, date=%lld, build=%d\r\n", 
             currentMajor, currentMinor, currentDate, currentBuild);
    
    result->majorDiff = fileMajor - currentMajor;
    result->minorDiff = fileMinor - currentMinor;
    result->dateDiff = fileDate - currentDate;
    result->buildDiff = (result->isDebug) ? (fileBuild - currentBuild) : 0;
    
    DEBUGOTA("\t Diffs: major=%d, minor=%d, date=%lld, build=%d\r\n", 
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

bool CORE_OTA_CLASS::isValidFilename(const String& filename) {
    if (filename.length() == 0 || filename.length() > 100) return false;
    
    for (int i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        // Разрешаем только буквы, цифры, точки, дефисы, подчёркивания
        if (!((c >= 'a' && c <= 'z') || 
              (c >= 'A' && c <= 'Z') || 
              (c >= '0' && c <= '9') || 
              c == '.' || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}



void CORE_OTA_CLASS::html_uploadUpdateFile(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    String values = "";
    static long totalSize = 0;
    static bool errorOccurred = false;
    static bool responseSent = false;  
    int updatePartition = 1;
    
    DEBUGLOAD("index=%u, len=%u, final=%d\r\n", index, len, final);
    
    if (index == 0) { // UPLOAD_FILE_START
        DEBUGOTA("===== UPLOAD START =====\r\n");
        DEBUGOTA("File: %s\r\n", filename.c_str());
        
        errorOccurred = false;
        responseSent = false;  
        totalSize = 0;
        
        // Подготовка размеров
        prepareSizesForUpdate();
        
        // Проверка типа файла
        if (typeOTAfile == FILE_TYPE_UNSUPPORTED) {
            values = "OTA Update error UNSUPPORTED file!";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }
        
        // Проверка имени файла
        if (!isValidFilename(filename)) {
            values = "Invalid filename";
            DEBUGOTA("%s: %s\n", values.c_str(), filename.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }

        // Проверка размера
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

        // Установка MD5
        if (_browserFileMD5 != NULL && _browserFileMD5 != "") {
            Update.setMD5(_browserFileMD5.c_str());
            DEBUGOTA("Hash from browser: %s\r\n", _browserFileMD5.c_str());
        } else {
            values = "OTA Update error no MD5 hash!";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            return;
        }
        
        // Выбор раздела для обновления
#if defined(ESP32)
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { updatePartition = U_SPIFFS; }
#elif defined(ESP8266)
        if (typeOTAfile == FILE_TYPE_FILESYSTEM) { updatePartition = U_FS; }
#endif
        if (typeOTAfile == FILE_TYPE_FIRMWARE) { updatePartition = U_FLASH; }
        
        DEBUGOTA("Update partition: %d\r\n", updatePartition);
        
        // Завершаем файловую систему перед обновлением
        if (_fs) { 
            DEBUGOTA("Ending filesystem...\n");
            _fs->end(); 
            delay(100);
        }
        
        // ВАЖНО: для ESP8266 включаем асинхронный режим
#if defined(ESP8266)
        DEBUGOTA("Enabling async mode for ESP8266\n");
        Update.runAsync(true);
#endif
        
        // Начинаем обновление
        if (Update.begin(_updateFileSize, updatePartition) == false) {
#ifdef DEBUG_OTA
            Update.printError(DEBUGOTASER);
#endif
            values = "OTA Update error at begin";
            DEBUGOTA("%s\n", values.c_str());
            request->send(500, "text/plain", values);
            errorOccurred = true;
            
            // Восстанавливаем файловую систему при ошибке
            if (_fs) {
                DEBUGOTA("Remounting filesystem after error...\n");
#if defined(ESP32)
                _fs->begin(true);
#elif defined(ESP8266)
                _fs->begin();
#endif
            }
            return;
        }
    }
    
    if (errorOccurred)  { return; }
    if (responseSent)   { return; }
    
    // Запись данных
    totalSize += len;
    
    // Вычисление процента
    uint16_t percentLoaded = (totalSize * 100) / _updateFileSize;
    fileUpadedpercent = percentLoaded;
    if ((percentLoaded % 5) == 0 && (percentLoaded != percentLoadedPrev)) {
        percentLoadedPrev = percentLoaded;
        DEBUGOTA("Uploaded: %ld bytes %u %%\r\n", totalSize, percentLoaded);
    }

    // Запись во flash
    size_t written = Update.write(data, len);
    if (written != len) {
        values = "OTA Update error data load!";
        DEBUGOTA("%s len=%d written=%d total=%ld\n", values.c_str(), len, written, totalSize);
        request->send(500, "text/plain", values);
        errorOccurred = true;
        responseSent = true;  
        
#if defined(ESP32)
        Update.abort();
#elif defined(ESP8266)
        Update.end();
#endif
        
        // Восстанавливаем файловую систему при ошибке
        if (_fs) {
            DEBUGOTA("Remounting filesystem...\n");
#if defined(ESP32)
            _fs->begin(true);
#elif defined(ESP8266)
            _fs->begin();
#endif
        }
        return;
    }
    
    // Завершение загрузки
    if (final) {
        if (errorOccurred) {
            return;
        }
        
        String updateHash;
        DEBUGOTA("Applying update...\n");
        if (Update.end(true)) {
            updateHash = Update.md5String();
            DEBUGOTA("Upload finished. Calculated MD5: %s\r\n", updateHash.c_str());
            DEBUGOTA("Update Success: %u\nRebooting...\r\n", request->contentLength());

            values = "Update successful! Device will restart in 3 seconds..."; 
            request->send(200, "text/plain", values);  
            responseSent = true;  
            delay(100); 
        } else {
            updateHash = Update.md5String();
            DEBUGOTA("Upload failed. Calculated MD5: %s\r\n", updateHash.c_str());
#ifdef DEBUG_OTA
            Update.printError(DEBUGOTASER);
#endif
            // При ошибке в конце тоже возвращаем ФС
            if (_fs) {
                DEBUGOTA("Remounting filesystem after failure...\n");
#if defined(ESP32)
                _fs->begin(true);
#elif defined(ESP8266)
                _fs->begin();
#endif
            }
        }
    }
}



String CORE_OTA_CLASS::getVersionStr(){
    return String(CORE_OTA_VERSION);
}

String CORE_OTA_CLASS::getGeneratedTime(){
    return String(CORE_OTA_GENERATED_TIME);
}

String CORE_OTA_CLASS::getCommitDateStr(){
    return String(CORE_OTA_COMMIT_DATE_STR);
}



void CORE_OTA_CLASS::html_ver_get(AsyncWebServerRequest *request) {
    DEBUGOTA("%s\n\r", __FUNCTION__);
    String values = "";
    values += "otaversion|"     + getVersionStr()    + "|dev\n";
    values += "otagentime|"     + getGeneratedTime() + "|dev\n";
    values += "otagendate|"     + getCommitDateStr() + "|dev\n";
    request->send(200, "text/plain", values);
}





