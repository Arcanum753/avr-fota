#include "main.h"

#include <cstddef>
#include <Arduino.h>
#include <SPI.h>


#include "core_web/FSWebServerLib.h"

#include "core_json/core_json.h"

#include "submodule_isp.h"
#include "prog_isp.h"
// #include "debug.h"
#include "common/common.h"
#include "../module_prog/format_hex.h"
#include "../module_prog/format_bin.h"
#include "core_sys/eertos.h"


#if defined(ESP32)
#include <LittleFS.h>
#endif


SPISettings fuses_spisettings = SPISettings(AVRISP_SPI_FREQLOW,  MSBFIRST, SPI_MODE0);
SPISettings flash_spisettings = SPISettings(AVRISP_SPI_FREQHIGH, MSBFIRST, SPI_MODE0);

ESP_AVRISP avrprog( PIN_RST);

ESP_AVRISP::ESP_AVRISP( uint8_t reset_pin
                                    , bool reset_state
                                    , bool reset_activehigh):
                                        _reset_pin(reset_pin)
                                        , _reset_state(reset_state)
                                        , _reset_activehigh(reset_activehigh)
{

}

//all about spi and reset
void ESP_AVRISP::setReset(bool rst) {
    _reset_state = rst;
    digitalWrite(_reset_pin, _resetLevel(_reset_state));
}

#if defined(ESP32)
    void ESP_AVRISP::setFs(fs::LittleFSFS* fs)
{   _fs = fs;   }

bool ESP_AVRISP::begin (){
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");

    pinMode(_reset_pin, OUTPUT);
    setReset(true);
    chipNow = chipSignRead();
    // FIXME do the false if smth wrong
    return true;
}

String  ESP_AVRISP:: avrChipSignGet(){    return chipNow;   }

uint16_t ESP_AVRISP::chipSpiTransaction(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint8_t n, m, r;
    SPI.transfer(a);
    n = SPI.transfer(b);
    //if (n != a) error = -1;
    m = SPI.transfer(c);
    r = SPI.transfer(d);
    return 0xFFFFFF & (((uint32_t)n<<16)+(m<<8) + r);
}

void ESP_AVRISP::pmode_begin() {
    DEBUGLOGISP(__PRETTY_FUNCTION__);	DEBUGLOGISP("\r\n");
    pinMode(_reset_pin,  OUTPUT);
    SPI.begin();
    digitalWrite(_reset_pin, _resetLevel(false));
    delayMicroseconds(50);
    digitalWrite(_reset_pin, _resetLevel(true));
    delay(30);
    chipSpiTransaction(STK_PROGMODE);
}
void ESP_AVRISP::pmode_end() {
    SPI.end();
    pinMode(PIN_MISO, INPUT);
    pinMode(PIN_MOSI, INPUT);
    pinMode(PIN_SCK,  INPUT);
    pinMode(PIN_RST,  INPUT);

    digitalWrite(PIN_MISO, LOW);		/* Make sure pullups are off too */
    digitalWrite(PIN_MOSI, LOW);
    digitalWrite(PIN_SCK,  LOW);
    digitalWrite(_reset_pin, _resetLevel(false));
    DEBUGLOGISP(__PRETTY_FUNCTION__);
	DEBUGLOGISP("\r\n");
}

void ESP_AVRISP::chipFusesWrite( uint8_t _high, uint8_t _low, uint8_t _lock, uint8_t _ext) {
    AVRISP_fuses_t AVRISP_fuses;
    chipFusesRead(AVRISP_fuses);

    if (!_lock) _lock = AVRISP_fuses.lock;
    if (!_low)  _low = AVRISP_fuses.low;
    if (!_high) _high = AVRISP_fuses.high;
    if (!_ext)  _ext = AVRISP_fuses.ext;

    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    if (_lock) {chipSpiTransaction(STK_CHIPFUSELOCK_WR, _lock);     chipBusyWaitPolling();}
    if (_low)  {chipSpiTransaction(STK_CHIPFUSELOW_WR, _low);       chipBusyWaitPolling();}
    if (_high) {chipSpiTransaction(STK_CHIPFUSEHIGH_WR, _high);     chipBusyWaitPolling();}
    if (_ext)  {chipSpiTransaction(STK_CHIPFUSEEXT_WR, _ext);       chipBusyWaitPolling();}
    SPI.endTransaction();
    pmode_end();

    DEBUGLOGISP("avr high 0x%02x low  0x%02x  lock  0x%02x ext  0x%02x\r\n",
    _high, _low, _lock, _ext);
    DEBUGLOGISP(__PRETTY_FUNCTION__);
    DEBUGLOGISP("\r\n");
}


