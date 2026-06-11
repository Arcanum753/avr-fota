

#ifndef _PROGISP_h
#define _PROGISP_h

#include <Arduino.h>
#include <FS.h>
#include <vector>
#include "format_hex.h"
#include "format_bin.h"


#if defined(ESP32)
#ifndef PIN_MISO
#define PIN_MISO  19   // d19 miso
#endif

#ifndef PIN_MOSI
#define PIN_MOSI  23   // d23 mosi
#endif

#ifndef PIN_SCK
#define PIN_SCK   18   // d18 sck
#endif

#ifndef PIN_RST
#define PIN_RST   5    // d5 rst
#endif

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

#define  FILE_TYPE_COMMA        '.'
#define  FILE_TYPE_HEX          "hex"
#define  FILE_TYPE_BIN          "bin"
#define  FILE_TYPE_BINARY       "binary"

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

// Конечный автомат прошивки AVR (для EERTOS-кооперативной работы)
enum FlashState { FLASH_IDLE = 0, FLASH_INIT, FLASH_WRITE, FLASH_DONE };

// Конечный автомат проверки чипа (для EERTOS-кооперативной работы)
enum ChipCheckState { CHIP_IDLE = 0, CHIP_INIT, CHIP_PROBE, CHIP_DONE };

class ESP_AVRISP {
    friend int hex_write_to_flash_cb(uint32_t chunkAddr, const uint8_t *data, uint32_t size, void *userData);
public:
    ESP_AVRISP(uint8_t reset_pin
    , bool reset_state = false
    , bool reset_activehigh = false);

    void setReset(bool);
#if defined(ESP32)
    void setFs(fs::SPIFFSFS* fs);
#endif
    bool begin ();

    // EERTOS-кооперативная прошивка
    bool startFlash(uint32_t offset, String &path, uint32_t chipMemSize = 0, uint32_t pageSize = 128);
    void flashStep();
    void beginFlashStep();  // регистрация задачи в EERTOS
    bool isFlashBusy() { return _flashState != FLASH_IDLE; }
    bool isFlashError() { return _flashError; }
    uint8_t getPercent() { return _percent; }
    String getFlashErrorString() { return _flashErrorString; }
    String getFlashErrorStage() { return _flashErrorStage; }
    uint8_t getFlashErrorPercent() { return _flashErrorPercent; }
    void updatePercent();   // вычисляет процент и выводит через DEBUGLOGISP
    // Обновляем счётчик записанных байт и процент
    inline void addToFlashPosi(uint32_t size) { _flashPosi += size; }  // добавляет к счётчику записанных байт

    void            chipFusesRead(AVRISP_fuses_t &AVRISP_fuses);
    void            chipFusesWrite( uint8_t _high, uint8_t _low, uint8_t _lock, uint8_t _ext);
    String          avrChipSignGet();
    String          chipSignRead();

    // EERTOS-кооперативная проверка чипа
    void startChipCheck();
    void chipCheckStep();
    bool isChipCheckBusy() { return _chipState != CHIP_IDLE; }
    String getChipCheckResult() { return _chipResultSig; }

protected:

    String          chipNow;
    int             chipErase();
    void            chipBusyWaitPolling();

//state
    int _error = 0;

//fs + hex file
#if defined(ESP32)
    fs::SPIFFSFS*               _fs;
#endif
    int                         hexFileOpen(String _in);
    std::vector<char>           _hexBinDataBuf;
    int                         hexFileBinDataCheck ();
    String                      chipFlashVerification();

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

    // EERTOS state для кооперативной прошивки
    FlashState       _flashState = FLASH_IDLE;
    bool             _flashError = false;  // флаг ошибки при записи страницы
    String           _flashErrorString = "";  // текст ошибки для фронтенда
    String           _flashErrorStage = "";   // стадия на которой произошла ошибка (FLASH_INIT, FLASH_WRITE)
    uint8_t          _flashErrorPercent = 0;  // процент на момент ошибки
    File             _flashFile;
    uint32_t         _flashAddr = 0;
    uint32_t         _flashPosi = 0;
    uint32_t         _flashFileSize = 0;
    uint32_t         _flashStartTime = 0;
    String           _flashPath;
    uint32_t         _chipMemSize = 0;  // размер памяти чипа (из конфига)
    uint32_t         _pageSize = 128;   // размер страницы (из конфига чипа)
    uint32_t         _flashStart = 0;   // стартовый адрес flash (всегда 0 для AVR)
    volatile uint8_t _percent = 0;
    bool             _isHexFormat = false;  // true если прошиваем HEX-файл

    // EERTOS state для кооперативной проверки чипа
    ChipCheckState   _chipState = CHIP_IDLE;
    uint8_t          _chipRetry = 0;
    String           _chipResultSig = "";

};



// EERTOS-враппер для кооперативной прошивки AVR (определён в prog_isp.cpp)
void flash_step_task_wrapper();

// EERTOS-враппер для кооперативной проверки чипа (определён в prog_isp.cpp)
void chip_check_step_task_wrapper();

extern ESP_AVRISP avrprog;




#endif // _PROGISP_h
