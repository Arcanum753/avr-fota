#ifndef _SUBMODULE_SWD_H
#define _SUBMODULE_SWD_H

#include "../module_prog/module_prog.h"
#include "prog_swd.h"
#include "submodule_swd_version.h"

#ifdef DEBUG_SWD
#define DEBUGLOGSWD(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGSWD(...)
#endif

#define SWD_CFG_JSON      "/swd_cfg.json"

#define DEFAULT_FLASH_START_ADDR    0x08000000
#define DEFAULT_PAGE_SIZE           1024
#define DEFAULT_WORD_SIZE           2
#define DEFAULT_CSW_VALUE           0xa2000002

typedef struct {
    uint32_t idcode;
    String   name;
    String   family;
    uint32_t flash_size;
    uint32_t flash_start;
    uint32_t page_size;
    uint32_t word_size;
    uint32_t csw_value;
} ChipConfig_t;

class Class_SubSwd : public Class_ProgBase {
public:
	Class_SubSwd(uint8_t in);

	const char* getProgTypePrefix() override { return "swd"; }
	const char* getChipCfgJsonPath() override { return SWD_CFG_JSON; }
	bool isFlashBusy() override { return swdprog.isFlashBusy(); }
	uint8_t getFlashPercent() override { return swdprog.getPercent(); }
	bool chipSpecificInit() override;
	void web_FileUpload2Chip(AsyncWebServerRequest *request) override;
	void onFlashComplete() override;
	void registerCustomRoutes() override;

	String getVersionStr() override { return String(SUBMODULE_SWD_VERSION_STR); }
	String getGeneratedTime() override { return String(SUBMODULE_SWD_GENERATED_TIME); }
	String getCommitDateStr() override { return String(SUBMODULE_SWD_COMMIT_DATE_STR); }

	// SWD-specific
	void _chipInfoAppendFields(String &values, JsonObject &chip) override;
	void onChipCheckComplete(uint32_t chipId);
	void web_CheckChipStatus(AsyncWebServerRequest *request) override;
	bool chip_IsConnected() override;
	bool chipCfg_FindById(uint32_t idcode, ChipConfig_t &cfg);
	int  prog_Programm(String path, String fwTime);
};

extern Class_SubSwd progSwd;

#endif
