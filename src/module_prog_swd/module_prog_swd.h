#ifndef _MODULEPROGSWD_h
#define _MODULEPROGSWD_h


#include "main.h"
#include <ArduinoJson.h>
#include <FS.h>
#include "ESPAsyncWebServer.h"

#ifdef DEBUG_SWD
#define DEBUGLOGSWD(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGSWD(...)
#endif


#define CONFIG_PROG_JSON  "/config_prog_swd.json"
#define SWD_CFG_JSON      "/swd_cfg.json"

#define  FILE_TYPE_COMMA            '.'
#define  FILE_TYPE_HEX              "hex"
#define  FILE_TYPE_BIN              "bin"
#define  FILE_TYPE_BINARY           "binary"

#define DEFAULT_PROG_PROJNAME       "projname" // дефолтное имя проекта
#define DEFAULT_chipsize            32768  // размер чипа по дефолту

// Fallback-константы для параметров прошивки (когда нет swd_cfg.json или чип не найден)
#define DEFAULT_FLASH_START_ADDR    0x08000000
#define DEFAULT_PAGE_SIZE           1024
#define DEFAULT_WORD_SIZE           2
#define DEFAULT_CSW_VALUE           0xa2000002

#define SWD_FILELIST_JSON       "/swd_filelist.json"

// Время актуальности статуса чипа после проверки (5 минут = 300 секунд)
#define CHIP_STATUS_TIMEOUT     300

#define JSON_STR_LEN			512
#define JSON_FILESIZEMAX		1024

// Maximum filename length for upload (30 chars + null terminator = 31 bytes).
// SPIFFS on ESP32 has a 32-byte limit for filenames (including path separator '/' and null terminator).
// We use 30 to leave room for the '/' prefix added by the server.
#define MAX_FILENAME_LEN		30


// Коды ошибок программатора SWD
typedef enum progerr_e  {
	ERROR_OK = 0,           // Ошибок нет
	ERR_SIGN = -1,          // Не совпадает сигнатура чипа (IDCODE)
	ERR_BUSY = -2,          // Программатор занят
	ERR_FLASH = -3,         // Идёт прошивка
	ERR_ERASE = -4,         // Идёт стирание
	ERR_HEX = -5,           // Ошибка в hex-файле
	ERR_CFG = -6,           // Ошибка конфигурационного файла
	ERR_OPENFILE = -8,      // Файл прошивки не открывается
	ERR_INCORRECTFILE = -9, // Неверный формат файла
	ERR_NOFILE = -10,       // Файл не найден
	ERR_HEXCRC = -11,       // Ошибка CRC в hex-файле
	ERR_HEXADDR = -12,      // Нарушение монотонности адресов в HEX-файле
	ERR_HEXMEMOVER = -13,   // Превышение размера памяти чипа
} progerr_t;


// главная структура настроек программатора.
typedef struct {
    String project_name;	//имя проекта.
    uint32_t chip_size;		// размер чипа.
} CfgFile_ProgSwd_t;

// Структура конфигурации чипа из swd_cfg.json
typedef struct {
    uint32_t idcode;        // IDCODE чипа (например, 0x2ba01477 для STM32F103)
    String   name;          // Название чипа (например, "STM32F103C8")
    String   family;        // Семейство (например, "stm32f1")
    uint32_t flash_size;    // Размер flash в байтах
    uint32_t flash_start;   // Стартовый адрес flash
    uint32_t page_size;     // Размер страницы в байтах
    uint32_t word_size;     // Размер слова в байтах
    uint32_t csw_value;     // Значение CSW для SWD-доступа
} ChipConfig_t;


class Class_ProgSwd {
public:
	Class_ProgSwd( uint8_t in);
    bool begin ();
#if defined(ESP32)
    void setFs(fs::SPIFFSFS* fs);
#endif
private:
    CfgFile_ProgSwd_t CfgFile_ProgSwd; //  структура конфига


public:
    String _hexfileProg;
    String _hexfileCheck;
    String _hexFileUploadStatus;

