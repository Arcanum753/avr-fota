// Auto-generated version file
// Generated: 2026-02-27 22:24:59

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// НАСТРОЙКИ ФОРМАТИРОВАНИЯ (из скрипта сборки)
// ============================================================
// Формат: MAJOR.CORE.MODULE.BUILD
// MAJOR_DIGITS = 1
// CORE_DIGITS = 2
// MODULE_DIGITS = 2
// BUILD_DIGITS = 3
// CORE_LEADING_ZEROS = False
// MODULE_LEADING_ZEROS = False
// BUILD_LEADING_ZEROS = False

// ============================================================
// ВЕРСИЯ В ФОРМАТЕ MAJOR.CORE.MODULE.BUILD
// ============================================================

// Глобальная версия проекта (из version_counter.txt)
#define PROJECT_VERSION_MAJOR 0

// Версия ядра (из version_counter.txt)
#define CORE_VERSION 7
#define CORE_VERSION_RAW 7
#define CORE_VERSION_STR "7"

// Версия модуля (из module_counter.txt в папке модуля)
#define MODULE_VERSION 4
#define MODULE_VERSION_RAW 4
#define MODULE_VERSION_STR "4"

// Номер сборки (из module_counter.txt в папке модуля)
#define BUILD_NUMBER 55
#define BUILD_NUMBER_RAW 55
#define BUILD_NUMBER_STR "55"

// Полная версия в формате MAJOR.CORE.MODULE.BUILD
#define FIRMWARE_VERSION "0.7.4.55"
#define FIRMWARE_VERSION_STR "0.7.4.55"

// ============================================================
// КОМПОНЕНТЫ ВЕРСИИ ДЛЯ МАТЕМАТИЧЕСКИХ ОПЕРАЦИЙ
// ============================================================

#define VERSION_MAJOR 0
#define VERSION_CORE 7
#define VERSION_MODULE 4
#define VERSION_BUILD 55

// ============================================================
// GIT ИНФОРМАЦИЯ
// ============================================================

#define GIT_BRANCH "fix-core-ota"
#define GIT_COMMIT "3cca6a1"
#define GIT_COMMIT_FULL "3cca6a1f83e6a688579807473b501d41e2bda6a3"
#define GIT_TAG "no-tag"
#define GIT_DIRTY true

// ============================================================
// ИНФОРМАЦИЯ О СБОРКЕ
// ============================================================

#define BUILD_ENV "esp8266-isp"
#define BUILD_TIME "2026-02-27 22:24:59"
#define BUILD_TIMESTAMP "20260227_222459"
#define BUILD_DATE "20260227"
#define BUILD_YEAR 2026
#define BUILD_MONTH 02
#define BUILD_DAY 27
#define BUILD_HOUR 22
#define BUILD_MINUTE 24
#define BUILD_SECOND 59

#define ACTIVE_MODULE "module_prog_isp"

// ============================================================
// УДОБНЫЕ МАКРОСЫ ДЛЯ ПРОВЕРОК
// ============================================================

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define IS_GIT_DIRTY GIT_DIRTY

// Версия как число (для сравнений)
#define VERSION_NUM ((VERSION_MAJOR << 24) | (VERSION_CORE << 16) | (VERSION_MODULE << 8) | VERSION_BUILD)

// Полная версия как строка (альтернативный макрос)
#define FW_VERSION TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_CORE) "." TOSTRING(VERSION_MODULE) "." TOSTRING(VERSION_BUILD)

// ============================================================
// ПРОВЕРКА ЦЕЛОСТНОСТИ
// ============================================================

#if VERSION_MAJOR != PROJECT_VERSION_MAJOR
#error "PROJECT_VERSION_MAJOR inconsistency"
#endif

#endif // VERSION_H
