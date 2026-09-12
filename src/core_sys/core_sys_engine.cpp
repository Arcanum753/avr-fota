#include "core_sys.h"
#include "version.h"

#if defined(ESP32)
#include <esp_partition.h>
#include <esp_ota_ops.h>
#endif

#include "debug.h"
#include "core_json/core_json.h"

// ============================================================
// Информация о системе
// ============================================================

String CLASS_CORE_SYS::getResetReason() {
	String reason = "Unknown";

	#if defined(ESP32)
	esp_reset_reason_t r = esp_reset_reason();
	switch(r) {
		case ESP_RST_POWERON:    {reason = "Power on"; break;}
		case ESP_RST_SW:         {reason = "Software reset"; break;}
		case ESP_RST_PANIC:      {reason = "Exception/Panic"; break;}
		case ESP_RST_TASK_WDT:   {reason = "Task watchdog"; break;}
		case ESP_RST_WDT:        {reason = "Hardware watchdog"; break;}
		case ESP_RST_BROWNOUT:   {reason = "Brownout"; break;}
		default: {break;}
	}
	#endif
	#if defined(ESP8266)
	rst_info *resetInfo = ESP.getResetInfoPtr();
	switch(resetInfo->reason) {
		case REASON_DEFAULT_RST:      { reason = "Power on"; break;}
		case REASON_WDT_RST:          { reason = "Watchdog"; break;}
		case REASON_EXCEPTION_RST:    { reason = "Exception"; break;}
		case REASON_SOFT_WDT_RST:     { reason = "Software watchdog"; break;}
		case REASON_SOFT_RESTART:     { reason = "Software restart"; break;}
		case REASON_DEEP_SLEEP_AWAKE: { reason = "Deep sleep wake"; break;}
		case REASON_EXT_SYS_RST:      { reason = "External reset"; break;}
		default: {break;}
	}
	#endif

	return reason;
}

