
#ifndef _CORE_SYS_COMMON_MODULE_h
#define _CORE_SYS_COMMON_MODULE_h

#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>

#include "common/TimeLib.h"

// Вспомогательные функции ядра core_sys (чистые, без состояния).
namespace ns_core_sys {

// Валидация пароля администратора: 8-63 символа, только латинские буквы и цифры
bool isAdminPassValid(const String& pass);

// Расчёт CRC по всей записи, но с пропуском поля crc (байты skipOff..skipOff+skipLen)
uint32_t identCrcSkip(uint8_t *data, size_t len, size_t skipOff, size_t skipLen);

// ============================================================
// Подсистема времени — чистые хелперы (для L1-тестов)
// ============================================================

struct TimeSourceView {
    char    name[16];
    int32_t prio;
    bool    enabled;
    bool    validLast;
};

// Индекс лучшего допустимого источника (enabled && validLast, макс. prio);
// -1 — допустимых нет. При равенстве prio — первый по порядку (FCFS).
int timeSelectActive(const TimeSourceView* srcs, int n);

// CSV имён зарегистрированных источников: "ntp,ds3231".
void timeCsvNames(const TimeSourceView* srcs, int n, String& out);

// Разбор часового пояса (десятые доли часа). Валиден диапазон -120..130.
bool timeParseTz(const String& in, int32_t& outDec);

// "YYYY-MM-DD HH:MM:SS" от epoch. buf всегда null-terminated.
void timeFormatNowStr(time_t t, char* buf, size_t n);

// true — кандидат допустим (нет отката назад); false — откат в прошлое.
// graceS оставлен для совместимости сигнатуры: wasRecentlyValid уже учитывает гранс.
bool timeSelectBackJump(time_t candidate, time_t sysT, bool wasRecentlyValid,
                        int32_t maxBackSec = 2, uint32_t graceS = 600);

} // namespace ns_core_sys

#endif // _CORE_SYS_COMMON_MODULE_h