    // cfg
    int			cfg_FileStructGet(CfgFile_ProgSwd_t &_inStruct);
    int			cfg_FileSaveFromWeb(CfgFile_ProgSwd_t &_inStruct);
    void		cfg_SetDefault();
    bool		cfg_FileLoad();
    bool		cfg_FileSave();
    bool		web_GetFilesListExe(String &str);
    bool		web_GetDiskInfoExe(String &_str);
    int			prog_Programm(String _in, String _fwTime);


    // all about WEB page;
    void    web_Init();
    void    web_GetFilesList (AsyncWebServerRequest *request);
    void    web_GetDiskInfoExe  (AsyncWebServerRequest *request);
    void    web_FileDelete         (AsyncWebServerRequest *request) ;
    int     web_FileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final);
    void    web_FileUpload2FS_Status(AsyncWebServerRequest *request);
    void    web_FileUploadProgress(AsyncWebServerRequest *request);
    void    web_FileUploadSize(AsyncWebServerRequest *request);
    void    web_setMD5(AsyncWebServerRequest *request);
    void    setUploadPercent(uint8_t p) { _uploadPercent = p; }
    // filelist management
    bool    filelist_Load(JsonDocument &doc);
    bool    filelist_Save(JsonDocument &doc);
    bool    filelist_AddEntry(const String &filename, const String &upload_date, const String &md5);
    bool    filelist_RemoveEntry(const String &filename);
    // check if filename exists in FS filelist
    bool    filelist_FileExists(const String &filename);
    // set prog_date and prog_status after programming attempt
    bool    filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error = "", const String &prog_time = "", const String &prog_error_stage = "", const String &prog_error_percent = "");
    // find the last successfully programmed filename (newest prog_date with "ok" status)
    String  filelist_GetLastSuccessFilename();
    // compute md5 for an existing file
    String  file_ComputeMD5(const String &path);
    // programming
    void    web_FileUpload2Chip(AsyncWebServerRequest *request) ;
    // Callback после завершения EERTOS-кооперативной прошивки
    void    onFlashComplete();
    // chip status check
    void    web_CheckChipStatus(AsyncWebServerRequest *request);
    bool    chip_IsConnected();
    // Callback после завершения EERTOS-кооперативной проверки чипа
    void    onChipCheckComplete(uint32_t chipId);

    // Chip config from swd_cfg.json
    bool    chipCfg_Load();
    bool    chipCfg_FindById(uint32_t idcode, ChipConfig_t &cfg);

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
protected:
    uint8_t _in;
    uint16_t _uploadPercent = 0;
    uint32_t _uploadFileSize = 0;
    // Состояние программирования STM32
    volatile bool _progRunning = false;
    int _progResult = -1;
    uint32_t _progStartTime = 0;
    String _flashPath;      // путь к файлу прошивки (сохраняем между вызовами EERTOS)
    String _flashNtpStr;    // строка времени (сохраняем между вызовами EERTOS)
    //fs + hex file
#if defined(ESP32)
    fs::SPIFFSFS*   _fs;
#endif
    File _fsUploadFile;        // открытый файл при загрузке в ФС
    size_t _fileUploadBytes;   // счётчик записанных байт при загрузке
    
    // MD5 verification
    String _browserFileMD5;    // MD5 переданный от браузера
    uint32_t _browserFileSize; // размер файла от браузера
    String _browserFileName;   // имя файла от браузера
    bool _fileUploadError;     // флаг ошибки загрузки (несовпадение MD5)

    // Chip status (SWD connection)
    uint32_t _chipId = 0;
    bool     _chipConnected = false;
    uint32_t _chipStatusTime = 0;  // millis() последней проверки статуса
};

extern Class_ProgSwd progSwd;


#endif //_MODULEPROGSWD_h
