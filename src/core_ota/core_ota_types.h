#ifndef _CORE_OTA_TYPES_h
#define _CORE_OTA_TYPES_h

// ============================================================
// core_ota_types.h — типы, структуры и define'ы ядра core_ota.
// Реализация: core_ota.cpp (шаблон) и core_ota_engine.cpp
// (сравнение версий и исполнение обновления).
// ============================================================

#include <Arduino.h>
#include <stdint.h>

#define OTA_STR_FILENAME_FIRMWARE           "firmware.bin"
#define OTA_STR_FILENAME_FILESYSTEM         "littlefs.bin"
#define OTA_STR_FILESYSTEM                  "FILESYSTEM"
#define OTA_STR_FIRMWARE                    "FIRMWARE"
#define OTA_STR_UNSUPPORTED                 "UNSUPPORTED"
#define OTA_STR_NAMEMATCH                   "MATCHED"
#define OTA_STR_NAMEDIFF                    "UNMATCHED"

// Разделители имён прошивок/ФС в именах файлов: <ENV>-FIRMWARE-<версия>.bin
#define OTA_STR_SEPARATOR_FIRMWARE          "-FIRMWARE-"
#define OTA_STR_SEPARATOR_FILESYSTEM        "-FILESYS-"

// FS version comparison results
#define FS_VERSION_COMPARE_SAME             "SAME"
#define FS_VERSION_COMPARE_NEWER            "NEWER"
#define FS_VERSION_COMPARE_OLDER            "OLDER"
#define FS_VERSION_COMPARE_MISSING          "NO_JSON"
#define FS_VERSION_COMPARE_ERROR            "ERROR"

// Индексы ota.state (порядок = enum в register_resources).
#define OTA_ST_IDLE     0
#define OTA_ST_UPLOAD   1
#define OTA_ST_VERIFY   2
#define OTA_ST_DONE     3
#define OTA_ST_ERROR    4

enum UpdateTypeFile {
       FILE_TYPE_UNSUPPORTED = -1
    ,  FILE_TYPE_FIRMWARE = 0
    ,  FILE_TYPE_FILESYSTEM = 1
  };


// Structure for file comparison results (MAJOR.MINOR.DATE.BUILD format)
struct fileCompareResult {
    int8_t  nameMatch;      // -1 not checked, 0 not match, 1 match
    int8_t  majorDiff;      // major version difference
    int16_t minorDiff;      // minor version difference (increments on commits)
    int64_t dateDiff;       // date difference (YYYYMMDDHHMM as number)
    int32_t buildDiff;      // build number difference
    UpdateTypeFile fileType; // file type
    uint8_t isDebug;        // 1 if build number present in filename
    // FS version comparison (for filesystem updates)
    int8_t fsVersionCompare;   // compare result against existing FS or firmware
    int64_t fsCurrentDate;     // current FS date (or firmware date)
    int32_t fsCurrentBuild;    // current FS build (or firmware build)
    int32_t fsCurrentMajor;    // current FS major version
    int32_t fsCurrentMinor;    // current FS minor version
};

#endif // _CORE_OTA_TYPES_h
