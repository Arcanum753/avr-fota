#ifndef _CORE_FS_TYPES_h
#define _CORE_FS_TYPES_h

#include <Arduino.h>

// ============================================================
// core_fs — общие константы и типы
// ============================================================

#define CORE_FS_API_VERSION      1
#define CORE_FS_MAX_UPLOAD       (1024UL * 1024UL)
#define CORE_FS_FREE_RESERVE     4096UL
#define CORE_FS_MAX_ENTRIES      256
#define CORE_FS_MAX_PATH         255
#define CORE_FS_MAX_NAME         32   // ограничение LittleFS (ESP32: OBJ_NAME_LEN=32)
#define CORE_FS_MAX_DEPTH        8
#define CORE_FS_BLOCK_SIZE       4096UL

struct FsEntry {
    String   path;
    uint32_t size;
    uint32_t mtime;
    bool     dir;
};

#endif // _CORE_FS_TYPES_h
