
#ifndef _CORE_SYS_COMMON_MODULE_h
#define _CORE_SYS_COMMON_MODULE_h

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

// Вспомогательные функции ядра core_sys (чистые, без состояния).
namespace ns_core_sys {

// Валидация пароля администратора: 8-63 символа, только латинские буквы и цифры
bool isAdminPassValid(const String& pass);

// Расчёт CRC по всей записи, но с пропуском поля crc (байты skipOff..skipOff+skipLen)
uint32_t identCrcSkip(uint8_t *data, size_t len, size_t skipOff, size_t skipLen);

} // namespace ns_core_sys

#endif // _CORE_SYS_COMMON_MODULE_h
