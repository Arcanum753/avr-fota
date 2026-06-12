#ifndef _MODULE_PROG_BASE_H
#define _MODULE_PROG_BASE_H

#include "main.h"
#include <ArduinoJson.h>
#include <FS.h>
#include "ESPAsyncWebServer.h"
#include "FSWebServerLib.h"
#include "common.h"
#include "debug.h"
#include "core_ntp/core_ntp.h"
#include "core_json/core_json.h"

#ifdef DEBUG_PROG
#define DEBUGLOGPROG(...)  Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGPROG(...)
#endif

#define CONFIG_PROG_JSON      "/config_prog.json"
#define PROG_FILELIST_JSON    "/prog_filelist.json"

#define  FILE_TYPE_COMMA            '.'
#define  FILE_TYPE_HEX              "hex"
#define  FILE_TYPE_BIN              "bin"
#define  FILE_TYPE_BINARY           "binary"

#define DEFAULT_PROG_PROJNAME       "projname"
#define DEFAULT_CHIP_NAME           ""
#define PROJECT_NAME_MAX_LEN        16

#define CHIP_STATUS_TIMEOUT     300

#define JSON_STR_LEN			512
#define JSON_FILESIZEMAX		1024

#define MAX_FILENAME_LEN		30

typedef enum progerr_e  {
	ERROR_OK = 0,
	ERR_SIGN = -1,
	ERR_BUSY = -2,
	ERR_FLASH = -3,
	ERR_ERASE = -4,
	ERR_HEX = -5,
	ERR_CFG = -6,
	ERR_OPENFILE = -8,
	ERR_INCORRECTFILE = -9,
	ERR_NOFILE = -10,
	ERR_HEXCRC = -11,
	ERR_HEXADDR = -12,
	ERR_HEXMEMOVER = -13,
	ERR_CHIP_OFFLINE = -14,
	ERR_CHIP_MISMATCH = -15,
	ERR_CHIP_NOT_IN_CFG = -16,
} progerr_t;

typedef struct {
    String project_name;
    String chip_name;
} CfgFile_ProgBase_t;

class Class_ProgBase {
public:
	Class_ProgBase(uint8_t in);
	virtual bool begin();
#if defined(ESP32)
	void setFs(fs::SPIFFSFS* fs);
#endif

	// Чисто виртуальные — субмодуль ОБЯЗАН реализовать
	virtual const char* getProgTypePrefix() = 0;
	virtual bool isFlashBusy() = 0;
	virtual uint8_t getFlashPercent() = 0;
	virtual bool chipSpecificInit() = 0;
	virtual void web_FileUpload2Chip(AsyncWebServerRequest *request) = 0;
	virtual void onFlashComplete() = 0;
	virtual const char* getChipCfgJsonPath() = 0;

	// Виртуальные с реализацией по умолчанию
	virtual void registerCustomRoutes() {}
	virtual void web_CheckChipStatus(AsyncWebServerRequest *request);
	virtual bool chip_IsConnected();
	virtual void onChipCheckComplete(const String &) {}

	// cfg
	int			cfg_FileStructGet(CfgFile_ProgBase_t &_inStruct);
	int			cfg_FileSaveFromWeb(CfgFile_ProgBase_t &_inStruct);
	void		cfg_SetDefault();
	bool		cfg_FileLoad();
	bool		cfg_FileSave();
	bool		web_GetFilesListExe(String &str);
	bool		web_GetDiskInfoExe(String &_str);

	// WEB
	void	registerCommonRoutes();
	void	web_Init();

	// Migration from old file paths
	void	_migrateOldFiles();

	// Handlers
	void	web_GetFilesList(AsyncWebServerRequest *request);
	void	web_GetDiskInfo(AsyncWebServerRequest *request);
	void	web_FileDelete(AsyncWebServerRequest *request);
	int		web_FileUpload2FS(String filename, size_t index, uint8_t *data, size_t len, bool final);
	void	web_FileUpload2FS_Status(AsyncWebServerRequest *request);
	void	web_FileUploadProgress(AsyncWebServerRequest *request);
	void	web_FileUploadSize(AsyncWebServerRequest *request);
	void	web_setMD5(AsyncWebServerRequest *request);
	void	setUploadPercent(uint8_t p) { _uploadPercent = p; }

	// filelist management
	bool	filelist_Load(JsonDocument &doc);
	bool	filelist_Save(JsonDocument &doc);
	bool	filelist_AddEntry(const String &filename, const String &upload_date, const String &md5);
	bool	filelist_RemoveEntry(const String &filename);
	bool	filelist_FileExists(const String &filename);
	bool	filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error = "", const String &prog_time = "", const String &prog_error_stage = "", const String &prog_error_percent = "");
	String	filelist_GetLastSuccessFilename();
	String	file_ComputeMD5(const String &path);

	// Project config page (project.html)
	void	web_ProjectInfo(AsyncWebServerRequest *request);
	void	web_ProjectSave(AsyncWebServerRequest *request);
	void	web_ProjectChips(AsyncWebServerRequest *request);
	void	web_ProjectChipInfo(AsyncWebServerRequest *request);
	virtual void _chipInfoAppendFields(String &values, JsonObject &chip);

	// Version — virtual, submodules override
	virtual String getVersionStr() { return String(""); }
	virtual String getGeneratedTime() { return String(""); }
	virtual String getCommitDateStr() { return String(""); }
	void html_ver_get(AsyncWebServerRequest *request);

public:
	String _hexfileProg;
	String _hexfileCheck;
	String _hexFileUploadStatus;

	uint16_t _uploadPercent = 0;
	uint32_t _uploadFileSize = 0;
	volatile bool _progRunning = false;
	int _progResult = -1;
	uint32_t _progStartTime = 0;
	String _flashPath;
	String _flashNtpStr;
#if defined(ESP32)
	fs::SPIFFSFS*   _fs = nullptr;
#endif
	File _fsUploadFile;
	size_t _fileUploadBytes = 0;
	uint32_t _uploadLastChunkTime = 0;

	String _browserFileMD5;
	uint32_t _browserFileSize = 0;
	String _browserFileName;
	bool _fileUploadError = false;
	String _uploadFilename;

	String _chipIdstr;
	uint32_t _chipId = 0;
	bool     _chipConnected = false;
	uint32_t _chipStatusTime = 0;

protected:
	void _cleanupStaleUpload();
	CfgFile_ProgBase_t CfgFile_Prog;
	uint8_t _in;
};

#endif
