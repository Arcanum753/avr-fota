#include "main.h"

#include "Arduino.h"
#include "FSWebServerLib.h"
#include "debug_cm.h"

#if defined(ESP32)
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#endif


#include "submodule_swd.h"
#include "prog_swd.h"
#include "swd.h"
#include "eertos.h"
#include "../module_prog/format_bin.h"
#include "../module_prog/format_hex.h"
#include "stm32f1_flash.h"
#include "stm32f4_flash.h"

// ===== Callback для потоковой записи HEX в flash =====
// Вызывается из hexFileParseStreamWrite() для каждого чанка данных.
// userData — это указатель на ESP_PROGSWD.
static int hex_write_to_flash_cb(uint32_t chunkAddr, const uint8_t *data, uint32_t size, void *userData) {
    ESP_PROGSWD *prog = (ESP_PROGSWD *)userData;
    if (!prog) return -1;

    // Пишем чанк в flash
    uint8_t ret = prog->stm32fX_write_bank(chunkAddr, (uint8_t *)data, size);
    if (ret != 0) {
        DEBUGLOGSWD("hex_write_to_flash_cb: write_bank returned %u at addr 0x%08x\n\r", ret, chunkAddr);
        return -1;
    }

    // Обновляем счётчик записанных байт и процент
    prog->addToFlashPosi(size);
    prog->updatePercent();

    return 0;
}

// ===== Реализация updatePercent =====
void ESP_PROGSWD::updatePercent() {
    if (_flashFileSize > 0) {
        _percent = (uint8_t)(((float)_flashPosi / (float)_flashFileSize) * 100.0f);
        progSwd.setUploadPercent(_percent);
        DEBUGLOGSWD("%i percents \r\n", _percent);
    }
}


ESP_PROGSWD swdprog;
ESP_PROGSWD::ESP_PROGSWD(){}

#if defined(ESP32)
void ESP_PROGSWD::setFs(fs::SPIFFSFS* fs) { _fs = fs; }
#endif

int ESP_PROGSWD::stm32_ChipProgrammMain( String &path)  {
  DEBUGLOGSWD(__PRETTY_FUNCTION__);    DEBUGLOGSWD("\r\n");
  
  stm32Fx_abort_all();
  stm32Fx_halt();
  
  // Выбор алгоритма по семейству чипа
  if (_chipFamily == "stm32f4") {
    stm32f4_erase_flash_dap();  // unlock + mass erase
    stm32f4_prog_enable();
  } else {
    stm32f1_unlock_erase_flash();
    stm32f1_progEn();
  }
  
  // Проверяем результат прошивки
  uint8_t flash_ret = stm32_flash_file(FLASH_START_ADDR, path);
  if (flash_ret != 0) {
    DEBUGLOGSWD("stm32_ChipProgrammMain: flash_file failed with code %u\r\n", flash_ret);
    // Всё равно пытаемся вывести чип из halt
    stm32Fx_halt();
    stm32Fx_unhalt();
    stm32Fx_rst();
    return (int)flash_ret;
  }
  
  stm32Fx_halt();
  stm32Fx_unhalt();
  stm32Fx_rst();
  return 0;
}

// ===== EERTOS-кооперативная прошивка =====

void ESP_PROGSWD::beginFlashStep() {
    // Регистрируем задачу в EERTOS (будет вызываться каждый вызов loop())
    SetTask(flash_step_task_wrapper);
}

// Глобальный враппер для регистрации в EERTOS.
// Перерегистрирует себя в очереди, пока прошивка не завершена.
void flash_step_task_wrapper() {
    swdprog.flashStep();
    // пока прошивка не завершена (FLASH_IDLE) — остаёмся в очереди EERTOS
    if (swdprog.isFlashBusy()) {
        SetTask(flash_step_task_wrapper);
    }
}

// ===== EERTOS-кооперативная проверка чипа =====

void ESP_PROGSWD::startChipCheck() {
    if (isChipCheckBusy()) { return; }  // защита от повторного входа
    _chipState = CHIP_INIT;
    _chipRetry = 0;
    _chipResultId = 0;
    DEBUGLOGSWD("startChipCheck: beginning chip probe\r\n");
    SetTask(chip_check_step_task_wrapper);
}

// Глобальный враппер для регистрации в EERTOS.
// Перерегистрирует себя в очереди, пока проверка чипа не завершена.
void chip_check_step_task_wrapper() {
    swdprog.chipCheckStep();
    if (swdprog.isChipCheckBusy()) {
        SetTask(chip_check_step_task_wrapper);
    }
}

