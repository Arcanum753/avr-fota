#ifndef _CORE_FS_ENGINE_h
#define _CORE_FS_ENGINE_h

#include <LittleFS.h>

#include "core_fs_types.h"

#if defined(ESP32)
typedef fs::LittleFSFS CoreFs;
#elif defined(ESP8266)
typedef FS CoreFs;
#endif

namespace ns_core_fs {

// Собрать плоский список всех записей (файлы + каталоги), лексикографически по path.
// count — фактически записано, truncated — достигнут cap.
bool fsCollectEntries(CoreFs& fs, FsEntry* out, size_t cap,
                      size_t& count, bool& truncated);

// Ёмкость/занятость LittleFS.
bool fsBuildInfo(CoreFs& fs, uint32_t& total, uint32_t& used);

// Операции. Возвращают false при ошибке ФС; notEmpty — каталог не пуст.
bool fsCreateFile(CoreFs& fs, const String& path);
bool fsCreateDir(CoreFs& fs, const String& path);
bool fsRenamePath(CoreFs& fs, const String& from, const String& to);
bool fsRemovePath(CoreFs& fs, const String& path, bool recursive, bool& notEmpty);
bool fsDirIsEmpty(CoreFs& fs, const String& path);

} // namespace ns_core_fs

#endif // _CORE_FS_ENGINE_h
