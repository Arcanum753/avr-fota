#include "main.h"

#include "Arduino.h"
#include "FSWebServerLib.h"
#include "debug_cm.h"

#if defined(ESP32)
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#endif


#include "module_prog_swd.h"
#include "prog_swd.h"
#include "swd.h"
#include "eertos.h"


ESP_PROGSWD swdprog;
ESP_PROGSWD::ESP_PROGSWD(){}

#if defined(ESP32)
void ESP_PROGSWD::setFs(fs::SPIFFSFS* fs) { _fs = fs; }
#endif

int ESP_PROGSWD::stm32_ChipProgrammMain( String &path)  {
  DEBUGLOGSWD(__PRETTY_FUNCTION__);    DEBUGLOGSWD("\r\n");
  
  // TODO пркрутить тип f1xx f4xx
	stm32Fx_abort_all();
	stm32Fx_halt();
	stm32f1_unlock_erase_flash();
	stm32f1_progEn();
	stm32_flash_file(FLASH_START_ADDR, path) ;
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

bool ESP_PROGSWD::startFlash(uint32_t offset, String &path) {
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
    _flashState = FLASH_INIT;
    
    DEBUGLOGSWD("startFlash: %s at 0x%08x\r\n", path.c_str(), offset);
    return true;
}

void ESP_PROGSWD::flashStep() {
    switch (_flashState) {
        case FLASH_INIT: {
            // Открываем файл и определяем размер
            _flashFile = _fs->open(_flashPath, "rb");
            if (!_flashFile) {
                DEBUGLOGSWD("flashStep: FAILED to open %s\r\n", _flashPath.c_str());
                _flashError = true;
                _flashState = FLASH_DONE;
                break;
            }
            _flashFile.seek(0, SeekEnd);
            _flashFileSize = _flashFile.position();
            _flashFile.seek(0, SeekSet);
            // _flashAddr уже установлен в startFlash(), не затираем!
            _flashPosi = 0;
            _flashStartTime = millis();
            
            DEBUGLOGSWD("Going to write %i bytes to flash\r\n", _flashFileSize);
            _flashState = FLASH_WRITE;
            break;
        }
        
        case FLASH_WRITE: {
            // Выполняем abort/halt/unlock/erase/progEn один раз перед началом записи
            if (_flashPosi == 0) {
                // Инициализация SWD и проверка IDCODE
                uint32_t idcode = stm32Fx_begin();
                if (idcode == 0) {
                    DEBUGLOGSWD("flashStep: STM32 not detected (IDCODE=0) — aborting!\r\n");
                    _flashFile.close();
                    _flashError = true;
                    _flashState = FLASH_DONE;
                    break;
                }
                if (idcode != SWD_STM32F103ID) {
                    DEBUGLOGSWD("flashStep: WARNING unexpected IDCODE 0x%08x (expected 0x%08x), continuing...\r\n", idcode, SWD_STM32F103ID);
                }
                stm32Fx_abort_all();
                stm32Fx_halt();
                stm32f1_unlock_erase_flash();
                stm32f1_progEn();
                
                // НЕМЕДЛЕННО пишем первую страницу, пока PG бит ещё установлен!
                // Если вернуть управление EERTOS между progEn и первой записью,
                // STM32 может сбросить PG бит, и запись не сработает.
                uint8_t buffer[PAGESIZE] = {0x00};
                uint32_t cur_len = (_flashFileSize - _flashPosi >= PAGESIZE) ? PAGESIZE : (_flashFileSize - _flashPosi);
                _flashFile.read(buffer, (size_t)cur_len);
                uint8_t write_ret = stm32fX_write_bank(_flashAddr, buffer, cur_len);
                if (write_ret != 0) {
                    DEBUGLOGSWD("flashStep: write_bank returned %i at addr 0x%08x — aborting!\r\n", write_ret, _flashAddr);
                    _flashFile.close();
                    _flashError = true;
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
                
                // Проверяем, закончили ли (файл меньше одной страницы)
                if (_flashPosi >= _flashFileSize) {
                    _flashFile.close();
                    _speed = (float)((float)(_flashFileSize / (float)(millis() - _flashStartTime)));
                    DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n",
                        (int)(millis() - _flashStartTime), _speed);
                    
                    stm32Fx_halt();
                    stm32Fx_unhalt();
                    stm32Fx_rst();
                    
                    _flashState = FLASH_DONE;
                }
                break;  // ← ВАЖНО: выходим из switch, чтобы EERTOS перепланировал задачу
            }
            
            // Все последующие страницы (не первая) — пишем как обычно
            uint8_t buffer[PAGESIZE] = {0x00};
            uint32_t cur_len = (_flashFileSize - _flashPosi >= PAGESIZE) ? PAGESIZE : (_flashFileSize - _flashPosi);
            _flashFile.read(buffer, (size_t)cur_len);
            uint8_t write_ret = stm32fX_write_bank(_flashAddr, buffer, cur_len);
            if (write_ret != 0) {
                DEBUGLOGSWD("flashStep: write_bank returned %i at addr 0x%08x — aborting!\r\n", write_ret, _flashAddr);
                _flashFile.close();
                _flashError = true;
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
            
            // Проверяем, закончили ли
            if (_flashPosi >= _flashFileSize) {
                _flashFile.close();
                _speed = (float)((float)(_flashFileSize / (float)(millis() - _flashStartTime)));
                DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n",
                    (int)(millis() - _flashStartTime), _speed);
                
                // Завершающие операции
                stm32Fx_halt();
                stm32Fx_unhalt();
                stm32Fx_rst();
                
                _flashState = FLASH_DONE;
            }
            break;
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
  swd_AP_Write(AP_CSW, 0xa2000002);
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
// ???
void ESP_PROGSWD::stm32f1_clear_eop(uint32_t bank_offset) {
	uint32_t status = stm32f_read_register( FLASH_SR + bank_offset);
	stm32Fx_write_register(FLASH_SR + bank_offset, status | SR_EOP, 0); /* EOP is W1C */
}
// work
void ESP_PROGSWD::stm32f1_progEn (void) {
  stm32Fx_write_register (FLASH_CR, FLASH_CR_PG, 0);  // Enable programming bit: PG
}

void ESP_PROGSWD::stm32f1_progOff (void) {
  stm32Fx_write_register (FLASH_CR, 0, 0);  // Disaable programming bit
}


/*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/
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


// stm32F1xxx //black magic stm32f1.c
void ESP_PROGSWD::stm32f1_flash_unlock(uint32_t bank_offset) {
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
  // command for unlock flash mem
  stm32Fx_write_register(FLASH_KEYR + bank_offset, KEY1, 0 ); // base + 0x04
  stm32Fx_write_register(FLASH_KEYR + bank_offset, KEY2, 0 ); // base + 0x04
}

// stm32F1xxx
void ESP_PROGSWD::stm32f1_unlock_erase_flash() {
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
  long timeout = millis();

  stm32f1_flash_unlock(FLASH_BANK1_OFFSET);
  // commands to erase flash mem
  stm32Fx_write_register(FLASH_CR + FLASH_BANK1_OFFSET, FLASH_CR_MER  , 0);
  stm32Fx_write_register(FLASH_CR + FLASH_BANK1_OFFSET, FLASH_CR_STRT | FLASH_CR_MER , 0);

  while (stm32f1_flash_busy())  {
    if( millis() - timeout > 2000 )  { return ; }
    delay(1);
  }

}

bool ESP_PROGSWD::stm32f1_flash_busy(void) {
	return ( stm32f_read_register(FLASH_SR) & STM32F1_FLASH_SR_BSY );
}

/*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/
// stm32F4xxx
void ESP_PROGSWD::stm32f4_flash_unlock_dap() {
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
  //swd_dap
  stm32Fx_write_register(SWD_FLASH_PECR, KEY1, 0 ); // base + 0x04
  stm32Fx_write_register(SWD_FLASH_PECR, KEY2, 0 ); // base + 0x04
}
void ESP_PROGSWD::stm32f4_erase_flash_dap() {

  stm32f4_flash_unlock_dap();
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");

  long timeout = millis();
  while (stm32f4_flash_busy())  {    if( millis() - timeout > 100 )  { return ; }  }
  return ;
}

bool ESP_PROGSWD::stm32f4_flash_busy(void) {
  return false;
  // return ( stm32f_read_register(SWD_FLASH_PEKEYR) & FLASH_SR_BSY ); //FIXME
}
/*_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-*/



uint8_t ESP_PROGSWD::stm32_flash_file(uint32_t offset, String &path) {
  // проверка на инициализированность файловой системы
  if (!_fs) { return 2; }
  if (!path.startsWith("/")){ path = "/" + path;}
	uint32_t addr =  offset;
	File file;
	file = _fs->open(path, "rb");
	if (file == 0)  {    return 1;  }
	file.seek(0, SeekEnd);
	uint32_t file_size = file.position();
	file.seek(0, SeekSet);

	DEBUGLOGSWD("Going to write %i bytes to flash\r\n", file_size);

	uint8_t buffer[PAGESIZE] = {0x00};
	long millis_start = millis();

	for (uint32_t posi = 0; posi < file_size; posi += PAGESIZE)  {
		uint32_t cur_len = (file_size - posi >= PAGESIZE) ? PAGESIZE : file_size - posi;
		file.read(buffer, (size_t)cur_len);
		stm32fX_write_bank(addr, buffer, cur_len);
		addr += cur_len;
		uint8_t percent = (uint16_t)(((float)posi / (float)file_size) * 100);
		DEBUGLOGSWD("%i percents \r\n", percent);
		// Обновляем процент для асинхронного опроса с фронтенда
		progSwd.setUploadPercent(percent);
#if defined(ESP32)
		esp_task_wdt_reset();
#endif
		delay(1);
	}
    file.close();
    _speed = (float)((float)(file_size / (float)(millis() - millis_start)));
    DEBUGLOGSWD("Done flashing file, it took %i ms speed: %.4f kbs\r\n", (int)(millis() - millis_start), _speed);
    return 0;
}


uint8_t ESP_PROGSWD::stm32fX_write_bank(uint32_t addr, uint8_t buffer[], uint32_t size) {
  if (size > PAGESIZE) {    return 2;  }  // buffer bigger then a bank
  uint16_t data16b0 = 0;
  uint8_t _ret = 0;

  for (int posi = 0; posi < size; posi += WORDSIZE)   { //WORDSIZE
    //  { data16b0 = (buffer[posi+1] << 8) | (buffer[posi + 0]);    }
    data16b0 =  (buffer[posi + 1] << 8)  | (buffer[posi + 0]);
    uint32_t tmp = (uint32_t)data16b0 << (8U *((addr + posi) & 2U) );

    _ret = stm32Fx_write_flash_16bit(addr + posi, tmp);
    if ( _ret != 1 ) {return 1;}
    delay(1);
  }
  return 0;
}


// for stm32f4
bool ESP_PROGSWD::stm32Fx_write_flash_32bit(uint32_t address, uint32_t value, bool muted) {
  uint32_t temp = 0;
  bool ret = false;
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
       state3 = swd_DP_Read(DP_RDBUFF, temp);
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

                swd_AP_Write(AP_CSW, CSW_SIZE16);
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
       state3 = swd_DP_Read(DP_RDBUFF, temp);
  if (muted == false)   { DEBUGLOGSWD("%i %i %i Write 0x%08x : 0x%08x  read 0x%08x \r\n", state1, state2, state3, address, value, temp );}
  return ret = state1 * state2 * state3;
}
