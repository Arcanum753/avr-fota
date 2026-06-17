#include "main.h"
#include "Arduino.h"
#include "debug_cm.h"
#include "submodule_swd.h"
#include "prog_swd.h"
#include "stm32f1_flash.h"

// ==================== STM32F1xxx Procedures ====================

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
    delayMicroseconds(50);
  }
}

bool ESP_PROGSWD::stm32f1_flash_busy(void) {
	return ( stm32f_read_register(FLASH_SR) & STM32F1_FLASH_SR_BSY );
}
