#ifndef _CORE_NTP_COMMON_MODULE_h
#define _CORE_NTP_COMMON_MODULE_h

// ============================================================
// common_module.h — чистые хелперы core_ntp (без состояния).
// ============================================================

#include <Arduino.h>

#include "core_ntp_types.h"

namespace ns_core_ntp {

// Период, после которого последний синк считается устаревшим:
// max(minStaleSec, 2 * periodMin * 60).
uint32_t ntpStaleSec(uint32_t periodMin, uint32_t minStaleSec);

// Прошедшие секунды с lastMs с учётом переполнения millis();
// 0xFFFFFFFF, если синка ещё не было (lastMs == 0).
uint32_t ntpElapsedSec(uint32_t nowMs, uint32_t lastMs);

// Проверка имени сервера: пустое допустимо только при required == false,
// иначе длина 1..NTP_SERVER_NAME_MAX.
bool ntpServerNameValid(const String& name, bool required);

// Следующий непустой сервер по кругу (0->1->2->0); -1, если все пустые.
int ntpNextServerIndex(int cur, const String& s0, const String& s1, const String& s2);

} // namespace ns_core_ntp

#endif // _CORE_NTP_COMMON_MODULE_h
