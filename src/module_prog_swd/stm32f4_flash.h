#ifndef _STM32F4_FLASH_h
#define _STM32F4_FLASH_h

#include <Arduino.h>

// stm32F4
#define SWD_FLASH_BASE_F4     0x40023c00  //  0x 4002 3c00
#define FLASH_KEYR_F4         SWD_FLASH_BASE_F4 + 0x04
#define FLASH_OPTKEYR_F4      SWD_FLASH_BASE_F4 + 0x08
#define FLASH_SR_F4           SWD_FLASH_BASE_F4 + 0x0c
#define FLASH_CR_F4           SWD_FLASH_BASE_F4 + 0x10
#define FLASH_OPTCR_F4        SWD_FLASH_BASE_F4 + 0x14
#define FLASH_OPTCR1_F4       SWD_FLASH_BASE_F4 + 0x18

// STM32F4 Flash SR bits
#define FLASH_SR_BSY_F4       (1U << 16U)
#define FLASH_SR_EOP_F4       (1U << 0U)
#define FLASH_SR_WRPERR_F4    (1U << 4U)
#define FLASH_SR_PGAERR_F4    (1U << 3U)
#define FLASH_SR_PGPERR_F4    (1U << 2U)
#define FLASH_SR_ERSERR_F4    (1U << 7U)
#define FLASH_SR_ERROR_MASK_F4 (FLASH_SR_WRPERR_F4 | FLASH_SR_PGAERR_F4 | FLASH_SR_PGPERR_F4 | FLASH_SR_ERSERR_F4)

// STM32F4 Flash CR bits
#define FLASH_CR_PG_F4        (1U << 0U)
#define FLASH_CR_SER_F4       (1U << 1U)
#define FLASH_CR_MER_F4       (1U << 2U)
#define FLASH_CR_SNB_F4       (3U << 3U)    // Sector number mask
#define FLASH_CR_SNB_POS_F4   3             // Sector number position
#define FLASH_CR_STRT_F4      (1U << 16U)
#define FLASH_CR_LOCK_F4      (1U << 31U)
#define FLASH_CR_EOPIE_F4     (1U << 24U)
#define FLASH_CR_ERRIE_F4     (1U << 25U)

// STM32F4 sector size definitions (F411 has 16KB sectors)
#define F4_SECTOR_SIZE_16KB   16384
#define F4_SECTOR_SIZE_128KB  131072

// STM32F4 DBGMCU IDCODE register (для дополнительной идентификации чипа)
#define DBGMCU_IDCODE         0xE0042000

#endif // _STM32F4_FLASH_h