void ESP_PROGSWD::chipCheckStep() {
    switch (_chipState) {
        case CHIP_INIT: {
            swd_gpio_init();
            _chipRetry = 0;
            _chipState = CHIP_PROBE;
            DEBUGLOGSWD("chipCheckStep: CHIP_INIT -> CHIP_PROBE\r\n");
            break;
        }
        
        case CHIP_PROBE: {
            // Одна попытка прочитать IDCODE через однократные функции без ретраев
            swd_write(0xffffffff, 32);
            swd_write(0xffffffff, 32);
            swd_write(0xe79e, 16);
            swd_write(0xffffffff, 32);
            swd_write(0xffffffff, 32);
            swd_write(0, 32);
            swd_write(0, 32);
            
            uint32_t idcode = 0;
            if (swd_DP_Read_once(DP_IDCODE, idcode) && idcode != 0) {
                _chipResultId = idcode;
                _chipState = CHIP_DONE;
                DEBUGLOGSWD("chipCheckStep: chip found, ID=0x%08x\r\n", idcode);
            } else {
                _chipRetry++;
                if (_chipRetry >= 15) {
                    _chipResultId = 0;
                    _chipState = CHIP_DONE;
                    DEBUGLOGSWD("chipCheckStep: chip NOT found after 15 attempts\r\n");
                }
                // иначе остаёмся в CHIP_PROBE — следующий вызов повторит
            }
            break;
        }
        
        case CHIP_DONE: {
            _chipState = CHIP_IDLE;
            DEBUGLOGSWD("chipCheckStep: CHIP_DONE -> CHIP_IDLE, result=0x%08x\r\n", _chipResultId);
            // Вызываем callback в module_prog_swd
            progSwd.onChipCheckComplete(_chipResultId);
            break;
        }
        
        case CHIP_IDLE:
        default:
            // Ничего не делаем
            break;
    }
}

bool ESP_PROGSWD::startFlash(uint32_t offset, String &path, uint32_t chipMemSize,
                            uint32_t pageSize, uint32_t wordSize, uint32_t cswValue) {
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
    
    // Сохраняем параметры прошивки из конфига чипа
    _pageSize = pageSize;
    _wordSize = wordSize;
    _cswValue = cswValue;
    _flashStart = offset;
    
    // Определяем формат по расширению файла
    if (hexFileIsFormat(path)) {
        _isHexFormat = true;
        DEBUGLOGSWD("startFlash: HEX format detected for %s\r\n", path.c_str());
        
        // Для HEX-формата сразу определяем реальный бинарный размер файла,
        // чтобы корректно рассчитывать процент прошивки.
        // Размер HEX-файла (текстовый) не равен размеру прошивки (бинарному).
        File hexSizeFile = _fs->open(path, "r");
        if (hexSizeFile) {
            int32_t binSize = hexFileGetBinarySize(hexSizeFile);
            hexSizeFile.close();
            if (binSize > 0) {
                _flashFileSize = (uint32_t)binSize;
                DEBUGLOGSWD("startFlash: HEX binary size = %u bytes (file size = %u bytes)\r\n", binSize, hexSizeFile.size());
            } else {
                DEBUGLOGSWD("startFlash: WARNING - hexFileGetBinarySize returned %d, will use file size\r\n", binSize);
            }
        }
    } else if (binFileIsFormat(path)) {
        _isHexFormat = false;
        DEBUGLOGSWD("startFlash: BIN format detected for %s\r\n", path.c_str());
    } else {
        DEBUGLOGSWD("startFlash: unknown format for %s, treating as BIN\r\n", path.c_str());
        _isHexFormat = false;
    }
    
    DEBUGLOGSWD("startFlash: %s at 0x%08x, chipMemSize=%u, pageSize=%u, wordSize=%u, csw=0x%08x\r\n",
        path.c_str(), offset, chipMemSize, pageSize, wordSize, cswValue);
    return true;
}

