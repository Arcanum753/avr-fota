#include <cstddef>
#include <cstring>
#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ESP32)
#include <esp_task_wdt.h>
#include <SPIFFS.h>
#endif

#include "../module_prog/module_prog.h"
#include "submodule_swd.h"
#include "prog_swd.h"
#include "swd.h"

Class_SubSwd progSwd(0);
Class_SubSwd::Class_SubSwd(uint8_t in): Class_ProgBase(in){ }

bool Class_SubSwd::chipSpecificInit() {
	swd_gpio_init();
	swdprog.setFs(_fs);
	return true;
}

void Class_SubSwd::onFlashComplete() {
	if (swdprog.isFlashError()) {
		DEBUGLOGSWD("onFlashComplete: ERROR during programming of %s\r\n", _flashPath.c_str());
		String errorText = swdprog.getFlashErrorString();
		String errorStage = swdprog.getFlashErrorStage();
		uint8_t errorPercent = swdprog.getFlashErrorPercent();
		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;
		}
		filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText, elapsedStr, errorStage, (String)errorPercent);
		_progResult = 1;
		_progRunning = false;
		_uploadPercent = 0;
		DEBUGLOGSWD("Programming error: %s, stage=%s, percent=%u, saved prog status to filelist\r\n", errorText.c_str(), errorStage.c_str(), errorPercent);
	} else {
		DEBUGLOGSWD("onFlashComplete: success for %s\r\n", _flashPath.c_str());

		String elapsedStr = "";
		if (_progStartTime > 0) {
			uint32_t elapsed = millis() - _progStartTime;
			elapsedStr = (String)elapsed;
		}

		filelist_SetProgStatus(_flashPath, _flashNtpStr, "ok", "", elapsedStr);

		DEBUGLOGSWD("Programming success, saved prog date to filelist: %s, time=%sms\r\n", _flashNtpStr.c_str(), elapsedStr.c_str());

		_progResult = 0;
		_progRunning = false;
		_uploadPercent = 100;

		DEBUGLOGSWD("Programming end \r\n");
	}
}

int Class_SubSwd::prog_Programm(String path, String fwTime) {
	DEBUGLOGSWD(__PRETTY_FUNCTION__); DEBUGLOGSWD("\r\n");
	DEBUGLOGSWD(" file %s time %s \r\n", path.c_str(), fwTime.c_str());

	if (!_fs) { return ERR_CFG; }
	if (!_fs->exists(path)) { return ERR_NOFILE; }

	int _res = ERR_OPENFILE;
	swdprog.stm32Fx_begin();
	_res = swdprog.stm32_ChipProgrammMain(path);

	String progStatus = (_res == 0) ? "ok" : "error";
	filelist_SetProgStatus(path, fwTime, progStatus);
	DEBUGLOGSWD("Programming %s, saved prog status to filelist: %s date=%s\r\n",
		(_res == 0) ? "success" : "failed", path.c_str(), fwTime.c_str());

	DEBUGLOGSWD("Programming end \r\n");
	return _res;
}

bool Class_SubSwd::chip_IsConnected() {
	if (_progRunning || swdprog.isFlashBusy()) {
		return true;
	}
	if (!_chipConnected) return false;
	if ((millis() - _chipStatusTime) >= (CHIP_STATUS_TIMEOUT * 1000UL)) {
		_chipConnected = false;
		return false;
	}
	return true;
}

void Class_SubSwd::web_CheckChipStatus(AsyncWebServerRequest *request) {
	DEBUGLOGSWD("%s\n\r", __FUNCTION__);
	String values = "";

	if (_progRunning || swdprog.isFlashBusy()) {
		values += "status|busy|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	if (swdprog.isChipCheckBusy()) {
		values += "status|checking|div\n";
		request->send(200, "text/plain", values);
		return;
	}

	swdprog.startChipCheck();

	values += "status|checking|div\n";
	request->send(200, "text/plain", values);
	DEBUGLOGSWD("web_CheckChipStatus: EERTOS chip check started\r\n");
}

