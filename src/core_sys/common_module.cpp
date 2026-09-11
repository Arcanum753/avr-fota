
#include "core_sys/common_module.h"

// ============================================================
// Вспомогательные функции ядра core_sys
// ============================================================

namespace ns_core_sys {

// Валидация пароля администратора: 8-63 символа, только латинские буквы и цифры
bool isAdminPassValid(const String& pass) {
	if (pass.length() < 8 || pass.length() > 63) { return false; }
	for (unsigned int i = 0; i < pass.length(); i++) {
		char c = pass.charAt(i);
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
		if (!ok) { return false; }
	}
	return true;
}

// Расчёт CRC по всей записи, но с пропуском поля crc (байты skipOff..skipOff+skipLen).
uint32_t identCrcSkip(uint8_t *data, size_t len, size_t skipOff, size_t skipLen) {
	uint32_t crc = 0xFFFFFFFF;
	for (size_t i = 0; i < len; i++) {
		if (i >= skipOff && i < skipOff + skipLen) { continue; }
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; bit++) {
			uint32_t mask = (crc & 1) ? 0xEDB88320 : 0;
			crc = (crc >> 1) ^ mask;
		}
	}
	return ~crc;
}

} // namespace ns_core_sys