void ESP_PROGSWD::flashStep() {
    switch (_flashState) {
        case FLASH_INIT: {
            if (_isHexFormat) {
                // HEX-формат: потоковый парсинг с immediate-записью через callback
                File hexFile = _fs->open(_flashPath, "r");
                if (!hexFile) {
                    DEBUGLOGSWD("flashStep: FAILED to open HEX %s\r\n", _flashPath.c_str());
                    _flashError = true;
                    _flashErrorString = "Failed to open HEX file";
                    _flashErrorStage = "FLASH_INIT";
                    _flashErrorPercent = 0;
                    _flashState = FLASH_DONE;
                    break;
                }
                
                // _flashFileSize уже установлен в startFlash() как бинарный размер HEX-файла
                // (см. hexFileGetBinarySize). Не перезаписываем его hexFile.size()!
                _flashPosi = 0;
                _flashStartTime = millis();
                
                // Выполняем abort/halt/unlock/erase/progEn перед началом записи
                uint32_t idcode = stm32Fx_begin();
                if (idcode == 0) {
                    DEBUGLOGSWD("flashStep: STM32 not detected (IDCODE=0) — aborting!\r\n");
                    hexFile.close();
                    _flashError = true;
                    _flashErrorString = "STM32 not detected";
                    _flashErrorStage = "FLASH_INIT";
                    _flashErrorPercent = 0;
                    _flashState = FLASH_DONE;
                    break;
                }
                if (idcode != SWD_STM32F103ID) {
                    DEBUGLOGSWD("flashStep: WARNING unexpected IDCODE 0x%08x (expected 0x%08x), continuing...\r\n", idcode, SWD_STM32F103ID);
                }
                stm32Fx_abort_all();
                stm32Fx_halt();
                
                // Выбор алгоритма по семейству чипа
                if (_chipFamily == "stm32f4") {
                    DEBUGLOGSWD("flashStep: using F4 algorithm (family=%s)\r\n", _chipFamily.c_str());
                    stm32f4_erase_flash_dap();
                    stm32f4_prog_enable();
                } else {
                    DEBUGLOGSWD("flashStep: using F1 algorithm (family=%s)\r\n", _chipFamily.c_str());
                    stm32f1_unlock_erase_flash();
                    stm32f1_progEn();
                }
                
                // Потоковый парсинг HEX с immediate-записью в flash
                int32_t parseRet = hexFileParseStreamWrite(hexFile, _flashStart, _chipMemSize, _pageSize, hex_write_to_flash_cb, this);
                hexFile.close();
                
                if (parseRet < 0) {
                    DEBUGLOGSWD("flashStep: HEX streaming write failed (err=%d)\r\n", parseRet);
                    _flashError = true;
                    _flashErrorStage = "FLASH_INIT";
                    _flashErrorPercent = _percent;
                    switch (parseRet) {
                        case -9:  _flashErrorString = "HEX: incorrect file format"; break;
                        case -10: _flashErrorString = "HEX: file not found"; break;
                        case -11: _flashErrorString = "HEX: CRC error"; break;
                        case -12: _flashErrorString = "HEX: memory overflow (exceeds chip size)"; break;
                        case -13: _flashErrorString = "HEX: non-monotonic address"; break;
                        case -14: _flashErrorString = "HEX: flash write error"; break;
                        default:  _flashErrorString = "HEX: error (" + String(parseRet) + ")"; break;
                    }
                    _flashState = FLASH_DONE;
                    break;
                }
                
                // Успешно записали весь HEX — пересчитываем скорость
                // Используем _flashPosi (реально записанные бинарные байты), а не _flashFileSize
                _speed = (float)((float)(_flashPosi / (float)(millis() - _flashStartTime)));
                DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n",
                    (int)(millis() - _flashStartTime), _speed);
                
                stm32Fx_halt();
                stm32Fx_unhalt();
                stm32Fx_rst();
                
                _flashState = FLASH_DONE;
            } else {
                // BIN-формат: открываем файл как обычно
                _flashFile = binFileOpen(*_fs, _flashPath);
                if (!_flashFile) {
                    DEBUGLOGSWD("flashStep: FAILED to open %s\r\n", _flashPath.c_str());
                    _flashError = true;
                    _flashErrorString = "Failed to open BIN file";
                    _flashErrorStage = "FLASH_INIT";
                    _flashErrorPercent = 0;
                    _flashState = FLASH_DONE;
                    break;
                }
                _flashFileSize = binFileGetSize(_flashFile);
                _flashPosi = 0;
                _flashStartTime = millis();
                DEBUGLOGSWD("Going to write %i bytes to flash\r\n", _flashFileSize);
                _flashState = FLASH_WRITE;
            }
            break;
        }
        
        case FLASH_WRITE: {
            // Выполняем abort/halt/unlock/erase/progEn один раз перед началом записи
            if (_flashPosi == 0) {
                // Инициализация SWD и проверка IDCODE
                uint32_t idcode = stm32Fx_begin();
                if (idcode == 0) {
                    DEBUGLOGSWD("flashStep: STM32 not detected (IDCODE=0) — aborting!\r\n");
                    if (!_isHexFormat) binFileClose(_flashFile);
                    _flashError = true;
                    _flashErrorString = "STM32 not detected";
                    _flashErrorStage = "FLASH_WRITE";
                    _flashErrorPercent = 0;
                    _flashState = FLASH_DONE;
                    break;
                }
                if (idcode != SWD_STM32F103ID) {
                    DEBUGLOGSWD("flashStep: WARNING unexpected IDCODE 0x%08x (expected 0x%08x), continuing...\r\n", idcode, SWD_STM32F103ID);
                }
                stm32Fx_abort_all();
                stm32Fx_halt();
                
                // Выбор алгоритма по семейству чипа
                if (_chipFamily == "stm32f4") {
                    DEBUGLOGSWD("flashStep: using F4 algorithm (family=%s)\r\n", _chipFamily.c_str());
                    stm32f4_erase_flash_dap();  // unlock + mass erase
                    stm32f4_prog_enable();
                } else {
                    DEBUGLOGSWD("flashStep: using F1 algorithm (family=%s)\r\n", _chipFamily.c_str());
                    stm32f1_unlock_erase_flash();
                    stm32f1_progEn();
                }
            }
            
            // Пишем буфером WRITE_BUF_SIZE, пока не закончатся данные
            uint8_t buffer[WRITE_BUF_SIZE];
            uint32_t remaining = _flashFileSize - _flashPosi;
            if (remaining == 0) {
                // Всё записали — завершаем
                if (!_isHexFormat) binFileClose(_flashFile);
                // Используем _flashPosi (реально записанные байты) для скорости
                _speed = (float)((float)(_flashPosi / (float)(millis() - _flashStartTime)));
                DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n",
                    (int)(millis() - _flashStartTime), _speed);
                
                stm32Fx_halt();
                stm32Fx_unhalt();
                stm32Fx_rst();
                
                _flashState = FLASH_DONE;
                break;
            }
            
            // Определяем размер текущего чанка (не больше WRITE_BUF_SIZE)
            uint32_t cur_len = (remaining > WRITE_BUF_SIZE) ? WRITE_BUF_SIZE : remaining;
            
            // Читаем данные в буфер
            memset(buffer, 0x00, WRITE_BUF_SIZE);
            binFileReadPage(_flashFile, buffer, cur_len);
            
            // Пишем чанк в flash
            uint8_t write_ret = stm32fX_write_bank(_flashAddr, buffer, cur_len);
            if (write_ret != 0) {
                DEBUGLOGSWD("flashStep: write_bank returned %i at addr 0x%08x — aborting!\r\n", write_ret, _flashAddr);
                if (!_isHexFormat) binFileClose(_flashFile);
                _flashError = true;
                _flashErrorString = "Flash write error at address 0x" + String(_flashAddr, HEX);
                _flashErrorStage = "FLASH_WRITE";
                _flashErrorPercent = _percent;
                _flashState = FLASH_DONE;
                break;
            }
            _flashAddr += cur_len;
            _flashPosi += cur_len;
            
            // Обновляем процент
            _percent = (uint8_t)(((float)_flashPosi / (float)_flashFileSize) * 100.0f);
            DEBUGLOGSWD("%i percents \r\n", _percent);
            progSwd.setUploadPercent(_percent);
#if defined(ESP32)
            esp_task_wdt_reset();
#endif
            
            break;  // Выходим, чтобы EERTOS перепланировал задачу
        }
        
        case FLASH_DONE: {
            // Если была ошибка — всё равно делаем halt/unhalt/rst, 
            // чтобы STM32 не завис в halt-режиме
            if (_flashError) {
                DEBUGLOGSWD("flashStep: FLASH_DONE with error, releasing target\r\n");
                stm32Fx_halt();
                stm32Fx_unhalt();
                stm32Fx_rst();
            }
            // Сообщаем о завершении — вызываем callback в module_prog_swd
            _flashState = FLASH_IDLE;
            DEBUGLOGSWD("flashStep: FLASH_DONE -> IDLE\r\n");
            progSwd.onFlashComplete();
            break;
        }
        
        case FLASH_IDLE:
        default:
            // Ничего не делаем
            break;
    }
}

