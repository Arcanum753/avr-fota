
#include <vector>


#if defined(ESP32)
#define PIN_MISO  19   // d19 miso
#define PIN_MOSI  23   // d23 mosi
#define PIN_SCK   18   // d18 sck
#define PIN_RST   5    // d5 rst
#elif defined(ESP8266)
#define PIN_MISO  12   // d6 miso
#define PIN_MOSI  13   // d7 mosi
#define PIN_SCK   14   // d5 sck
#define PIN_RST   5    // d1 rst
#endif

#define JSON_STR_LEN    512
#define JSON_FILESIZEMAX 1024

#define DEFAULT_AVR_CHIPSIZE           32768  // размер чипа

// STK cmd's
#define STK_PROGMODE        0xAC, 0x53, 0x00, 0x00
#define STK_READSIGN1       0x30, 0x00, 0x00, 0x00
#define STK_READSIGN2       0x30, 0x00, 0x01, 0x00
#define STK_READSIGN3       0x30, 0x00, 0x02, 0x00


#define STK_POLLCHIP        0xF0, 0x00, 0x00, 0x00
#define STK_ERASECHIP       0xAC, 0x80, 0x00, 0x00	// chip erase  // ALARM!

#define STK_VERIFYADDRHIGH 0x28
#define STK_VERIFYADDRLOW  0x20

//read fuses
#define STK_CHIPFUSELOCK    0x58, 0x00, 0x00, 0x00 	/* memory protection read*/
#define STK_CHIPFUSELOW     0x50, 0x00, 0x00, 0x00	/* Low fuse read*/
#define STK_CHIPFUSEHIGH    0x58, 0x08, 0x00, 0x00	/* High fuse read*/
#define STK_CHIPFUSEEXT     0x50, 0x08, 0x00, 0x00	/* Extended fuse read*/


//fuses write
#define STK_CHIPFUSELOCK_WR 0xAC, 0xE0, 0x00         /* memory protection read*/
#define STK_CHIPFUSELOW_WR  0xAC, 0xA0, 0x00         /* Low fuse read*/
#define STK_CHIPFUSEHIGH_WR 0xAC, 0xA8, 0x00         /* High fuse read*/
#define STK_CHIPFUSEEXT_WR  0xAC, 0xA4, 0x00         /* Extended fuse read*/

#define CONFIG_AVRPROG_JSON  "/config_avr.json"

#define HEX_PARSE_METALINEBEGIN       '$'    //spec symb for delimiter
#define HEX_PARSE_WORD_SIGN  "sign"
#define HEX_PARSE_WORD_PROJ  "proj"
#define HEX_PARSE_WORD_VER   "vers"
#define HEX_PARSE_WORD_DATE  "date"
#define HEX_PARSE_WORD_TIME  "time"

#define HEX_PARSE_LINEBEGIN      ':'


#define HEX_PARSE_CHAR_SPACE       ' '    //spec symb for delimiter

#define  FILE_TYPE_COMMA        '.'
#define  FILE_TYPE_HEX          "hex"
#define  FILE_TYPE_BIN          "bin"
#define  FILE_TYPE_BINARY       "binary"

// atmega 328p and all about firmwares hex files
#define DEFAULT_HEXFILENAME     ""
#define DEFAULT_VER             "0.01"


#define DEFAULT_AVR_SIGN        "1e950f"
#define DEFAULT_AVR_MCU         "m328p"




#define DEFAULT_TEMPDIRNAME        "temp"
// uncomment if you use an n-mos to level-shift the reset line
// #define AVRISP_ACTIVE_HIGH_RESET

// SPI clock frequency in Hz
#define AVRISP_SPI_FREQLOW    100000
#define AVRISP_SPI_FREQHIGH   500000
#define AVRISP_SPI_DELAY      90000000     // delay counter max for spi polling func

//mem page size for isp prog
#define MEM_PAGE_SIZE       128


// структура для фьюз битов
typedef struct {
 uint8_t high;
 uint8_t low;
 uint8_t lock;
 uint8_t ext;
} AVRISP_fuses_t;

// структура для мета информации загруженного hex файла
typedef struct {
    String hex_filename;
    String signture;
    String project_name;
    String version;
    uint32_t size;
    String buildtime;
    bool cmpsign;
    bool cmpproj;
} AVRISP_HexFileUploaded_t;

// актуальное состояние
typedef struct {
    String hex_filename;
    String hex_version;
    String hex_buildtime;
    String fwTS;
    uint32_t pagesize;
    // uint32_t chipsize;
} AVRISP_CfgFile_t;

class ESP8266_AVRISP {
public:
    ESP8266_AVRISP( uint8_t reset_pin
    , bool reset_state = false
    , bool reset_activehigh = false);
    void setReset(bool);
#if ESP32
    void setFs(fs::SPIFFSFS* fs);
#elif defined(ESP8266)
    void setFs(FS* fs)  ;                       // esp8266/esp32 flash file system
#endif

    bool begin ();

    int             avrChipProgrammDBG(String _in); // отладка для консоли
    int             avr_ChipProgrammMain(String _in, String _fwTime); // основной "сценарий" программирования из веба

    int32_t         hexFileUploadedBodyCheck(String _in);
    int             cfgFileStructGet(AVRISP_CfgFile_t &_inStruct) ;
    void            cfgFileLoadWeb(AVRISP_CfgFile_t &_inStruct);
    String          chipFlashVerificationResultGet();


    void            chipFusesRead(AVRISP_fuses_t &AVRISP_fuses);
    void            chipFusesWrite( uint8_t _high, uint8_t _low, uint8_t _lock, uint8_t _ext);
    String          avrChipSignGet();
    String          chipSignRead();
    AVRISP_CfgFile_t _AVRISP_CfgFile;

protected:
    String          chipNow;
    int             chipErase();
    void            chipBusyWaitPolling();

//all about json & configs

    bool            cfgFileLoad();
    bool            cfgFileSave();
    void            cfg_setDefault();
    int             cfgFilSetUploadeAsNow( String _fwTime);
//state
    int _error = 0;

//fs + hex file
#if ESP32
    fs::SPIFFSFS*               _fs;
#elif defined(ESP8266)
    FS*                         _fs;                        // esp8266/esp32 flash file system
#endif
    int                         hexFileOpen(String _in);
    std::vector<char>           _hexFileBuf;
    std::vector<char>           _hexFileBinDataBuf;
    AVRISP_HexFileUploaded_t    AVRISP_HexFileUploaded;
    int                         hexFileBinDataCheck ();
    int32_t                     hexFileLineParser (uint32_t begin, uint16_t & pageaddr, byte *page,  uint8_t  &chsum, uint8_t &type, uint8_t &binReadNum , uint32_t &totalBins ) ;
    uint8_t                     hex2bin (uint8_t h);
    String                      chipFlashVerification();
    String                      verificationResult ;

// avr chip spi + rst
    void pmode_begin();     // enter program mode
    void pmode_end();       // exit program mode

    int             hexFile2flashByPages();
    int             chipFlashPage (byte *pagebuff, uint16_t pageaddr, uint8_t pagesize) ;
    void            chipFlashWord (uint8_t hilo, uint16_t addr, uint8_t data) ;
    uint16_t        chipSpiTransaction(uint8_t, uint8_t, uint8_t, uint8_t);

    uint8_t _reset_pin = PIN_RST;
    bool _reset_state;
    bool _reset_activehigh;
    inline bool _resetLevel(bool reset_state) { return reset_state == _reset_activehigh; }

    void dbgPrintVector(); //FIXME debug

};


extern ESP8266_AVRISP avrprog;