void ESP_AVRISP::chipFusesRead( AVRISP_fuses_t &_AVRISP_fuses ) {
    DEBUGLOGISP(__PRETTY_FUNCTION__);   DEBUGLOGISP("\r\n");
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    _AVRISP_fuses.lock  = chipSpiTransaction(STK_CHIPFUSELOCK);
    _AVRISP_fuses.low   = chipSpiTransaction(STK_CHIPFUSELOW);
    _AVRISP_fuses.high  = chipSpiTransaction(STK_CHIPFUSEHIGH);
    _AVRISP_fuses.ext   = chipSpiTransaction(STK_CHIPFUSEEXT);
    SPI.endTransaction();
    pmode_end();
}

String ESP_AVRISP::chipSignRead() {
    DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
    char _ret [10] = {0};
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    uint8_t high    = chipSpiTransaction(STK_READSIGN1);
    uint8_t middle  = chipSpiTransaction(STK_READSIGN2);
    uint8_t low     = chipSpiTransaction(STK_READSIGN3);
    SPI.endTransaction();
    pmode_end();
    sprintf (_ret, "0x%02X%02X%02X", high, middle, low);
    DEBUGLOGISP("\t%s \n\r", _ret);
    return String(_ret);

}

int ESP_AVRISP::chipErase() {
    DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
    int avrsip_err = ERROR_OK;
    pmode_begin();
    SPI.beginTransaction(fuses_spisettings);
    chipSpiTransaction(STK_ERASECHIP);	// chip erase
    SPI.endTransaction();
    chipBusyWaitPolling();
    pmode_end();
    return avrsip_err;
}

// Simply polls the chip until it is not busy any more - for erasing and programming
void ESP_AVRISP::chipBusyWaitPolling()  {
    uint8_t busybit;
    SPI.beginTransaction(fuses_spisettings);
    do {
        busybit = chipSpiTransaction(STK_POLLCHIP);
    } while ((busybit & 0x01) );
    SPI.endTransaction();
}

int ESP_AVRISP::chipFlashPage (byte *pagebuff, uint16_t pageaddr, uint8_t pagesize) {
    DEBUGLOGISP("%04x \n\r ", pageaddr);

    SPI.beginTransaction(flash_spisettings);
    for (uint16_t i=0; i < pagesize/2; i++) {
        DEBUGLOGISP("%02x", pagebuff[2*i]);
        DEBUGLOGISP("%02x", pagebuff[2*i+1]);
        if ( i % 8 == 7) { DEBUGLOGISP("\n\r"); }
        if ( i % 4 == 3) { DEBUGLOGISP(" "); }
        chipFlashWord(LOW, i, pagebuff[2*i]);
        chipFlashWord(HIGH, i, pagebuff[2*i+1]);
    }
    //page addr is in bytes, byt we need to convert to words (/2)
    pageaddr = (pageaddr/2) & (~(((pagesize/2)-1)));

    uint16_t commitreply = chipSpiTransaction(0x4C, (pageaddr >> 8) & 0xFF, pageaddr & 0xFF, 0);
    SPI.endTransaction();
    chipBusyWaitPolling();

    DEBUGLOGISP("\tCommit Page: 0x%04x -> 0x%04x\n\r", pageaddr, commitreply);
    if (commitreply != pageaddr) { return ERR_FLASH;  }
    return ERROR_OK;
}

// Send one byte to the page buffer on the chip
void ESP_AVRISP::chipFlashWord (uint8_t hilo, uint16_t addr, uint8_t data) {
    chipSpiTransaction(0x40+8*hilo, addr>>8 & 0xFF, addr & 0xFF, data);
}


