#ifndef _STM32F1_FLASH_h
#define _STM32F1_FLASH_h

#include <Arduino.h>

// stm32 F1xx
#define KEY1                    0x45670123
#define KEY2                    0xcdef89ab
#define FLASH_BANK1_OFFSET      0x00U
#define FLASH_BANK2_OFFSET      0x40U
#define FLASH_BANK_SPLIT        0x08080000U

#define SR_ERROR_MASK 0x14U
#define SR_PROG_ERROR 0x04U
#define SR_EOP        (1U << 5U)

#define WORDSIZE       2 // bytes
#define PAGESIZE       1024 // bytes

#define SWD_FLASH_BASE_F1     0x40022000
#define FLASH_ACR             SWD_FLASH_BASE_F1 + 0x00
#define FLASH_KEYR            SWD_FLASH_BASE_F1 + 0x04
#define FLASH_OPTKEYR         SWD_FLASH_BASE_F1 + 0x08
#define FLASH_SR              SWD_FLASH_BASE_F1 + 0x0c
#define FLASH_CR              SWD_FLASH_BASE_F1 + 0x10
#define FLASH_OPTCR           SWD_FLASH_BASE_F1 + 0x14

#define STM32F1_FLASH_SR_BSY (1U << 0U)
#define FLASH_CR_OBL_LAUNCH (1U << 13U)
#define FLASH_CR_OPTWRE     (1U << 9U)
#define FLASH_CR_LOCK       (1U << 7U) // don't touch!
#define FLASH_CR_STRT       (1U << 6U)
#define FLASH_CR_OPTER      (1U << 5U)
#define FLASH_CR_OPTPG      (1U << 4U)
#define FLASH_CR_MER        (1U << 2U)
#define FLASH_CR_PER        (1U << 1U)
#define FLASH_CR_PG         (1U << 0U)

// Значение CSW для доступа к flash STM32F1 (32-bit, auto-increment, debug mode)
#define CSW_VALUE_STM32F1   0xa2000002

#endif // _STM32F1_FLASH_h
