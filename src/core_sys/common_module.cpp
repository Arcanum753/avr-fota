
#include "core_sys/common_module.h"

#include <string.h>
#include <stdlib.h>

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

// ============================================================
// Подсистема времени — чистые хелперы
// ============================================================

int timeSelectActive(const TimeSourceView* srcs, int n) {
	if (srcs == nullptr || n <= 0) { return -1; }
	int best = -1;
	for (int i = 0; i < n; i++) {
		if (!srcs[i].enabled || !srcs[i].validLast) { continue; }
		if (best < 0 || srcs[i].prio > srcs[best].prio) { best = i; }
	}
	return best;
}

void timeCsvNames(const TimeSourceView* srcs, int n, String& out) {
	out = "";
	if (srcs == nullptr || n <= 0) { return; }
	for (int i = 0; i < n; i++) {
		if (i > 0) { out += ","; }
		out += srcs[i].name;
	}
}

bool timeParseTz(const String& in, int32_t& outDec) {
	if (in.length() == 0) { return false; }
	const char* s = in.c_str();
	char* end = nullptr;
	long v = strtol(s, &end, 10);
	if (end == s || (end != nullptr && *end != 0)) { return false; }
	if (v < -120 || v > 130) { return false; }
	outDec = (int32_t)v;
	return true;
}

void timeFormatNowStr(time_t t, char* buf, size_t n) {
	if (buf == nullptr || n == 0) { return; }
	tmElements_t tm;
	breakTime(t, tm);
	snprintf(buf, n, "%04u-%02u-%02u %02u:%02u:%02u",
		(unsigned)(tm.Year + 1970), (unsigned)tm.Month, (unsigned)tm.Day,
		(unsigned)tm.Hour, (unsigned)tm.Minute, (unsigned)tm.Second);
}

bool timeSelectBackJump(time_t candidate, time_t sysT, bool wasRecentlyValid,
                        int32_t maxBackSec, uint32_t graceS) {
	(void)graceS;   // wasRecentlyValid уже учитывает гранс-окно
	if (!wasRecentlyValid) { return true; }
	if (sysT == 0) { return true; }
	int64_t diff = (int64_t)sysT - (int64_t)candidate;
	if (diff > (int64_t)maxBackSec) { return false; }
	return true;
}

} // namespace ns_core_sys