// read *.hex file to buf (использует format_hex)
int ESP_AVRISP::hexFileOpen(String _in){
    if (_in.isEmpty()) {return ERR_NOFILE;}
    if (!_fs){  _fs->begin(); }// If LittleFS is not started

    // Открываем файл для потокового парсинга
    String fullPath = _in;
    if (!fullPath.startsWith("/")) {
        fullPath = "/" + fullPath;
    }
    
    File hexFile = _fs->open(fullPath, "r");
    if (!hexFile) {
        DEBUGLOGISP("Failed to open %s\r\n", _in.c_str());
        return ERR_OPENFILE;
    }
    
    uint32_t chipMemSize = _chipMemSize; // размер памяти чипа (из конфига)
    uint32_t totalBins = 0;
    int32_t ret = hexFileParseStream(hexFile, _hexBinDataBuf, 0, chipMemSize, totalBins);
    hexFile.close();
    
    if (ret < 0) {
        DEBUGLOGISP("Failed to parse %s (err=%d)\r\n", _in.c_str(), ret);
        _hexBinDataBuf.clear();
        return ERR_INCORRECTFILE;
    }
    
    DEBUGLOGISP("file %s open success! %u bytes binary data\r\n", _in.c_str(), ret);
    return ERROR_OK;
}


// Returns 0 if hex file is ok. (использует format_hex)
// Теперь просто проверяет, что данные уже распарсены в hexFileOpen()
int ESP_AVRISP::hexFileBinDataCheck (  )   {
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");

    if (_hexBinDataBuf.empty()) {
        return ERR_NOFILE;
    }

    DEBUGLOGISP("hexFileBinDataCheck OK: %u bytes\r\n", _hexBinDataBuf.size());
    return (int)_hexBinDataBuf.size();
}


int  ESP_AVRISP::hexFile2flashByPages( ){
    DEBUGLOGISP(__PRETTY_FUNCTION__);    DEBUGLOGISP("\r\n");
    int _ret = ERROR_OK;
    if (_hexBinDataBuf.empty()) {
        return ERR_NOFILE;
    }

    uint32_t  pagesize = _pageSize;
    uint32_t  fileSize = _hexBinDataBuf.size();
    uint8_t   spipageBuffer[pagesize];
    uint16_t  spipageaddr = 0;
    uint32_t  posi = 0;

    pmode_begin();
    for (uint16_t y = 0; y < pagesize; y++) spipageBuffer[y] = 0xff;

    while (posi < fileSize) {
        // Заполняем страницу из бинарного буфера
        uint16_t bufPos = 0;
        while (bufPos < pagesize && posi < fileSize) {
            spipageBuffer[bufPos++] = (uint8_t)_hexBinDataBuf.at(posi++);
        }

        // Прошиваем страницу
        _ret = chipFlashPage(spipageBuffer, spipageaddr, pagesize);
        if (_ret != ERROR_OK) {
            break;
        }

        // Очищаем буфер для следующей страницы
        for (uint16_t y = 0; y < pagesize; y++) spipageBuffer[y] = 0xff;
        spipageaddr += pagesize;
    }

    pmode_end();
    return _ret;
}



/*------------------------------------------------------------------------*/
//chip flash verification
// return 0 if all ok
// return addr where is mistake
String ESP_AVRISP::chipFlashVerification() {
    String _ret = "";
    char strbuf[256];
    uint32_t addr = 0;
    uint8_t bytebuf = 0;
    uint8_t bytespi = 0;
    pmode_begin();
    for (addr = 0; addr < _hexBinDataBuf.size(); addr++){
        bytebuf = _hexBinDataBuf.at(addr);
// read this byte
        SPI.beginTransaction(flash_spisettings);
        if (addr % 2) {
            // for 'high' bytes:
            bytespi = chipSpiTransaction(STK_VERIFYADDRHIGH, addr >> 9, addr / 2, 0) & 0xFF;
        } else {
            // for 'low bytes'
            bytespi = chipSpiTransaction(STK_VERIFYADDRLOW,  addr >> 9, addr / 2, 0) & 0xFF;
        }
        SPI.endTransaction();
// verify this byte
        if (bytebuf != bytespi){
            sprintf(strbuf, "Verification error at address 0x%04x. <br> Should be 0x%02x not 0x%02x", addr, bytebuf, bytespi);
            _ret = (String)strbuf;
            DEBUGLOGISP("%s \n\r", strbuf);

            pmode_end();
            return _ret;
        }
    }
    pmode_end();
    sprintf(strbuf, "Verification succesfull! <br> All %d bytes are correct!<br>", addr);
    AVRISP_fuses_t AVRISP_fuses ;
    chipFusesRead(AVRISP_fuses);
    sprintf(strbuf, "Fuses: HIGH 0x%02x LOW 0x%02x LOCK 0x%02x EXT 0x%02x<br>",
        AVRISP_fuses.high, AVRISP_fuses.low, AVRISP_fuses.lock, AVRISP_fuses.ext
        );
    DEBUGLOGISP("%s \n\r", strbuf);
    DEBUGLOGISP(__PRETTY_FUNCTION__); DEBUGLOGISP("\r\n");
    return _ret;

}