void CLASS_CORE_SYS::serialShowAbout() {
	DBG_MOD("[C_SYS] ", "\n\r\t\t**About** \n\r ");
	DBG_MOD("[C_SYS] ", "Project env: %s\n\r ", BUILD_ENV);
	DBG_MOD("[C_SYS] ", "git branch: %s\n\r ", GIT_BRANCH);
	DBG_MOD("[C_SYS] ", "ver date: %s\n\r ", BUILD_TIME);
	DBG_MOD("[C_SYS] ", "Fiemware ver: %s\n\r ", String(VERSION_BUILD).c_str());
	DBG_MOD("[C_SYS] ", "File system ver: %s\n\r ", getFsVersionStr().c_str());

	DBG_MOD("[C_SYS] ", "Device serial number: %s\n\r ", _sysConfig.deviceSerial.c_str());
	#if defined(ESP32)
	DBG_MOD("[C_SYS] ", "Flash chip size: %u\r\n", ESP.getFlashChipSize());
	#endif
	#if defined(ESP8266)
	DBG_MOD("[C_SYS] ", "Flash chip size: %u\r\n", ESP.getFlashChipRealSize());
	#endif
	DBG_MOD("[C_SYS] ", "Scketch size: %u\r\n", 		ESP.getSketchSize());
#if defined(ESP32)
	{
		// Реальная информация о размере прошивки. Бутлоадер сверяет длину образа
		// из заголовка (ESP.getSketchSize — включает выравнивание/дескриптор) с
		// размером app-раздела, а не «used» из elf-секций (тот занижает и не
		// ловит переполнение — вечный boot-loop).
		const esp_partition_t* runPart = esp_ota_get_running_partition();
		if (runPart) {
			uint32_t sketchSize = ESP.getSketchSize();
			DBG_MOD("[C_SYS] ", "App partition: %s @0x%X, size: %u (0x%X)\r\n",
				runPart->label,
				(unsigned)runPart->address,
				(unsigned)runPart->size,
				(unsigned)runPart->size);
			DBG_MOD("[C_SYS] ", "Sketch image real size: %u (0x%X)\r\n",
				(unsigned)sketchSize,
				(unsigned)sketchSize);
			if (sketchSize > runPart->size) {
				DBG_MOD("[C_SYS] ", "!!! WARNING: image larger than app partition by %u bytes - bootloader rejects it (boot loop) !!!\r\n",
					(unsigned)(sketchSize - runPart->size));
			} else {
				DBG_MOD("[C_SYS] ", "Free in app partition: %u bytes (%u%%)\r\n",
					(unsigned)(runPart->size - sketchSize),
					(unsigned)((runPart->size - sketchSize) * 100UL / runPart->size));
			}
			DBG_MOD("[C_SYS] ", "Free sketch space (OTA target): %u\r\n", (unsigned)ESP.getFreeSketchSpace());
		}
	}
#endif
	if (_fs) {
#if defined(ESP32)
		DBG_MOD("[C_SYS] ", "FS total: %u\r\n", 		_fs->totalBytes());
		DBG_MOD("[C_SYS] ", "FS used: %u\r\n", 		_fs->usedBytes());
		DBG_MOD("[C_SYS] ", "FS free: %u\r\n", 		_fs->totalBytes() - _fs->usedBytes());
#endif
#if defined(ESP8266)
		FSInfo fs_info;
		if (_fs->info(fs_info)) {
			DBG_MOD("[C_SYS] ", "FS total: %u\r\n", 		fs_info.totalBytes);
			DBG_MOD("[C_SYS] ", "FS used: %u\r\n", 		fs_info.usedBytes);
			DBG_MOD("[C_SYS] ", "FS free: %u\r\n", 		fs_info.totalBytes - fs_info.usedBytes);
		}
#endif
	}

	DBG_MOD("[C_SYS] ", "wifi ssid: %s \n", WiFi.SSID().c_str());
	DBG_MOD("[C_SYS] ", "IP Address: %s \n", WiFi.localIP().toString().c_str());
	#if defined(ESP32)
	DBG_MOD("[C_SYS] ", "WifiHostName  %s \n\r", 	WiFi.getHostname());
	#endif
	#if defined(ESP8266)
	DBG_MOD("[C_SYS] ", "WifiHostName  %s \n\r", 	WiFi.hostname().c_str());
	#endif

	DBG_MOD("[C_SYS] ", "Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
	DBG_MOD("[C_SYS] ", "DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
	DBG_MOD("[C_SYS] ", "local DNS hostname  http://%s.local \n\r", getHostName().c_str());
	DBG_MOD("[C_SYS] ", "or you can connect directly  http://%s \n\r", WiFi.localIP().toString().c_str());
}

// ============================================================
// FS-версия
// ============================================================

void CLASS_CORE_SYS::cacheFsVersionInfo() {
	if (_fsVersionCached) return;

	_fsVersionValid = false;
	_cachedFsVersionStr = "";

	if (!_fs) {
		// FS ещё не смонтирован/не передан: не кэшируем ошибку, чтобы прочитать
		// версию после core_sys.begin() (getFsVersionStr вызывается и в setup()
		// до инициализации ядра).
		DEBUGSYS("cacheFsVersionInfo: No FS mounted\n");
		return;
	}

	File jsonFile = _fs->open(FS_VERSION_JSON_PATH, "r");
	if (!jsonFile) {
		DEBUGSYS("cacheFsVersionInfo: %s not found\n", FS_VERSION_JSON_PATH);
		_fsVersionCached = true;
		return;
	}

	String jsonStr;
	while (jsonFile.available()) { jsonStr += (char)jsonFile.read(); }
	jsonFile.close();

	DEBUGSYS("cacheFsVersionInfo: Read %d bytes\n", jsonStr.length());

	_fsVersionValid = parseVersionFromJson(jsonStr, _cachedFsDate, _cachedFsBuild, _cachedFsMajor, _cachedFsMinor);
	_fsVersionCached = true;
}

bool CLASS_CORE_SYS::parseVersionFromJson(const String& jsonStr, int64_t& date, int32_t& build, int32_t& major, int32_t& minor) {
	if (!core_json.jsonParseNestedInt(jsonStr, "filesystem|version|major", major)) return false;
	if (!core_json.jsonParseNestedInt(jsonStr, "filesystem|version|minor", minor)) return false;

	if (!core_json.jsonParseNestedInt64(jsonStr, "filesystem|version|date", date)) return false;

	if (!core_json.jsonParseNestedInt(jsonStr, "filesystem|version|build", build)) return false;

	core_json.jsonParseNestedStr(jsonStr, "filesystem|version|full_string", _cachedFsVersionStr);

	DEBUGSYS("parseVersionFromJson: FS version %d.%d.%lld.%d (%s)\n",
		major, minor, date, build, _cachedFsVersionStr.c_str());

	return true;
}

String CLASS_CORE_SYS::getFsVersionStr() {
	if (!_fsVersionCached) { cacheFsVersionInfo(); }
	return _cachedFsVersionStr;
}

void CLASS_CORE_SYS::invalidateFsVersionCache() {
	_fsVersionCached = false;
	_fsVersionValid = false;
}

bool CLASS_CORE_SYS::getFsVersion(int64_t& date, int32_t& build, int32_t& major, int32_t& minor) {
	if (!_fsVersionCached) { cacheFsVersionInfo(); }
	if (!_fsVersionValid) { return false; }
	date = _cachedFsDate;
	build = _cachedFsBuild;
	major = _cachedFsMajor;
	minor = _cachedFsMinor;
	return true;
}
