#include <vector>
#include "debug.h"


#define CONFIG_PROG_JSON  "/config_prog.json"

#define  FILE_TYPE_COMMA        '.'
#define  FILE_TYPE_HEX          "hex"
#define  FILE_TYPE_BIN          "bin"
#define  FILE_TYPE_BINARY       "binary"

#define DEFAULT_PROG_TYPE           "avr" // тип программатора
#define DEFAULT_PROG_PROJNAME       "projname" // дефолтный имя проекта
#define DEFAULT_chipsize            32768  // размер чипа по дефолту

#define JSON_STR_LEN            512
#define JSON_FILESIZEMAX        1024



typedef enum progerr_e{
    ERROR_OK           =  0     // ошибок нет
   ,ERR_SIGN           = -1     // не совпадает сигнатура чипа
   ,ERR_BUSY           = -2     // программатор занят
   ,ERR_FLASH          = -3     // идёт прошивка
   ,ERR_ERASE          = -4     // идёт стирание
   ,ERR_HEX            = -5     // что-то с хекс файлом
   ,ERR_CFG            = -6     // что-то с конфигфайлом
   ,ERR_RNM            = -7     // TODO вспомнить бы год спустя что это
   ,ERR_OPENFILE       = -8     // файл прошивки не открывается.
   ,ERR_INCORRECTFILE  = -9     // он неправильный
   ,ERR_NOFILE         = -10    //наверное его нет
   ,ERR_HEXCRC         = -11    //что-то с CRC
   ,ERR_HEXMEMOVER     = -12    // FIXME
   ,ERR_HEXADDR        = -13    // FIXME
}progerr_t;


// главная структура настроек программатора.
typedef struct {
    // String programmer_type; // тип программатора.
    String project_name;    //имя проекта.
    uint32_t chip_size;      // размер чипа.
} Prog_CfgFile_t;


class ESP_Programmer {
    public:
    ESP_Programmer( uint8_t in);
    bool begin ();
#if ESP32
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif
private:
    Prog_CfgFile_t _Prog_CfgFile; //  структура конфига
    bool 	load_jsonDoc(const String& file,	JsonDocument& jsonDoc);
    bool 	save_jsonDoc(const JsonDocument& jsonDoc,	const String& file);
    String 	progType;

public:
	    // all about AVR;
    String _hexfileProg;
    String _hexfileCheck;
    String _hexFileUploadStatus;

    void		webInit();
    int			cfg_FileStructGet(Prog_CfgFile_t &_inStruct);
    int			cfg_FileSaveFromWeb(Prog_CfgFile_t &_inStruct);
    void		cfg_SetDefault();
    bool		cfg_FileLoad();
    bool		cfg_FileSave();
    bool		web_GetFileList(String &str);
    bool		web_GetDiskInfo(String &_str);
    void		prog_ProgTypeSet(String _str);
    String		formatBytes(size_t bytes);
    int			prog_Programm(String _in, String _fwTime);

	void avrGetActualFWInfo(AsyncWebServerRequest *request);
    void avrProg(AsyncWebServerRequest *request);
    void avrProgStatus(AsyncWebServerRequest *request) ;
    void avrFusesRead(AsyncWebServerRequest *request) ;
    void avrWebFusesWrite(AsyncWebServerRequest *request) ;

      // all about STM32;
    void programmerGetFilesList (AsyncWebServerRequest *request);
    void programmerGetDiskInfo  (AsyncWebServerRequest *request);
    void programmerFileDelete         (AsyncWebServerRequest *request) ;
    int  programmerFileUpload2FS( String filename, size_t index, uint8_t *data, size_t len, bool final);
    void programmerFileUpload2FSStat(AsyncWebServerRequest *request);
    void programmerFileUpload2Chip(AsyncWebServerRequest *request) ;
	uint8_t hex2bin (uint8_t h) ;
protected:
    uint8_t _in;
    //fs + hex file
#if ESP32
    fs::SPIFFSFS*   _fs;
#elif defined(ESP8266)
    FS* _fs;    // esp8266/esp32 flash file system
#endif

};

extern ESP_Programmer espProgrammer;