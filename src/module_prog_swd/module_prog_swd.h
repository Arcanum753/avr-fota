#ifndef _MODULEPROGSWD_h
#define _MODULEPROGSWD_h


#include "main.h"

#ifdef DEBUG_SWD
#define DEBUGLOGSWD(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUGLOGSWD(...)
#endif


#define CONFIG_PROG_JSON  "/config_prog_swd.json"

#define  FILE_TYPE_COMMA            '.'
#define  FILE_TYPE_HEX              "hex"
#define  FILE_TYPE_BIN              "bin"
#define  FILE_TYPE_BINARY           "binary"

#define DEFAULT_PROG_PROJNAME       "projname" // дефолтное имя проекта
#define DEFAULT_chipsize            32768  // размер чипа по дефолту

#define JSON_STR_LEN			512
#define JSON_FILESIZEMAX		1024

// Maximum filename length for upload (30 chars + null terminator = 31 bytes).
// SPIFFS on ESP8266/ESP32 has a 32-byte limit for filenames (including path separator '/' and null terminator).
// We use 30 to leave room for the '/' prefix added by the server.
// Максимальная длина имени файла для загрузки (30 символов + нуль-терминатор = 31 байт).
// SPIFFS на ESP8266/ESP32 имеет ограничение 32 байта на имя файла (включая разделитель '/' и нуль-терминатор).
// Используем 30, чтобы оставить место для префикса '/', добавляемого сервером.
#define MAX_FILENAME_LEN		30


// TODO навести тут порядок с кодами ошибок
typedef enum progerr_e  {
	ERROR_OK = 0 // ошибок нет
	,ERR_SIGN = -1 // не совпадает сигнатура чипа
	,ERR_BUSY = -2 // программатор занят
	,ERR_FLASH = -3 // идёт прошивка
	,ERR_ERASE = -4 // идёт стирание
	,ERR_HEX = -5 // что-то с хекс файлом
	,ERR_CFG = -6 // что-то с конфигфайлом
	,ERR_RNM = -7 // TODO вспомнить бы год спустя что это
	,ERR_OPENFILE = -8 // файл прошивки не открывается.
	,ERR_INCORRECTFILE = -9 // он неправильный
	,ERR_NOFILE = -10 // наверное его нет
	,ERR_HEXCRC = -11 // что-то с CRC
	,ERR_HEXMEMOVER = -12 // FIXME
	,ERR_HEXADDR = -13 // FIXME
} progerr_t;

// главная структура настроек программатора.
typedef struct {
    String project_name;	//имя проекта.
    uint32_t chip_size;		// размер чипа.
    String last_prog_file;	// имя последнего прошитого файла
    String last_prog_date;	// дата последней прошивки
} CfgFile_ProgSwd_t;


class Class_ProgSwd {
public:
	Class_ProgSwd( uint8_t in);
    bool begin ();
#if defined(ESP32)
    void setFs(fs::SPIFFSFS* fs);
#endif
#if defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
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
    // programming
    void    web_FileUpload2Chip(AsyncWebServerRequest *request) ;

private:
    String getVersionStr();
    String getGeneratedTime();
    String getCommitDateStr();
    void  html_ver_get(AsyncWebServerRequest *request);
protected:
    uint8_t _in;
    //fs + hex file
#if defined(ESP32)
    fs::SPIFFSFS*   _fs;
#endif
#if defined(ESP8266)
    FS* _fs;    // esp8266/esp32 flash file system
#endif

};

extern Class_ProgSwd progSwd;


#endif //_MODULEPROGSWD_h