void Class_SubSwd::onChipCheckComplete(uint32_t chipId) {
	DEBUGLOGSWD("%s: chipId=0x%08x\n\r", __FUNCTION__, chipId);

	_chipStatusTime = millis();

	if (chipId != 0) {
		_chipConnected = true;
		_chipId = chipId;
		DEBUGLOGSWD("onChipCheckComplete: chip connected, ID=0x%08x\r\n", chipId);
	} else {
		_chipConnected = false;
		_chipId = 0;
		DEBUGLOGSWD("onChipCheckComplete: chip NOT detected\r\n");
	}
}

void Class_SubSwd::web_FileUpload2Chip(AsyncWebServerRequest *request) {
	if (_fs == nullptr) 		{	return request->send(500, "text/plain", "FS not initialized");	}
	if (request->args() == 0) 	{	return request->send(500, "text/plain", "BAD ARGS");	}

	if (swdprog.isFlashBusy()) {
		DEBUGLOGSWD("web_FileUpload2Chip: BUSY\n\r");
		return request->send(423, "text/plain", "busy");
	}
	if (_progRunning) {
		DEBUGLOGSWD("web_FileUpload2Chip: _progRunning already true\r\n");
		return request->send(423, "text/plain", "busy");
	}

	String path = "";
	for (uint8_t i = 0; i < request->args(); i++) {
		DEBUGLOGSWD("Arg %d: %s\r\n", i, request->arg(i).c_str());
		if (request->argName(i) == "path") 	{ path = urldecode(request->arg(i));	continue; }
	}
	if (path == "/")				{	return request->send(500, "text/plain", "BAD PATH");	}
	if (!path.startsWith("/")) 		{path = "/" + path;}
	if (!_fs->exists(path)) 		{	return request->send(404, "text/plain", "FileNotFound");	}

	DEBUGLOGSWD("\t upload status: %s\r\n", path.c_str());

	_flashPath = path;
	_flashNtpStr = NTP.getTimeDateString();

	uint32_t flashStart = DEFAULT_FLASH_START_ADDR;
	uint32_t chipMemSize = DEFAULT_PAGE_SIZE * 64;
	uint32_t pageSize = DEFAULT_PAGE_SIZE;
	uint32_t wordSize = DEFAULT_WORD_SIZE;
	uint32_t cswValue = DEFAULT_CSW_VALUE;

	String expectedChipName = CfgFile_Prog.chip_name;

	swd_gpio_init();
	uint32_t idcode = swd_init();

	if (idcode == 0) {
		if (expectedChipName.length() > 0) {
			String errorText = "Chip offline - unable to read IDCODE";
			DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
			filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
			return request->send(423, "text/plain", errorText);
		}
		DEBUGLOGSWD("web_FileUpload2Chip: chip not detected, using defaults\n\r");
	} else {
		DEBUGLOGSWD("web_FileUpload2Chip: detected chip ID=0x%08x\n\r", idcode);

		ChipConfig_t chipCfg;
		bool found = chipCfg_FindById(idcode, chipCfg);

		if (found) {
			if (expectedChipName.length() > 0 && chipCfg.name != expectedChipName) {
				String errorText = "Chip mismatch: expected '" + expectedChipName + "', detected '" + chipCfg.name + "'";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			flashStart = chipCfg.flash_start;
			chipMemSize = chipCfg.flash_size;
			pageSize = chipCfg.page_size;
			wordSize = chipCfg.word_size;
			cswValue = chipCfg.csw_value;
			swdprog.setChipFamily(chipCfg.family);
			DEBUGLOGSWD("web_FileUpload2Chip: using config for %s (family=%s flash=%u start=0x%08x page=%u word=%u csw=0x%08x)\n\r",
				chipCfg.name.c_str(), chipCfg.family.c_str(), chipMemSize, flashStart, pageSize, wordSize, cswValue);
		} else {
			if (expectedChipName.length() > 0) {
				char idStr[12];
				snprintf(idStr, sizeof(idStr), "0x%08x", idcode);
				String errorText = "Chip '" + expectedChipName + "' not found in swd_cfg.json (IDCODE=" + String(idStr) + ")";
				DEBUGLOGSWD("web_FileUpload2Chip: %s\n\r", errorText.c_str());
				filelist_SetProgStatus(_flashPath, _flashNtpStr, "error", errorText);
				return request->send(423, "text/plain", errorText);
			}
			swdprog.setChipFamily("stm32f1");
			DEBUGLOGSWD("web_FileUpload2Chip: chip ID=0x%08x not in swd_cfg.json, using defaults (family=stm32f1)\n\r", idcode);
		}
	}

	if (!swdprog.startFlash(flashStart, _flashPath, chipMemSize, pageSize, wordSize, cswValue)) {
		DEBUGLOGSWD("web_FileUpload2Chip: startFlash() failed\r\n");
		return request->send(500, "text/plain", "startFlash failed");
	}

	_progRunning = true;
	_progResult = -1;
	_progStartTime = millis();

	swdprog.beginFlashStep();

	request->send(200, "text/plain", "ok");
	DEBUGLOGSWD("web_FileUpload2Chip: EERTOS flash started for %s\r\n", _flashPath.c_str());
}

bool Class_SubSwd::chipCfg_FindById(uint32_t idcode, ChipConfig_t &cfg) {
	DEBUGLOGSWD("%s: searching for ID=0x%08x\n\r", __FUNCTION__, idcode);

	if (!_fs) {
		DEBUGLOGSWD("chipCfg_FindById: FS not initialized\n\r");
		return false;
	}
	if (!_fs->exists(SWD_CFG_JSON)) {
		DEBUGLOGSWD("chipCfg_FindById: %s not found\n\r", SWD_CFG_JSON);
		return false;
	}

	File file = _fs->open(SWD_CFG_JSON, "r");
	if (!file) {
		DEBUGLOGSWD("chipCfg_FindById: failed to open %s\n\r", SWD_CFG_JSON);
		return false;
	}

	JsonDocument doc;
	DeserializationError err = deserializeJson(doc, file);
	file.close();

	if (err) {
		DEBUGLOGSWD("chipCfg_FindById: JSON parse error: %s\n\r", err.c_str());
		return false;
	}

	JsonArray chips = doc["chips"].as<JsonArray>();
	if (chips.isNull()) {
		DEBUGLOGSWD("chipCfg_FindById: no 'chips' array in %s\n\r", SWD_CFG_JSON);
		return false;
	}

	for (JsonObject chip : chips) {
		uint32_t chipIdcode = 0;
		if (chip["idcode"].is<const char*>()) {
			chipIdcode = strtoul(chip["idcode"].as<const char*>(), NULL, 0);
		} else {
			chipIdcode = chip["idcode"].as<uint32_t>();
		}

		if (chipIdcode == idcode) {
			cfg.idcode = chipIdcode;
			cfg.name = chip["name"].as<const char*>();
			cfg.family = chip["family"].as<const char*>();
			cfg.flash_size = chip["flash_size"].as<uint32_t>();

			if (chip["flash_start"].is<const char*>()) {
				cfg.flash_start = strtoul(chip["flash_start"].as<const char*>(), NULL, 0);
			} else {
				cfg.flash_start = chip["flash_start"].as<uint32_t>();
			}

			cfg.page_size = chip["page_size"].as<uint32_t>();
			cfg.word_size = chip["word_size"].as<uint32_t>();

			if (chip["csw_value"].is<const char*>()) {
				cfg.csw_value = strtoul(chip["csw_value"].as<const char*>(), NULL, 0);
			} else {
				cfg.csw_value = chip["csw_value"].as<uint32_t>();
			}

			DEBUGLOGSWD("chipCfg_FindById: found %s (family=%s) flash=%u start=0x%08x page=%u word=%u csw=0x%08x\n\r",
				cfg.name.c_str(), cfg.family.c_str(), cfg.flash_size, cfg.flash_start,
				cfg.page_size, cfg.word_size, cfg.csw_value);
			return true;
		}
	}

	DEBUGLOGSWD("chipCfg_FindById: ID=0x%08x not found in %s\n\r", idcode, SWD_CFG_JSON);
	return false;
}

void Class_SubSwd::_chipInfoAppendFields(String &values, JsonObject &chip) {
	values += "chipinfo_idcode|" + String(chip["idcode"].as<const char*>()) + "|div\n";
	values += "chipinfo_family|" + String(chip["family"].as<const char*>()) + "|div\n";
	values += "chipinfo_flash|" + String(chip["flash_size"].as<uint32_t>()) + "|div\n";
	values += "chipinfo_page|" + String(chip["page_size"].as<uint32_t>()) + "|div\n";
}

void Class_SubSwd::registerCustomRoutes() {
}
