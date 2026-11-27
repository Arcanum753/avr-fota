#ifndef _CORE_FS_COMMON_MODULE_h
#define _CORE_FS_COMMON_MODULE_h

// ============================================================
// common_module.h — чистые хелперы core_fs (без состояния).
// ============================================================

#include <Arduino.h>

namespace ns_core_fs {

// Привести путь к виду "/a/b": заменить '\' на '/', добавить ведущий '/',
// схлопнуть повторные '/', убрать хвостовой '/'.
String fsNormalizePath(const String& path);

// Проверить нормализованный путь: не пустой, не "/", без NUL, без "."/"..",
// длина <= CORE_FS_MAX_PATH, глубина <= CORE_FS_MAX_DEPTH.
bool fsPathIsSafe(const String& path);

// "/secret.json" без учёта регистра.
bool fsIsSecretPath(const String& path);

// Разрешены ли записи в текущем режиме ядра: init/normal/test.
bool fsWriteAllowed(int mode);

} // namespace ns_core_fs

#endif // _CORE_FS_COMMON_MODULE_h
