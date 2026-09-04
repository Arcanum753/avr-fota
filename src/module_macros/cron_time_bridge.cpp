/*
 * cron_time_bridge.cpp — реализация функций времени для ccronexpr на базе
 * common/TimeLib. Время «наивное»/локальное (now() из TimeLib уже сдвинуто
 * в локальный пояс), поэтому cron-выражения интерпретируются в локальном
 * времени без обращения к libc (timegm на ESP отсутствует).
 */

#include "common/TimeLib.h"
#include <time.h>
#include <string.h>

extern "C" {

time_t cron_mktime(struct tm* tm) {
    if (!tm) { return (time_t)-1; }
    tmElements_t e;
    memset(&e, 0, sizeof(e));
    e.Year   = (uint8_t)(tm->tm_year - 70);   // tm_year от 1900 -> Year от 1970
    e.Month  = (uint8_t)(tm->tm_mon + 1);     // tm_mon 0..11 -> Month 1..12
    e.Day    = (uint8_t)tm->tm_mday;
    e.Hour   = (uint8_t)tm->tm_hour;
    e.Minute = (uint8_t)tm->tm_min;
    e.Second = (uint8_t)tm->tm_sec;
    return (time_t)makeTime(e);
}

struct tm* cron_time(time_t* date, struct tm* out) {
    if (!date || !out) { return NULL; }
    tmElements_t e;
    breakTime((time_t)*date, e);
    memset(out, 0, sizeof(struct tm));
    out->tm_sec  = e.Second;
    out->tm_min  = e.Minute;
    out->tm_hour = e.Hour;
    out->tm_mday = e.Day;
    out->tm_mon  = e.Month - 1;
    out->tm_year = e.Year + 70;
    out->tm_wday = e.Wday - 1;                // TimeLib: воскресенье = 1 -> tm_wday 0
    out->tm_isdst = -1;
    return out;
}

}
