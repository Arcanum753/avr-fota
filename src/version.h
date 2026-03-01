// Auto-generated version file
// Generated: 2026-03-01 23:13:33

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// НАСТРОЙКИ ФОРМАТИРОВАНИЯ (из скрипта сборки)
// ============================================================
// Формат: MAJOR.MINOR.DATE.BUILD
// MAJOR_DIGITS = 1
// MINOR_DIGITS = 3
// BUILD_DIGITS = 4
// MINOR_LEADING_ZEROS = True
// BUILD_LEADING_ZEROS = True
// DATE_FORMAT = "%Y%m%d%H%M"

// ============================================================
// ВЕРСИЯ В ФОРМАТЕ MAJOR.MINOR.DATE.BUILD
// ============================================================

// Глобальная версия проекта (из version_counter.txt)
#define PROJECT_VERSION_MAJOR 0

// Минорная версия (инкремент при каждом коммите)
#define PROJECT_VERSION_MINOR 1
#define PROJECT_VERSION_MINOR_RAW 1
#define PROJECT_VERSION_MINOR_STR "001"

// Дата и время сборки (yyyyMMddHHmm)
#define BUILD_DATE_STR "202603012313"
#define BUILD_DATE_RAW 202603012313
#define BUILD_DATE_YEAR 2026
#define BUILD_DATE_MONTH 03
#define BUILD_DATE_DAY 01
#define BUILD_TIME_HOUR 23
#define BUILD_TIME_MINUTE 13

// Номер сборки
#define BUILD_NUMBER 18
#define BUILD_NUMBER_RAW 18
#define BUILD_NUMBER_STR "0018"

// Полная версия в формате MAJOR.MINOR.DATE.BUILD
#define FIRMWARE_VERSION "0.001.202603012313.0018"
#define FIRMWARE_VERSION_STR "0.001.202603012313.0018"

// ============================================================
// КОМПОНЕНТЫ ВЕРСИИ ДЛЯ МАТЕМАТИЧЕСКИХ ОПЕРАЦИЙ
// ============================================================

#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_DATE 202603012313
#define VERSION_BUILD 18

// ============================================================
// GIT ИНФОРМАЦИЯ
// ============================================================

#define GIT_BRANCH "version-format"
#define GIT_COMMIT "239cd0e"
#define GIT_COMMIT_FULL "239cd0e30d08d164ab4701bfcc9abcca28abfad4"
#define GIT_TAG "no-tag"
#define GIT_DIRTY true

// ============================================================
// ИНФОРМАЦИЯ О СБОРКЕ
// ============================================================

#define BUILD_ENV "esp8266-gpio"
#define BUILD_TIME "2026-03-01 23:13:33"
#define BUILD_TIMESTAMP "20260301_231333"
#define BUILD_DATE "20260301"
#define BUILD_YEAR 2026
#define BUILD_MONTH 03
#define BUILD_DAY 01
#define BUILD_HOUR 23
#define BUILD_MINUTE 13
#define BUILD_SECOND 33

// ============================================================
// УДОБНЫЕ МАКРОСЫ ДЛЯ ПРОВЕРОК
// ============================================================

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define IS_GIT_DIRTY GIT_DIRTY

// Версия как число (для сравнений)
#define VERSION_NUM ((VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | (int(VERSION_DATE) << 8) | VERSION_BUILD)

// Полная версия как строка (альтернативный макрос)
#define FW_VERSION TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_DATE) "." TOSTRING(VERSION_BUILD)

// ============================================================
// ПРОВЕРКА ЦЕЛОСТНОСТИ
// ============================================================

#if VERSION_MAJOR != PROJECT_VERSION_MAJOR
#error "PROJECT_VERSION_MAJOR inconsistency"
#endif

#endif // VERSION_H