// ===== Callback для потоковой записи HEX в flash =====
// Вызывается из hexFileParseStreamWrite() для каждого чанка данных.
// userData — это указатель на ESP_AVRISP.
int hex_write_to_flash_cb(uint32_t chunkAddr, const uint8_t *data, uint32_t size, void *userData) {
    ESP_AVRISP *prog = (ESP_AVRISP *)userData;
    if (!prog) return -1;

    // AVR использует страничную запись. Пишем чанк постранично.
    uint32_t pageSize = prog->_pageSize;
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t curLen = (size - offset > pageSize) ? pageSize : (size - offset);
        
        // Копируем данные во временный буфер страницы
        uint8_t pageBuf[pageSize];
        memset(pageBuf, 0xFF, pageSize);
        memcpy(pageBuf, data + offset, curLen);
        
        // Прошиваем страницу
        prog->pmode_begin();
        int ret = prog->chipFlashPage(pageBuf, chunkAddr + offset, pageSize);
        prog->pmode_end();
        
        if (ret != ERROR_OK) {
            DEBUGLOGISP("hex_write_to_flash_cb: chipFlashPage returned %d at addr 0x%04x\n\r", ret, chunkAddr + offset);
            return -1;
        }
        
        offset += pageSize;
    }

    // Обновляем счётчик записанных байт и процент
    prog->addToFlashPosi(size);
    prog->updatePercent();

    return 0;
}

// ===== Реализация updatePercent =====
void ESP_AVRISP::updatePercent() {
    if (_flashFileSize > 0) {
        _percent = (uint8_t)(((float)_flashPosi / (float)_flashFileSize) * 100.0f);
        DEBUGLOGISP("updatePercent: %u%%\r\n", _percent);
        progIsp.setUploadPercent(_percent);
    }
}

// ===== EERTOS-кооперативная прошивка AVR =====

void ESP_AVRISP::beginFlashStep() {

    // Регистрируем задачу в EERTOS (будет вызываться каждый вызов loop())
    SetTask(flash_step_task_wrapper);
}

// Глобальный враппер для регистрации в EERTOS.
// Перерегистрирует себя в очереди, пока прошивка не завершена.
void flash_step_task_wrapper() {
    avrprog.flashStep();
    // пока прошивка не завершена (FLASH_IDLE) — остаёмся в очереди EERTOS
    if (avrprog.isFlashBusy()) {
        SetTask(flash_step_task_wrapper);
    }
}

// ===== EERTOS-кооперативная проверка чипа =====

void ESP_AVRISP::startChipCheck() {
    if (isChipCheckBusy()) { return; }  // защита от повторного входа
    _chipState = CHIP_INIT;
    _chipRetry = 0;
    _chipResultSig = "";
    DEBUGLOGISP("startChipCheck: beginning chip probe\r\n");
    SetTask(chip_check_step_task_wrapper);
}

// Глобальный враппер для регистрации в EERTOS.
// Перерегистрирует себя в очереди, пока проверка чипа не завершена.
void chip_check_step_task_wrapper() {
    avrprog.chipCheckStep();
    if (avrprog.isChipCheckBusy()) {
        SetTask(chip_check_step_task_wrapper);
    }
}

void ESP_AVRISP::chipCheckStep() {
    switch (_chipState) {
        case CHIP_INIT: {
            _chipRetry = 0;
            _chipState = CHIP_PROBE;
            DEBUGLOGISP("chipCheckStep: CHIP_INIT -> CHIP_PROBE\r\n");
            break;
        }
        
        case CHIP_PROBE: {
            // Читаем сигнатуру AVR-чипа через SPI
            String sig = chipSignRead();
            if (sig.length() > 0 && sig != "0x000000") {
                _chipResultSig = sig;
                _chipState = CHIP_DONE;
                DEBUGLOGISP("chipCheckStep: chip found, signature=%s\r\n", sig.c_str());
            } else {
                _chipRetry++;
                if (_chipRetry >= 15) {
                    _chipResultSig = "";
                    _chipState = CHIP_DONE;
                    DEBUGLOGISP("chipCheckStep: chip NOT found after 15 attempts\r\n");
                }
                // иначе остаёмся в CHIP_PROBE — следующий вызов повторит
            }
            break;
        }
        
        case CHIP_DONE: {
            _chipState = CHIP_IDLE;
            DEBUGLOGISP("chipCheckStep: CHIP_DONE -> CHIP_IDLE, result='%s'\r\n", _chipResultSig.c_str());
            // Вызываем callback в module_prog_isp
            progIsp.onChipCheckComplete(_chipResultSig);
            break;
        }
        
        case CHIP_IDLE:
        default:
            // Ничего не делаем
            break;
    }
}


