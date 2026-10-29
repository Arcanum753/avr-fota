#include "common_module.h"

namespace ns_core_ntp {

// ============================================================
// Свежесть последнего синка
// ============================================================

uint32_t ntpStaleSec(uint32_t periodMin, uint32_t minStaleSec) {
	// periodMin уже прокламлен (0..NTP_PERIOD_MAX_MIN), переполнения нет:
	// 720 * 120 = 86400 << 2^32.
	uint32_t twice = periodMin * 2UL * 60UL;
	return (twice > minStaleSec) ? twice : minStaleSec;
}

uint32_t ntpElapsedSec(uint32_t nowMs, uint32_t lastMs) {
	if (lastMs == 0) { return 0xFFFFFFFFUL; }
	// Разность беззнаковых корректна и при переполнении millis().
	return (uint32_t)(nowMs - lastMs) / 1000UL;
}

// ============================================================
// Серверы
// ============================================================

bool ntpServerNameValid(const String& name, bool required) {
	unsigned int len = name.length();
	if (len == 0) { return !required; }
	return len <= NTP_SERVER_NAME_MAX;
}

int ntpNextServerIndex(int cur, const String& s0, const String& s1, const String& s2) {
	const String* names[NTP_SERVER_COUNT] = { &s0, &s1, &s2 };
	if (cur < 0 || cur >= NTP_SERVER_COUNT) { cur = 0; }
	for (int i = 1; i <= NTP_SERVER_COUNT; i++) {
		int idx = (cur + i) % NTP_SERVER_COUNT;
		if (names[idx]->length() > 0) { return idx; }
	}
	return -1;
}

} // namespace ns_core_ntp
