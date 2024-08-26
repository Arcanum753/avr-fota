
#include <vector>

#define PIN_MISO  12   // d6 miso
#define PIN_MOSI  13   // d7 mosi
#define PIN_SCK   14   // d5 sck
#define PIN_RST   5    // rst


#define DEBUG_SHOWHEXBUF 2    // 0 disable showing debug // 1 show logic debug // 2 - show all bufs

#define DBG_ISP_OUTPUT_PORT Serial

// #ifndef RELEAS_AVRISP
#if (DEBUG_SHOWHEXBUF > 1)
#define DEBUGLOGISP(...) DBG_ISP_OUTPUT_PORT.printf(__VA_ARGS__)

#else
#define DEBUGLOGISP(...)
#endif


// #ifndef RELEAS_AVRISP
#if (DEBUG_SHOWHEXBUF > 2)
#define DEBUGLOGISPBUF(...) DBG_ISP_OUTPUT_PORT.printf(__VA_ARGS__)

#else
#define DEBUGLOGISPBUF(...)
#endif

#define JSON_STR_LEN    512
#define JSON_FILESIZEMAX 1024

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

#define DEFAULT_PROG_JSON  "/avrisp.json"

#define HEX_PARSE_STRNUM      4             // number of strings with params
#define HEX_PARSE_METALINEBEGIN       '$'    //spec symb for delimiter   
#define HEX_PARSE_WORD_SIGN  "sign"
#define HEX_PARSE_WORD_PROJ  "proj"
#define HEX_PARSE_WORD_VER   "vers"
#define HEX_PARSE_WORD_DATE  "date"
#define HEX_PARSE_WORD_TIME  "time"

#define HEX_PARSE_LINEBEGIN      ':'


#define HEX_PARSE_CHAR_SPACE       ' '    //spec symb for delimiter   

// atmega 328p and all about firmwares hex files
#define DEFAULT_HEXFILENAME     ""
#define DEFAULT_HEXFILENAMEOLD  ""
#define DEFAULT_VER             "0.01"
#define DEFAULT_VEROLD          "0.00"

#define DEFAULT_AVR_SIGN        "1e950f" 
#define DEFAULT_AVR_MCU         "m328p" 

#define DEFAULT_chipsize     32768

#define DEFAULT_OLDHEXFILENAMEPREFIX    "old_"
#define DEFAULT_OLDHEXFILENAMEEND       ".hex"

#define DEFAULT_TEMPDIRNAME        "temp"
// uncomment if you use an n-mos to level-shift the reset line
// #define AVRISP_ACTIVE_HIGH_RESET

// SPI clock frequency in Hz
#define AVRISP_SPI_FREQLOW    100000
#define AVRISP_SPI_FREQHIGH   500000
#define AVRISP_SPI_DELAY      90000000     // delay counter max for spi polling func

//mem page size for isp prog
#define MEM_PAGE_SIZE       128



typedef struct {
 uint8_t high;
 uint8_t low;
 uint8_t lock;
 uint8_t ext;
} AVRISP_fuses_t;

typedef enum avrsip_err_e{
     ERROR_OK           =  0
    ,ERR_SIGN           = -1
    ,ERR_BUSY           = -2
    ,ERR_FLASH          = -3
    ,ERR_ERASE          = -4
    ,ERR_HEX            = -5
    ,ERR_CFG            = -6
    ,ERR_RNM            = -7
    ,ERR_OPENFILE       = -8
    ,ERR_INCORRECTFILE  = -9
    ,ERR_NOFILE         = -10
    ,ERR_HEXCRC         = -11
    ,ERR_HEXMEMOVER     = -12
    ,ERR_HEXADDR        = -13
}avrsip_err_t;

typedef struct {
    String hex_filename;
    String signture;
    String project_name;
    String version;
    uint32_t size;
    // String buildDate;
    String buildtime;
    bool cmpsign;
    bool cmpproj;
} AVRISP_HexFileUploaded_t;

typedef struct {
    String hex_filename;
    String hex_filename_old;
    String hex_version;
    String hex_version_old;
    String hex_buildtime;
    String hex_buildtime_old;

    String fwTS;
    String avr_signature;
    String project_name;
    uint32_t chipsize;
    uint32_t pagesize;

} AVRISP_CfgFile_t;

class ESP8266_AVRISP {
public:
    ESP8266_AVRISP(uint8_t reset_pin
    , bool reset_state=false
    , bool reset_activehigh=false);

    void setReset(bool);
    void setFs (FS* fs);
    bool begin ();
        
    avrsip_err_t    avrChipProgrammDBG(String _in);
    avrsip_err_t    avrChipProgrammMain(String _in, String _fwTime);
    
    avrsip_err_t    hexFileMetaStructGet(String _in, AVRISP_HexFileUploaded_t &_inStruct)   ;
    int32_t         hexFileUploadedBodyCheck(String _in);
    avrsip_err_t    cfgFileStructGet(AVRISP_CfgFile_t &_inStruct) ;
    void            cfgFileLoadWeb(AVRISP_CfgFile_t &_inStruct);
    String          chipFlashVerificationResultGet();
    String          fsDirListGet(); //DEBUG

    void            chipFusesRead(AVRISP_fuses_t &AVRISP_fuses);
    void            chipFusesWrite( uint8_t _high, uint8_t _low, uint8_t _lock, uint8_t _ext);
    AVRISP_CfgFile_t _AVRISP_CfgFile;

protected:    
    String          chipSignRead();
    
    avrsip_err_t    chipErase();
    void            chipBusyWaitPolling();
   
//all about json & configs
    
    bool            cfgFileLoad();
    bool            cfgFileSave();
    void            cfg_setDefault();
    avrsip_err_t    cfgFileSetNowAsOld();
    avrsip_err_t    cfgFilSetUploadeAsNow( String _fwTime);
//state
    int _error = 0;

//fs + hex file
    FS* _fs; // esp8266 flash file system
    avrsip_err_t                hexFileOpen(String _in);
    std::vector<char>           _hexFileBuf;
    std::vector<char>           _hexFileBinDataBuf;
    AVRISP_HexFileUploaded_t    AVRISP_HexFileUploaded;
    int32_t                     hexFileBinDataCheck ();
    int32_t                     hexFileLineParser (uint32_t begin, uint16_t & pageaddr, byte *page,  uint8_t  &chsum, uint8_t &type, uint8_t &binReadNum , uint32_t &totalBins ) ;
    uint8_t                     hex2bin (uint8_t h);
    avrsip_err_t                hexFileHeadCheck();
    String                      chipFlashVerification();      
    String                      verificationResult ;

// avr chip spi + rst
    void pmode_begin();     // enter program mode
    void pmode_end();       // exit program mode
    
    avrsip_err_t    hexFile2flashByPages();
    avrsip_err_t    chipFlashPage (byte *pagebuff, uint16_t pageaddr, uint8_t pagesize) ;
    void            chipFlashWord (uint8_t hilo, uint16_t addr, uint8_t data) ;
    uint16_t        chipSpiTransaction(uint8_t, uint8_t, uint8_t, uint8_t);

    uint8_t _reset_pin = PIN_RST;
    bool _reset_state;
    bool _reset_activehigh;
    inline bool _resetLevel(bool reset_state) { return reset_state == _reset_activehigh; }

    void dbgPrintVector();
    
};


extern ESP8266_AVRISP avrprog;







