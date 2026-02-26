// Auto-generated version file
// Generated: 2026-02-26 11:35:41

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
#define MODULE_VERSION 001

// Номер сборки (из module_counter.txt в папке модуля)
#define BUILD_NUMBER 0008

// Полная версия в формате 0.C.M.B
#define FIRMWARE_VERSION "0.001.001.0008"

// ============================================================
// Git информация
// ============================================================

#define GIT_BRANCH "fix-core-code"
#define GIT_COMMIT "ebe7e63"
#define GIT_COMMIT_FULL "ebe7e6377a635b9a248ecdbcc470f4f8be0e51f1"
#define GIT_TAG "no-tag"
#define GIT_DIRTY true

// ============================================================
// Информация о сборке
// ============================================================

#define BUILD_ENV "esp32-swd"
#define BUILD_TIME "2026-02-26 11:35:41"
#define BUILD_TIMESTAMP "20260226_113541"
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
