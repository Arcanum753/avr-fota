#include "main.h"

#include "Arduino.h"
#include "FSWebServerLib.h"
#include "debug_cm.h"

#ifdef ESP32
#include <SPIFFS.h>
#include <esp_task_wdt.h>
#elif defined(ESP8266)
#include <FS.h>

extern "C" {
  #include "user_interface.h"
  #include "mem.h"
}
#endif

#include "module_prog_swd.h"
#include "prog_swd.h"
#include "swd.h"


ESP_PROGSWD swdprog;
ESP_PROGSWD::ESP_PROGSWD(){}

#if ESP32
    void ESP_PROGSWD::setFs(fs::SPIFFSFS* fs)
#elif defined(ESP8266)
    void ESP_PROGSWD::setFs(FS* fs)                         // esp8266/esp32 flash file system
#endif
{   _fs = fs;   }

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
  if (temp == SWD_STM32F103ID) {} // TODO

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

  while (stm32f1_flash_busy())  {    if( millis() - timeout > 100 )  { return ; }   }

  // TODO offset2 raeder
  // stm32f1_flash_unlock (FLASH_BANK2_OFFSET)
  // stm32Fx_write_register(FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_MER  );
  // stm32Fx_write_register(FLASH_CR + FLASH_BANK2_OFFSET, FLASH_CR_STRT | FLASH_CR_MER );
  // while (stm32f1_flash_busy())  {    if( millis() - timeout > 100 )  { return ; }   }

}

bool ESP_PROGSWD::stm32f1_flash_busy(void) {
	return ( stm32f_read_register(FLASH_SR) & STM32F1_FLASH_SR_BSY , 1);
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

  // stm32Fx_write_register(SWD_FLASH_PRGKEYR, FLASH_CR_MER | FLASH_CR_STRT | FLASH_CR_PSIZE_WORD); // base + 0x10
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
  if (!_fs) { _fs->begin();  }// If SPIFFS is not started
  if (!path.startsWith("/")){ path = "/" + path;}
	uint32_t addr =  offset;
	File file;
  #ifdef ESP32
	file = SPIFFS.open(path, "rb");
  #elif defined(ESP8266)
  file = _fs->open(path, "r");
  #endif
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
		_percent = (uint8_t)(((float)posi / (float)file_size) * 100);
		DEBUGLOGSWD("%i percents \r\n", _percent);
    #ifdef ESP32
		esp_task_wdt_reset();
    #elif defined(ESP8266)
        ESP.wdtDisable();
    #endif
	}
    file.close();
    #ifdef ESP32
    #elif defined(ESP8266)
    ESP.wdtEnable(WDTO_8S);
    #endif
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
    //long end_micros = micros() + 500;
    if ( _ret != 1 ) {return 1;}

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

//   long end_micros = micros() + 500;
                swd_AP_Write(AP_CSW, CSW_SIZE16);
  bool state1 = swd_AP_Write(AP_TAR, address);
  bool state2 = swd_AP_Write(AP_DRW, value);
//   while (micros() < end_micros)    {    }
  bool state3 = swd_DP_Read(DP_RDBUFF, temp);
       state3 = swd_DP_Read(DP_RDBUFF, temp);
  if (muted == false)   { DEBUGLOGSWD("%i %i %i Write 0x%08x : 0x%08x  read 0x%08x \r\n", state1, state2, state3, address, value, temp );}
  return ret = state1 * state2 * state3;
}







