#include <cstddef>
#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <LittleFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_isp.h"
#include "prog_isp.h"

Class_SubIsp progIsp(0);
Class_SubIsp::Class_SubIsp(uint8_t in): Class_ProgBase(in){ }

bool Class_SubIsp::chipSpecificInit() {
	avrprog.begin();
	avrprog.setFs(_fs);
	return true;
}

void Class_SubIsp::onFlashComplete() {
	if (avrprog.isFlashError()) {
		DEBUGLOGPROG("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		String errorText = avrprog.getFlashErrorString();
		String errorStage = avrprog.getFlashErrorStage();
		String errorPercent = String(avrprog.getFlashErrorPercent());
		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, errorPercent);
		_progResult = 1;
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGPROG("Programming error: %s, stage=%s, pct=%s, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent.c_str());
	} else {
		DEBUGLOGPROG("onFlashComplete: success for %s\r\n", _flashPath.c_str());

		String elapsedStr = "";
		if (_progStartTime > 0) {
			elapsedStr = String(millis() - _progStartTime);
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr);

		DEBUGLOGPROG("Programming success, saved prog date to filelist: %s, time=%sms\r\n", _flashNtpStr.c_str(), elapsedStr.c_str());

		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;

		DEBUGLOGPROG("Programming end \r\n");
	}
}

bool Class_SubIsp::chip_IsConnected() {
	if (_chipStatusTime > 0 && (millis() - _chipStatusTime) < (CHIP_STATUS_TIMEOUT * 1000)) {
		return _chipConnected;
	}
	String sig = avrprog.chipSignRead();
	_chipStatusTime = millis();
	if (sig.length() > 0 && sig != "0x000000") {
		_chipConnected = true;
		_chipIdstr = sig;
	} else {
		_chipConnected = false;
		_chipIdstr = "";
	}
	DEBUGLOGISP("chip_IsConnected: %s (sig=%s)\r\n", _chipConnected ? "YES" : "NO", _chipIdstr.c_str());
	return _chipConnected;
}

void Class_SubIsp::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);

	if (_progRunning || avrprog.isFlashBusy()) {
		request->send(200, "text/plain", "chipstatus|busy|div\n");
		return;
	}

	avrprog.startChipCheck();
	request->send(200, "text/plain", "chipstatus|checking|div\n");
}

void Class_SubIsp::onChipCheckComplete(const String &signature) {
	DEBUGLOGISP("onChipCheckComplete: signature=%s\r\n", signature.c_str());

	_chipStatusTime = millis();
	if (signature.length() > 0 && signature != "0x000000") {
		_chipConnected = true;
		_chipIdstr = signature;
		DEBUGLOGISP("onChipCheckComplete: chip CONNECTED (sig=%s)\r\n", signature.c_str());
	} else {
		_chipConnected = false;
		_chipIdstr = "";
		DEBUGLOGISP("onChipCheckComplete: chip NOT CONNECTED\r\n");
	}
}

