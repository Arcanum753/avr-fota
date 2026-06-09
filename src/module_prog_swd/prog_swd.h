
#ifndef _PROGSWD_h
#define _PROGSWD_h

#include <Arduino.h>
#include <FS.h>
#include <vector>


#include "swd.h"
#include "stm32f1_flash.h"
#include "stm32f4_flash.h"



// Размер буфера для записи в flash (в байтах).
// Должен быть достаточно мал для безопасного размещения на стеке EERTOS-задачи,
// но достаточно велик для эффективной записи.
// 256 байт — оптимальный баланс.
#define WRITE_BUF_SIZE  256

// AD AD
#define AP_TIMES  50

// TODO table of ID's
#define SWD_STM32F103ID          0x2ba01477

//stm32 Fxxx
#define SWD_UNHALT               0xA05F0000
#define SWD_HALT                 0xA05F0003
#define SWD_RST                  0x05FA0004


// STM32 auto map 0x00 to 0x08000000, use 0 for simplicity
#define FLASH_START_ADDR    0x08000000

#define DHCSR 0xe000edf0
#define DEMCR 0xe000edfc
#define AIRCR 0xe000ed0c


#define AP_NRF_RESET             0x00
#define AP_NRF_ERASEALL          0x04
#define AP_NRF_ERASEALLSTATUS    0x08
#define AP_NRF_APPROTECTSTATUS   0x0c`
#define AP_NRF_IDR               0xfc

#define AP_CSW                   0x00
#define AP_TAR                   0x04
#define AP_DRW                   0x0c
#define AP_HLT                   0x0F
#define AP_BD0                   0x10
#define AP_BD1                   0x14
#define AP_BD2                   0x18
#define AP_BD3                   0x1c
#define AP_DBGDRAR               0xf8
#define AP_IDR                   0xfc

#define ADIV5_APnDP     0x1000U
#define ADIV5_AP_REG(x) (ADIV5_APnDP | (x))
#define ADIV5_AP_TAR_HIGH ADIV5_AP_REG(0xd08U)



// Конечный автомат прошивки STM32 (для EERTOS-кооперативной работы)
enum FlashState { FLASH_IDLE = 0, FLASH_INIT, FLASH_WRITE, FLASH_DONE };

// Конечный автомат проверки чипа (для EERTOS-кооперативной работы)
enum ChipCheckState { CHIP_IDLE = 0, CHIP_INIT, CHIP_PROBE, CHIP_DONE };

class ESP_PROGSWD {
public:
    ESP_PROGSWD();
#if defined(ESP32)
    void setFs(fs::SPIFFSFS* fs);
#endif

    uint32_t stm32Fx_begin();

    // Блокирующая прошивка (старый метод — для совместимости)
    int stm32_ChipProgrammMain( String &path);
    uint8_t stm32_flash_file(uint32_t offset, String &path);

    // Установка семейства чипа для выбора алгоритма прошивки
    void setChipFamily(const String &family) { _chipFamily = family; }
    const String& getChipFamily() const { return _chipFamily; }

    // EERTOS-кооперативная прошивка
    bool startFlash(uint32_t offset, String &path, uint32_t chipMemSize = 0,
                    uint32_t pageSize = 1024, uint32_t wordSize = 2, uint32_t cswValue = 0xa2000002);

    void flashStep();
    void beginFlashStep();  // регистрация задачи в EERTOS
    bool isFlashBusy() { return _flashState != FLASH_IDLE; }
    bool isFlashError() { return _flashError; }
    uint8_t getPercent() { return _percent; }
    String getFlashErrorString() { return _flashErrorString; }

    // EERTOS-кооперативная проверка чипа
    void startChipCheck();
    void chipCheckStep();
    bool isChipCheckBusy() { return _chipState != CHIP_IDLE; }
    uint32_t getChipCheckResult() { return _chipResultId; }

    void stm32Fx_abort_all();
    void stm32Fx_rst ();
    void stm32Fx_halt();
    void stm32Fx_unhalt ();

    /*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/
    uint32_t stm32f_read_register(uint32_t address, bool muted = 1);
    void stm32Fx_write_register(uint32_t address, uint32_t value, bool muted = false);
    void stm32Fx_write_port(bool APorDP, uint8_t address, uint32_t value, bool muted = 1);
    uint8_t stm32fX_write_bank(uint32_t addr, uint8_t buffer[], uint32_t size) ;
    bool stm32Fx_write_flash_32bit(uint32_t address, uint32_t value, bool muted = 1) ;
    bool stm32Fx_write_flash_16bit(uint32_t address, uint32_t value, bool muted = 1);

    /*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/
    bool stm32f4_flash_busy(void);
    bool stm32f4_wait_busy(uint32_t timeout_ms = 5000);
    void stm32f4_flash_unlock_dap() ;
    void stm32f4_erase_flash_dap();
    void stm32f4_prog_enable();
    void stm32f4_prog_disable();
    void stm32f4_erase_sector(uint8_t sector_num);
    void stm32f4_mass_erase();
    /*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/

    void stm32f1_progEn (void) ;
    void stm32f1_progOff (void) ;
    void stm32f1_clear_eop(uint32_t bank_offset);
    bool stm32f1_flash_busy(void);
    void stm32f1_flash_unlock(uint32_t bank_offset = 0);
    void stm32f1_unlock_erase_flash();

protected:
    uint32_t          _offset = 0;
    uint32_t         _file_size = 0;
    String           _filename = "";
    volatile float    _speed = 0;
    volatile uint8_t _percent = 0;
    //fs + hex file
#if defined(ESP32)
    fs::SPIFFSFS*               _fs;
#endif

    // EERTOS state для кооперативной прошивки
    FlashState       _flashState = FLASH_IDLE;
    bool             _flashError = false;  // флаг ошибки при записи страницы
    String           _flashErrorString = "";  // текст ошибки для фронтенда
    File             _flashFile;
    uint32_t         _flashAddr = 0;
    uint32_t         _flashPosi = 0;
    uint32_t         _flashFileSize = 0;
    uint32_t         _flashStartTime = 0;
    String           _flashPath;
    uint32_t         _chipMemSize = 0;  // размер памяти чипа (из конфига)
    uint32_t         _pageSize = 1024;  // размер страницы (из конфига чипа)
    uint32_t         _wordSize = 2;     // размер слова (из конфига чипа)
    uint32_t         _cswValue = 0xa2000002;  // значение CSW (из конфига чипа)
    uint32_t         _flashStart = 0x08000000;  // стартовый адрес flash (из конфига чипа)
    bool             _isHexFormat = false;  // true если прошиваем HEX-файл
    std::vector<char> _hexBinDataBuf;  // распарсенные бинарные данные из HEX

    // Семейство чипа (stm32f1, stm32f4 и т.д.) — для выбора алгоритма прошивки
    String           _chipFamily = "stm32f1";

    // EERTOS state для кооперативной проверки чипа
    ChipCheckState   _chipState = CHIP_IDLE;
    uint8_t          _chipRetry = 0;
    uint32_t         _chipResultId = 0;

};


// EERTOS-враппер для кооперативной прошивки STM32 (определён в prog_swd.cpp)
void flash_step_task_wrapper();

// EERTOS-враппер для кооперативной проверки чипа (определён в prog_swd.cpp)
void chip_check_step_task_wrapper();

extern ESP_PROGSWD swdprog;


#endif // _PROGSWD_h
