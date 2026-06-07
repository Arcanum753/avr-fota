
#ifndef _MODULEPROGISP_h
#define _MODULEPROGISP_h

#include "main.h"
#include <ArduinoJson.h>


#ifdef DEBUG_ISP
#define DEBUGLOGISP(...)  Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGISP(...)
#endif


#define CONFIG_PROG_JSON  "/config_prog_isp.json"
#define AVRISP_CFG_JSON   "/avrisp_cfg.json"

#define  FILE_TYPE_COMMA            '.'
#define  FILE_TYPE_HEX              "hex"
#define  FILE_TYPE_BIN              "bin"
#define  FILE_TYPE_BINARY           "binary"

#define DEFAULT_PROG_PROJNAME       "projname" // дефолтное имя проекта
#define DEFAULT_chipsize            32768  // размер чипа по дефолту

#define ISP_FILELIST_JSON       "/isp_filelist.json"

#define JSON_STR_LEN			512
#define JSON_FILESIZEMAX		1024

// Maximum filename length for upload (30 chars + null terminator = 31 bytes).
// SPIFFS on ESP32 has a 32-byte limit for filenames (including path separator '/' and null terminator).
// We use 30 to leave room for the '/' prefix added by the server.
#define MAX_FILENAME_LEN		30


// Коды ошибок программатора ISP
typedef enum progerr_e  {
	ERROR_OK = 0,           // Ошибок нет
	ERR_SIGN = -1,          // Не совпадает сигнатура чипа
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
} CfgFile_ProgIsp_t;

// Структура конфигурации AVR-чипа из avrisp_cfg.json
typedef struct {
    String   signature;      // Сигнатура чипа (например, "0x1E950F" для ATmega328P)
    String   name;           // Название чипа (например, "ATmega328P")
    uint32_t flash_size;     // Размер flash в байтах
    uint32_t page_size;      // Размер страницы в байтах
} ChipConfigAvr_t;


class Class_ProgIsp {
public:
	Class_ProgIsp( uint8_t in);
    bool begin ();
#if defined(ESP32)
    void setFs(fs::SPIFFSFS* fs);
#endif
private:
    CfgFile_ProgIsp_t CfgFile_ProgIsp; //  структура конфига


public:
    String _hexfileProg;
    String _hexfileCheck;
    String _hexFileUploadStatus;

    // cfg
    int			cfg_FileStructGet(CfgFile_ProgIsp_t &_inStruct);
    int			cfg_FileSaveFromWeb(CfgFile_ProgIsp_t &_inStruct);
    void		cfg_SetDefault();
    bool		cfg_FileLoad();
    bool		cfg_FileSave();
    bool		web_GetFilesListExe(String &str);
    bool		web_GetDiskInfoExe(String &_str);
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
    bool    filelist_SetProgStatus(const String &filename, const String &prog_date, const String &prog_status, const String &prog_error = "");
    // find the last successfully programmed filename (newest prog_date with "ok" status)
    String  filelist_GetLastSuccessFilename();
    // compute md5 for an existing file
    String  file_ComputeMD5(const String &path);
    // programming
    void    web_FileUpload2Chip(AsyncWebServerRequest *request) ;
    // Callback после завершения EERTOS-кооперативной прошивки
    void    onFlashComplete();
    // Chip config from avrisp_cfg.json
    bool    chipCfg_Load();
    bool    chipCfg_FindBySignature(const String &signature, ChipConfigAvr_t &cfg);

    // AVR config page (avrcfg.html)
    void    web_AvrCfgInfo(AsyncWebServerRequest *request);
    void    web_AvrCfgSave(AsyncWebServerRequest *request);
    void    web_AvrCfgReadSignature(AsyncWebServerRequest *request);

    // AVR-specific (fuses)
    void    avrFusesRead(AsyncWebServerRequest *request);
    void    avrWebFusesWrite(AsyncWebServerRequest *request);

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
protected:
    uint8_t _in;
    uint16_t _uploadPercent = 0;
    uint32_t _uploadFileSize = 0;
    // Состояние программирования AVR
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

    // Chip status (ISP connection)
    String _chipIdstr;
};

extern Class_ProgIsp progIsp;




#endif //_MODULEPROGISP_h