void Class_SubIsp::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}

	if (avrprog.isFlashBusy()) {
		DEBUGLOGISP("web_FileUpload2Chip: BUSY\n\r");
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

	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();

	uint32_t flashStart = 0;
	uint32_t chipMemSize = 32768;
	uint32_t pageSize = 128;

	String expectedChipName = CfgFile_Prog.chip_name;
	String signature = avrprog.chipSignRead();

	if (signature.length() == 0 || signature == "0x000000") {
		if (expectedChipName.length() > 0) {
			String errorText = "Chip offline - unable to read signature";
			DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
			filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
			return request->send(423, "text/plain", errorText);
		}
		DEBUGLOGISP("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	} else {
		DEBUGLOGISP("web_FileUpload2Chip: detected chip signature=%s\n\r", signature.c_str());

		ChipConfigAvr_t chipCfg;
		bool found = chipCfg_FindBySignature(signature, chipCfg);

		if (found) {
			if (expectedChipName.length() > 0 && chipCfg.name != expectedChipName) {
				String errorText = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chipCfg.name + "'";
				DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			DEBUGLOGISP("web_FileUpload2Chip: using config for %s (flash=%u page=%u)\n\r",
				chipCfg.name.c_str(), chipMemSize, pageSize);
		} else {
			if (expectedChipName.length() > 0) {
				String errorText = "Chip '" + expectedChipName + "' not found in avrisp_cfg.json (signature=" + signature + ")";
				DEBUGLOGISP("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			DEBUGLOGISP("web_FileUpload2Chip: chip signature=%s not in avrisp_cfg.json, using defaults\n\r", signature.c_str());
		}
	}

	if (!avrprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize)) {
		DEBUGLOGISP("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}

	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();

	avrprog.beginFlashStep();

	request->send(200, "text/plain", "ok");
	DEBUGLOGISP("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}

bool Class_SubIsp::chipCfg_FindBySignature(const String &signature, ChipConfigAvr_t &cfg) {
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

void Class_SubIsp::avrFusesRead(AsyncWebServerRequest *request) {
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

void Class_SubIsp::avrWebFusesWrite(AsyncWebServerRequest *request) {
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

	if (!hasHigh && !hasLow && !hasLock && !hasExt) {
		request->send(500, "text/plain", "BAD ARGS: no fuse values provided");
		return;
	}

	avrprog.chipFusesWrite(high, low, lock, ext);
	request->send(200, "text/plain", "OK");
}

void Class_SubIsp::web_AvrCfgInfo(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);
	String values = "";

	CfgFile_ProgBase_t cfg;
	cfg_FileStructGet(cfg);
	values += "projname|" + cfg.project_name + "|input\n";
	values += "chipname|" + cfg.chip_name + "|input\n";

	if (_chipIdstr.length() > 0 && _chipIdstr != "0x000000") {
		values += "signature|" + _chipIdstr + "|div\n";
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

void Class_SubIsp::web_AvrCfgSave(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);

	if (request->args() == 0) {
		request->send(500, "text/plain", "BAD ARGS");
		return;
	}

	CfgFile_ProgBase_t newCfg;
	cfg_FileStructGet(newCfg);

	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGISP("Arg %d: %s = %s\r\n", i, request->argName(i).c_str(), request->arg(i).c_str());
		if (request->argName(i) == "projname") {
			newCfg.project_name = urldecode(request->arg(i));
			continue;
		}
		if (request->argName(i) == "chipname") {
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
		DEBUGLOGISP("web_AvrCfgSave: saved project='%s' chip='%s'\r\n",
			newCfg.project_name.c_str(), newCfg.chip_name.c_str());
	} else {
		request->send(500, "text/plain", "ERROR|Failed to save configuration");
	}
}

void Class_SubIsp::web_AvrCfgReadSignature(AsyncWebServerRequest *request) {
	DEBUGLOGISP("%s\n\r", __FUNCTION__);
	String values = "";
	_chipIdstr = avrprog.chipSignRead();
	values += "signature|" + _chipIdstr + "|div\n";
	request->send(200, "text/plain", values);
}

void Class_SubIsp::registerCustomRoutes() {
	ESPHTTPServer.on("/avr/fuseread", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		avrFusesRead(request);
	});
	ESPHTTPServer.on("/avr/fusewrite", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		avrWebFusesWrite(request);
	});
	ESPHTTPServer.on("/avr/info", [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgInfo(request);
	});
	ESPHTTPServer.on("/avr/save", HTTP_POST, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgSave(request);
	});
	ESPHTTPServer.on("/avr/readsignature", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		web_AvrCfgReadSignature(request);
	});
	ESPHTTPServer.on("/avrcfg", HTTP_GET, [this](AsyncWebServerRequest *request) {
		if (!ESPHTTPServer.checkAuth(request)) { return request->requestAuthentication(); };
		ESPHTTPServer.handleFileRead("/web/avrcfg.html", request);
	});
}

void Class_SubIsp::_chipInfoAppendFields(JsonObject &out, JsonObject &chip) {
	out["signature"] = chip["signature"].as<const char*>();
	out["flash"] = chip["flash_size"].as<uint32_t>();
	out["page"] = chip["page_size"].as<uint32_t>();
}