void ESP_PROGSWD::stm32Fx_write_port(bool APorDP, uint8_t address, uint32_t value, bool muted) {
  uint32_t temp = 0;
  bool state = false;
  if (APorDP)     {state = swd_AP_Write(address, value);}
  else            {state = swd_DP_Write(address, value);}
  swd_DP_Read(DP_RDBUFF, temp);
  swd_DP_Read(DP_RDBUFF, temp);
  if (!muted) { DEBUGLOGSWD("%i %s Write reg: 0x%02x : 0x%08x r: 0x%08x \r\n", state, APorDP ? "AP" : "DP",  address, value, temp);  }
}



uint32_t ESP_PROGSWD::stm32f_read_register(uint32_t address, bool muted)  {
  uint32_t temp = 0;
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Read(AP_DRW,    temp);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
  bool state4 = swd_DP_Read(DP_RDBUFF, temp);
  if (!muted)
    {DEBUGLOGSWD("%i %i %i %i Read Register: 0x%08x : 0x%08x\r\n", state1, state2, state3, state4, address, temp);}
  return temp;
}

void ESP_PROGSWD::stm32Fx_write_register(uint32_t address, uint32_t value, bool muted) {
  uint32_t temp = 0;
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
  if (muted == false)	{ DEBUGLOGSWD("%i %i %i Write Register: 0x%08x : 0x%08x \r\n", state1, state2, state3, address, value); }
}


