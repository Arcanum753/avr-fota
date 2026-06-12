#include "main.h"
#include "Arduino.h"
#include "debug_cm.h"
#include "submodule_swd.h"
#include "prog_swd.h"
#include "stm32f4_flash.h"

// ==================== STM32F4xxx Procedures ====================

// Разблокировка flash STM32F4 (пишем ключи в FLASH_KEYR по адресу F4)
void ESP_PROGSWD::stm32f4_flash_unlock_dap() {
  DEBUGLOGSWD(__FUNCTION__);	DEBUGLOGSWD("\r\n");
  // Разблокировка: пишем KEY1, KEY2 в FLASH_KEYR (адрес 0x40023C04)
  stm32Fx_write_register(FLASH_KEYR_F4, KEY1, 0);
  stm32Fx_write_register(FLASH_KEYR_F4, KEY2, 0);
  // Проверяем, что LOCK бит сброшен
  uint32_t cr_val = stm32f_read_register(FLASH_CR_F4);
  if (cr_val & FLASH_CR_LOCK_F4) {
    DEBUGLOGSWD("stm32f4_flash_unlock_dap: WARNING - flash still locked! CR=0x%08x\r\n", cr_val);
  }
}

// Ожидание готовности flash STM32F4 (проверка BSY бита)
bool ESP_PROGSWD::stm32f4_flash_busy(void) {
  uint32_t sr = stm32f_read_register(FLASH_SR_F4);
  return (sr & FLASH_SR_BSY_F4) != 0;
}

// Проверка ошибок flash STM32F4 (возвращает 0 если ошибок нет, иначе маску ошибок)
static uint32_t stm32f4_flash_check_error(ESP_PROGSWD &prog) {
  uint32_t sr = prog.stm32f_read_register(FLASH_SR_F4);
  if (sr & FLASH_SR_ERROR_MASK_F4) {
    DEBUGLOGSWD("stm32f4_flash_check_error: SR=0x%08x errors=0x%08x\r\n", sr, sr & FLASH_SR_ERROR_MASK_F4);
    // Сбрасываем ошибки записью 1 в соответствующие биты (W1C)
    prog.stm32Fx_write_register(FLASH_SR_F4, sr & FLASH_SR_ERROR_MASK_F4, 0);
    return sr & FLASH_SR_ERROR_MASK_F4;
  }
  return 0;
}

// Ожидание завершения операции с таймаутом
bool ESP_PROGSWD::stm32f4_wait_busy(uint32_t timeout_ms) {
  uint32_t start = millis();
  while (stm32f4_flash_busy()) {
    if (millis() - start > timeout_ms) {
      DEBUGLOGSWD("stm32f4_wait_busy: TIMEOUT!\r\n");
      return false; // таймаут
    }
    delay(1);
  }
  // Проверяем ошибки после ожидания
  uint32_t err = stm32f4_flash_check_error(*this);
  return (err == 0);
}

// Включение режима программирования STM32F4 (PG бит + PSIZE=32-bit word)
void ESP_PROGSWD::stm32f4_prog_enable() {
  DEBUGLOGSWD(__FUNCTION__); DEBUGLOGSWD("\r\n");
  uint32_t cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~FLASH_CR_PSIZE_F4;          // очищаем PSIZE
  cr |= FLASH_CR_PG_F4 | FLASH_CR_PSIZE_32_F4;  // PG + 32-bit word size
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
}

// Выключение режима программирования STM32F4
void ESP_PROGSWD::stm32f4_prog_disable() {
  DEBUGLOGSWD(__FUNCTION__); DEBUGLOGSWD("\r\n");
  uint32_t cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~FLASH_CR_PG_F4;
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
}

// Стирание сектора STM32F4 по номеру сектора
// Для F411: сектор 0..7 по 16KB = 128KB, сектор 8..11 по 128KB = 512KB всего
void ESP_PROGSWD::stm32f4_erase_sector(uint8_t sector_num) {
  DEBUGLOGSWD("stm32f4_erase_sector: sector %u\r\n", sector_num);
  
  // Ожидаем готовность
  if (!stm32f4_wait_busy(5000)) {
    DEBUGLOGSWD("stm32f4_erase_sector: busy timeout before erase\r\n");
    return;
  }
  
  // Устанавливаем SER (sector erase) и номер сектора в SNB
  uint32_t cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~(FLASH_CR_SER_F4 | FLASH_CR_SNB_F4 | FLASH_CR_MER_F4 | FLASH_CR_PSIZE_F4);  // очищаем SER, SNB, MER, PSIZE
  cr |= FLASH_CR_SER_F4;                                          // sector erase
  cr |= ((uint32_t)sector_num << FLASH_CR_SNB_POS_F4);            // номер сектора
  cr |= FLASH_CR_PSIZE_32_F4;                                     // 32-bit word size
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  // Запускаем стирание (STRT бит)
  cr |= FLASH_CR_STRT_F4;
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  // Ожидаем завершения
  if (!stm32f4_wait_busy(5000)) {
    DEBUGLOGSWD("stm32f4_erase_sector: timeout during erase of sector %u\r\n", sector_num);
  }
  
  // Сбрасываем SER бит
  cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~(FLASH_CR_SER_F4 | FLASH_CR_SNB_F4);
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  DEBUGLOGSWD("stm32f4_erase_sector: sector %u erased OK\r\n", sector_num);
}

// Mass erase STM32F4 (стирает всё, включая OTP)
void ESP_PROGSWD::stm32f4_mass_erase() {
  DEBUGLOGSWD(__FUNCTION__); DEBUGLOGSWD("\r\n");
  
  // Ожидаем готовность
  if (!stm32f4_wait_busy(5000)) {
    DEBUGLOGSWD("stm32f4_mass_erase: busy timeout before erase\r\n");
    return;
  }
  
  // Устанавливаем MER (mass erase)
  uint32_t cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~(FLASH_CR_SER_F4 | FLASH_CR_SNB_F4 | FLASH_CR_PSIZE_F4);  // очищаем SER, PSIZE
  cr |= FLASH_CR_MER_F4 | FLASH_CR_PSIZE_32_F4;                    // mass erase + 32-bit word size
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  // Запускаем стирание
  cr |= FLASH_CR_STRT_F4;
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  // Ожидаем завершения
  if (!stm32f4_wait_busy(30000)) {  // mass erase может быть долгим
    DEBUGLOGSWD("stm32f4_mass_erase: timeout during erase\r\n");
  }
  
  // Сбрасываем MER бит
  cr = stm32f_read_register(FLASH_CR_F4);
  cr &= ~FLASH_CR_MER_F4;
  stm32Fx_write_register(FLASH_CR_F4, cr, 0);
  
  DEBUGLOGSWD("stm32f4_mass_erase: completed\r\n");
}

// Полная процедура стирания STM32F4 (разблокировка + mass erase)
void ESP_PROGSWD::stm32f4_erase_flash_dap() {
  DEBUGLOGSWD(__FUNCTION__); DEBUGLOGSWD("\r\n");
  stm32f4_flash_unlock_dap();
  stm32f4_mass_erase();
}