bool ESP_AVRISP::startFlash(uint32_t offset, String &path, uint32_t chipMemSize, uint32_t pageSize) {
    if (isFlashBusy()) { return false; }  // защита от повторного входа
    if (!_fs) { return false; }
    if (!path.startsWith("/")) { path = "/" + path; }
    
    _flashPath = path;
    _flashAddr = offset;
    _flashPosi = 0;
    _flashFileSize = 0;
    _flashStartTime = 0;
    _percent = 0;
    _flashError = false;
    _flashErrorString = "";
    _flashErrorStage = "";
    _flashErrorPercent = 0;
    _flashState = FLASH_INIT;
    _chipMemSize = chipMemSize;
    _isHexFormat = false;
    _hexBinDataBuf.clear();
    
    // Сохраняем параметры прошивки из конфига чипа
    _pageSize = pageSize;
    _flashStart = offset;
    
    // Определяем формат по расширению файла
    if (hexFileIsFormat(path)) {
        _isHexFormat = true;
        DEBUGLOGISP("startFlash: HEX format detected for %s\r\n", path.c_str());
        
        // Для HEX-формата сразу определяем реальный бинарный размер файла,
        // чтобы корректно рассчитывать процент прошивки.
        // Размер HEX-файла (текстовый) не равен размеру прошивки (бинарному).
        File hexSizeFile = _fs->open(path, "r");
        if (hexSizeFile) {
            int32_t binSize = hexFileGetBinarySize(hexSizeFile);
            hexSizeFile.close();
            if (binSize > 0) {
                _flashFileSize = (uint32_t)binSize;
                DEBUGLOGISP("startFlash: HEX binary size = %u bytes\r\n", binSize);
            } else {
                DEBUGLOGISP("startFlash: WARNING - hexFileGetBinarySize returned %d\r\n", binSize);
            }
        }
    } else if (binFileIsFormat(path)) {

        _isHexFormat = false;
        DEBUGLOGISP("startFlash: BIN format detected for %s\r\n", path.c_str());
    } else {
        DEBUGLOGISP("startFlash: unknown format for %s, treating as BIN\r\n", path.c_str());
        _isHexFormat = false;
    }
    
    DEBUGLOGISP("startFlash: %s at 0x%08x, chipMemSize=%u, pageSize=%u\r\n",
        path.c_str(), offset, chipMemSize, pageSize);
    return true;
}

