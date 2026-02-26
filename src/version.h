// Auto-generated version file
// Generated: 2026-02-26 11:48:50

#ifndef VERSION_H
#define VERSION_H

// ============================================================
// Версия в формате 0.C.M.B (из файлов счётчиков)
// ============================================================

// Глобальная версия проекта (из version_counter.txt)
#define PROJECT_VERSION_MAJOR 0

// Версия ядра (из version_counter.txt)
#define CORE_VERSION 001

// Версия модуля (из module_counter.txt в папке модуля)
#define MODULE_VERSION 002

// Номер сборки (из module_counter.txt в папке модуля)
#define BUILD_NUMBER 0009

// Полная версия в формате 0.C.M.B
#define FIRMWARE_VERSION "0.001.002.0009"

// ============================================================
// Git информация
// ============================================================

#define GIT_BRANCH "fix-core-code"
#define GIT_COMMIT "6341b62"
#define GIT_COMMIT_FULL "6341b62b2f7445a4a50a8b209695070d2005a25d"
#define GIT_TAG "no-tag"
#define GIT_DIRTY true

// ============================================================
// Информация о сборке
// ============================================================

#define BUILD_ENV "esp32-swd"
#define BUILD_TIME "2026-02-26 11:48:50"
#define BUILD_TIMESTAMP "20260226_114850"
#define ACTIVE_MODULE "module_prog_swd"

// ============================================================
// Удобные макросы для проверок
// ============================================================

#if GIT_DIRTY
#define IS_GIT_DIRTY true
#else
#define IS_GIT_DIRTY false
#endif

#endif // VERSION_H