/*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/
//sam code
// work
void ESP_PROGSWD::stm32Fx_halt() {
  swd_AP_Write(AP_CSW, _cswValue);
  swd_AP_Write(AP_TAR, 0xe000edf0);
  uint32_t retry = AP_TIMES;
  while (retry--){    swd_AP_Write(AP_DRW, SWD_HALT);  }
}
// work
void ESP_PROGSWD::stm32Fx_unhalt() {
  stm32Fx_write_register(DEMCR, SWD_UNHALT, 0);
}
// work
void ESP_PROGSWD::stm32Fx_rst(){
  stm32Fx_write_register(AIRCR, SWD_RST, 0);
}
uint32_t ESP_PROGSWD::stm32Fx_begin()  {
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
  swd_gpio_init();
  uint32_t temp = 0;
  temp = swd_init();
  if (temp == 0 ) { return temp; }
  if (temp == SWD_STM32F103ID) {} //TODO 

  return temp;
}


void ESP_PROGSWD::stm32Fx_abort_all()  {
  stm32Fx_write_port(0, DP_ABORT,    0x1e, 0);
  stm32Fx_write_port(0, DP_CTRLSTAT, 0x50000000, 0);
}


/*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/



uint8_t ESP_PROGSWD::stm32_flash_file(uint32_t offset, String &path) {
  // проверка на инициализированность файловой системы
  if (!_fs) { return 2; }

	uint32_t addr = offset;
	uint32_t file_size = 0;
	std::vector<char> hexBinBuf;
	bool isHex = hexFileIsFormat(path);

	if (isHex) {
		// HEX-формат: потоковый парсинг файла в бинарный буфер
		File hexFile = _fs->open(path, "r");
		if (!hexFile) {
			DEBUGLOGSWD("stm32_flash_file: FAILED to open HEX %s\r\n", path.c_str());
			return 1;
		}
		
		uint32_t totalBins = 0;
		int32_t parseRet = hexFileParseStream(hexFile, hexBinBuf, FLASH_START_ADDR, _chipMemSize, totalBins);
		hexFile.close();
		
		if (parseRet < 0) {
			DEBUGLOGSWD("stm32_flash_file: HEX validation failed (err=%d)\r\n", parseRet);
			return 1;
		}
		file_size = (uint32_t)parseRet;
		DEBUGLOGSWD("Going to write %i bytes from HEX to flash\r\n", file_size);
	} else {
		// BIN-формат: открываем файл
		File file = binFileOpen(*_fs, path);
		if (!file) { return 1; }
		file_size = binFileGetSize(file);
		DEBUGLOGSWD("Going to write %i bytes from BIN to flash\r\n", file_size);
		
		uint8_t buffer[PAGESIZE] = {0x00};
		long millis_start = millis();

		for (uint32_t posi = 0; posi < file_size; posi += PAGESIZE)  {
			uint32_t cur_len = (file_size - posi >= PAGESIZE) ? PAGESIZE : file_size - posi;
			binFileReadPage(file, buffer, cur_len);
			stm32fX_write_bank(addr, buffer, cur_len);
			addr += cur_len;
			uint8_t percent = (uint16_t)(((float)posi / (float)file_size) * 100);
			DEBUGLOGSWD("%i percents \r\n", percent);
			progSwd.setUploadPercent(percent);
#if defined(ESP32)
			esp_task_wdt_reset();
#endif
			delay(1);
		}
		binFileClose(file);
		_speed = (float)((float)(file_size / (float)(millis() - millis_start)));
		DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n", (int)(millis() - millis_start), _speed);
		return 0;
	}
	
	// HEX: прошиваем из буфера
	uint8_t buffer[PAGESIZE] = {0x00};
	long millis_start = millis();
	
	for (uint32_t posi = 0; posi < file_size; posi += PAGESIZE)  {
		uint32_t cur_len = (file_size - posi >= PAGESIZE) ? PAGESIZE : file_size - posi;
		memcpy(buffer, hexBinBuf.data() + posi, cur_len);
		stm32fX_write_bank(addr, buffer, cur_len);
		addr += cur_len;
		uint8_t percent = (uint16_t)(((float)posi / (float)file_size) * 100);
		DEBUGLOGSWD("%i percents \r\n", percent);
		progSwd.setUploadPercent(percent);
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
		delay(1);
	}
	_speed = (float)((float)(file_size / (float)(millis() - millis_start)));
	DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n", (int)(millis() - millis_start), _speed);
	return 0;
}


uint8_t ESP_PROGSWD::stm32fX_write_bank(uint32_t addr, uint8_t buffer[], uint32_t size) {
  if (size > _pageSize) {    return 2;  }  // buffer bigger then a bank
  uint8_t _ret = 0;

  for (int posi = 0; posi < size; posi += _wordSize)   {
    if (_wordSize == 4) {
      // 32-bit запись для STM32F4
      uint32_t data32 = ((uint32_t)buffer[posi + 3] << 24) |
                        ((uint32_t)buffer[posi + 2] << 16) |
                        ((uint32_t)buffer[posi + 1] << 8)  |
                        ((uint32_t)buffer[posi + 0]);
      _ret = stm32Fx_write_flash_32bit(addr + posi, data32);
      if ( _ret != 1 ) {return 1;}
      delay(1);
    } else {
      // 16-bit запись для STM32F1 (и других с wordSize == 2)
      uint16_t data16b0 = (buffer[posi + 1] << 8) | (buffer[posi + 0]);
      uint32_t tmp = (uint32_t)data16b0 << (8U *((addr + posi) & 2U) );
      _ret = stm32Fx_write_flash_16bit(addr + posi, tmp);
      if ( _ret != 1 ) {return 1;}
      delay(1);
    }
  }
  return 0;
}


// for stm32f4
bool ESP_PROGSWD::stm32Fx_write_flash_32bit(uint32_t address, uint32_t value, bool muted) {
  uint32_t temp = 0;
  bool ret = false;

  // Устанавливаем CSW для 32-битного доступа (как в 16-bit версии, но с CSW_SIZE32)
  swd_AP_Write(AP_CSW, _cswValue);
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
       state3 = swd_DP_Read(DP_RDBUFF, temp);
  
  // Для STM32F4 ожидаем завершения программирования слова
  if (_chipFamily == "stm32f4") {
    stm32f4_wait_busy(100);  // таймаут 100ms на одно слово
  }
  
  if (muted == false) {
    DEBUGLOGSWD("%i %i %i Write 0x%08x : 0x%08x  read 0x%08x \r\n" ,
        state1, state2, state3, address, value, temp );	
  }
  return ret = state1 * state2 * state3;
}

// for stm32f1
bool ESP_PROGSWD::stm32Fx_write_flash_16bit(uint32_t address, uint32_t value, bool muted) {
  uint32_t temp = 0;
  bool ret = false;

  // Формируем CSW для 16-битного доступа: берём _cswValue, очищаем биты размера [2:0], устанавливаем CSW_SIZE16
  uint32_t csw16 = (_cswValue & ~CSW_SIZE) | CSW_SIZE16;
                swd_AP_Write(AP_CSW, csw16);
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
       state3 = swd_DP_Read(DP_RDBUFF, temp);
  if (muted == false)   { DEBUGLOGSWD("%i %i %i Write 0x%08x : 0x%08x  read 0x%08x \r\n", state1, state2, state3, address, value, temp );}
  return ret = state1 * state2 * state3;
}
