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
#include "core_sys/core_sys.h"
#include "core_led/core_led.h"

// Global flag to prevent double _fs->end() crashes
bool _ota_fsEndCalled = false;

// ============================================================
// Паттерны светодиодной индикации обновления (кассета модуля)
// ============================================================

static const char patOtaFw[]    PROGMEM = "*.*.*.*.*.*.*.*.*.*";
static const char patOtaFs[]    PROGMEM = "***...***...***...";
static const char patOtaErr[]   PROGMEM = "*.*.*";

// ============================================================
// Светодиодная индикация обновления
// ============================================================

void ledMacrosUpdateFirmware()		{	ledSetState(LED_PRIO_OTA, patOtaFw, -1); }
void ledMacrosUpdateFilesystem()	{	ledSetState(LED_PRIO_OTA, patOtaFs, -1); }
void ledMacrosUpdateError()			{	ledSetState(LED_PRIO_OTA, patOtaErr, 2); }

// ============================================================
// Конкретная логика модуля
// ============================================================

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