void ESP_AVRISP::flashStep() {
    switch (_flashState) {
        case FLASH_INIT: {
            _flashErrorStage = "FLASH_INIT";
            _flashErrorPercent = 0;
            if (_isHexFormat) {
                // HEX-формат: потоковый парсинг файла в бинарный буфер
                File hexFile = _fs->open(_flashPath, "r");
                if (!hexFile) {
                    DEBUGLOGISP("flashStep: FAILED to open HEX %s\r\n", _flashPath.c_str());
                    _flashError = true;
                    _flashErrorString = "Failed to open HEX file";
                    _flashState = FLASH_DONE;
                    break;
                }
                
                uint32_t totalBins = 0;
                int32_t parseRet = hexFileParseStream(hexFile, _hexBinDataBuf, _flashStart, _chipMemSize, totalBins);
                hexFile.close();
                
                if (parseRet < 0) {
                    DEBUGLOGISP("flashStep: HEX validation failed (err=%d)\r\n", parseRet);
                    _flashError = true;
                    // Преобразуем код ошибки в текст
                    switch (parseRet) {
                        case -9:  _flashErrorString = "HEX: incorrect file format"; break;
                        case -10: _flashErrorString = "HEX: file not found"; break;
                        case -11: _flashErrorString = "HEX: CRC error"; break;
                        case -12: _flashErrorString = "HEX: memory overflow (exceeds chip size)"; break;
                        case -13: _flashErrorString = "HEX: non-monotonic address"; break;
                        default:  _flashErrorString = "HEX: validation error (" + String(parseRet) + ")"; break;
                    }
                    _flashState = FLASH_DONE;
                    break;
                }
                
                // _flashFileSize уже мог быть установлен в startFlash() через hexFileGetBinarySize().
                // Если нет — устанавливаем из результата парсинга.
                if (_flashFileSize == 0) {
                    _flashFileSize = (uint32_t)parseRet;
                }
                _flashPosi = 0;
                _flashStartTime = millis();
                DEBUGLOGISP("flashStep: HEX parsed, %u bytes binary data, flashFileSize=%u\r\n", parseRet, _flashFileSize);

            } else {
                // BIN-формат: открываем файл как обычно
                _flashFile = binFileOpen(*_fs, _flashPath);
                if (!_flashFile) {
                    DEBUGLOGISP("flashStep: FAILED to open %s\r\n", _flashPath.c_str());
                    _flashError = true;
                    _flashErrorString = "Failed to open BIN file";
                    _flashState = FLASH_DONE;
                    break;
                }
                _flashFileSize = binFileGetSize(_flashFile);
                _flashPosi = 0;
                _flashStartTime = millis();
                DEBUGLOGISP("Going to write %i bytes to flash\r\n", _flashFileSize);
            }
            
            // Стираем чип перед прошивкой
            chipErase();
            
            _flashState = FLASH_WRITE;
            break;
        }
        
        case FLASH_WRITE: {
            // Прошиваем одну страницу за раз (кооперативно)
            uint32_t pagesize = _pageSize;
            uint8_t spipageBuffer[pagesize];
            for (uint16_t y = 0; y < pagesize; y++) spipageBuffer[y] = 0xff;
            
            // Заполняем страницу из данных
            uint16_t bufPos = 0;
            if (_isHexFormat) {
                // HEX: читаем из распарсенного буфера
                while (bufPos < pagesize && _flashPosi < _flashFileSize) {
                    spipageBuffer[bufPos++] = (uint8_t)_hexBinDataBuf.at(_flashPosi++);
                }
            } else {
                // BIN: читаем напрямую из файла
                uint32_t cur_len = (_flashFileSize - _flashPosi >= pagesize) ? pagesize : (_flashFileSize - _flashPosi);
                binFileReadPage(_flashFile, spipageBuffer, cur_len);
                _flashPosi += cur_len;
                bufPos = cur_len;
            }
            
            // Прошиваем страницу
            pmode_begin();
            int _ret = chipFlashPage(spipageBuffer, _flashAddr, pagesize);
            pmode_end();
            
            if (_ret != ERROR_OK) {
                DEBUGLOGISP("flashStep: chipFlashPage returned %d at addr 0x%04x — aborting!\r\n", _ret, _flashAddr);
                if (!_isHexFormat) binFileClose(_flashFile);
                _flashError = true;
                _flashErrorStage = "FLASH_WRITE";
                _flashErrorPercent = _percent;
                _flashErrorString = "Flash write error at page 0x" + String(_flashAddr, HEX);
                _flashState = FLASH_DONE;
                break;
            }
            
            _flashAddr += pagesize;
            
            // Обновляем процент через updatePercent()
            updatePercent();
            
            // Проверяем, закончили ли
            if (_flashPosi >= _flashFileSize) {
                if (!_isHexFormat) binFileClose(_flashFile);
                DEBUGLOGISP("Done flashing file, it took %u ms\r\n", (int)(millis() - _flashStartTime));
                _flashState = FLASH_DONE;
            }
            break;
        }
        
        case FLASH_DONE: {
            // Если была ошибка — освобождаем SPI и пины (аналог pmode_end)
            if (_flashError) {
                DEBUGLOGISP("flashStep: FLASH_DONE with error, releasing SPI\r\n");
                pmode_end();
            }
            // Очищаем HEX-буфер
            _hexBinDataBuf.clear();
            // Сообщаем о завершении — вызываем callback в module_prog_isp
            _flashState = FLASH_IDLE;
            DEBUGLOGISP("flashStep: FLASH_DONE -> IDLE\r\n");
            progIsp.onFlashComplete();
            break;
        }

        
        case FLASH_IDLE:
        default:
            // Ничего не делаем
            break;
    }
}
#endif // ESP32
