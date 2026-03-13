// Auto-generated version file
// Generated: 2026-03-13 17:18:04

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
#define PROJECT_VERSION_MINOR 9
#define PROJECT_VERSION_MINOR_RAW 9
#define PROJECT_VERSION_MINOR_STR "009"

// Дата и время сборки (yyyyMMddHHmm)
#define BUILD_DATE_STR "202603131718"
#define BUILD_DATE_RAW 202603131718
#define BUILD_DATE_YEAR 2026
#define BUILD_DATE_MONTH 03
#define BUILD_DATE_DAY 13
#define BUILD_TIME_HOUR 17
#define BUILD_TIME_MINUTE 18

// Номер сборки
#define BUILD_NUMBER 510
#define BUILD_NUMBER_RAW 510
#define BUILD_NUMBER_STR "0510"

// Полная версия в формате MAJOR.MINOR.DATE.BUILD
#define FIRMWARE_VERSION "0.009.202603131718.0510"
#define FIRMWARE_VERSION_STR "0.009.202603131718.0510"

// ============================================================
// КОМПОНЕНТЫ ВЕРСИИ ДЛЯ МАТЕМАТИЧЕСКИХ ОПЕРАЦИЙ
// ============================================================

#define VERSION_MAJOR 0
#define VERSION_MINOR 9
#define VERSION_DATE 202603131718
#define VERSION_BUILD 510

// ============================================================
// GIT ИНФОРМАЦИЯ
// ============================================================

#define GIT_BRANCH "fix-sys-wifi"
#define GIT_COMMIT "3f72a81"
#define GIT_COMMIT_FULL "3f72a8140196137d085ecc643d84ef2d835edfb5"
#define GIT_TAG "no-tag"
#define GIT_DIRTY true

// ============================================================
// ИНФОРМАЦИЯ О СБОРКЕ
// ============================================================

#define BUILD_ENV "TestSolo32"
#define BUILD_TIME "2026-03-13 17:18:04"
#define BUILD_TIMESTAMP "20260313_171804"
#define BUILD_DATE "20260313"
#define BUILD_YEAR 2026
#define BUILD_MONTH 03
#define BUILD_DAY 13
#define BUILD_HOUR 17
#define BUILD_MINUTE 18
#define BUILD_SECOND 04

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
