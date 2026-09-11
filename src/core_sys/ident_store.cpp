// ident_store.cpp
//
// Энергонезависимое хранилище идентичности устройства (имя + серийный номер).
// Не стирается при обновлении прошивки и файловой системы:
//   ESP32  — отдельный data-раздел "sysid" (8KB) в partitions_esp32.csv;
//   ESP8266 — EEPROM-секция в конце flash (partition table отсутствует).
//
// Формат записи (в начале хранилища):
//   [0..3]   magic "IDS1"
//   [4..5]   nameLen   (uint16, little-endian)
//   [6..7]   serialLen (uint16, little-endian)
//   [8..11]  crc32     (uint32, little-endian; считается по всей записи с пропуском поля crc)
//   [12...]  payload: name (nameLen байт), затем serial (serialLen байт)

#include <Arduino.h>
#include <string.h>
#include "ident_store.h"
#include "common_module.h"

#if defined(ESP32)
#include <esp_partition.h>
#endif
#if defined(ESP8266)
#include <EEPROM.h>
#endif

// ============================================================
// Параметры хранилища
// ============================================================

#define IDENT_PARTITION_LABEL  "sysid"
// Размер используемой области записи в начале сектора/раздела.
// Полный сектор (4096) для хранения не нужен — запись мала, остальное не трогаем.
#define IDENT_STORE_SIZE       256
#define IDENT_HEADER_LEN       12
#define IDENT_MAGIC0           'I'
#define IDENT_MAGIC1           'D'
#define IDENT_MAGIC2           'S'
#define IDENT_MAGIC3           '1'

// Буфер для операций чтения/записи.
static uint8_t s_identBuf[IDENT_STORE_SIZE];

// ============================================================
// Низкоуровневый доступ к сектору хранения
// ============================================================

static bool identStoreReadRaw(uint8_t *buf, size_t size) {
#if defined(ESP32)
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, IDENT_PARTITION_LABEL);
    if (part == NULL) { return false; }
    if (esp_partition_read(part, 0, buf, size) != ESP_OK) { return false; }
    return true;
#endif
#if defined(ESP8266)
    EEPROM.begin(IDENT_STORE_SIZE);
    // getConstDataPtr не выставляет флаг dirty (в отличие от getDataPtr),
    // иначе последующий end()/commit() стирал бы сектор при каждом чтении.
    memcpy(buf, EEPROM.getConstDataPtr(), size);
    EEPROM.end();
    return true;
#endif
}

static bool identStoreWriteRaw(const uint8_t *buf, size_t size) {
#if defined(ESP32)
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, IDENT_PARTITION_LABEL);
    if (part == NULL) { return false; }
    if (esp_partition_erase_range(part, 0, 0x1000) != ESP_OK) { return false; }
    if (esp_partition_write(part, 0, buf, size) != ESP_OK) { return false; }
    return true;
#endif
#if defined(ESP8266)
    EEPROM.begin(IDENT_STORE_SIZE);
    memcpy(EEPROM.getDataPtr(), buf, size);
    bool ok = EEPROM.commit();
    EEPROM.end();
    return ok;
#endif
}

static bool identStoreEraseRaw(void) {
    memset(s_identBuf, 0xFF, sizeof(s_identBuf));
    return identStoreWriteRaw(s_identBuf, sizeof(s_identBuf));
}

// ============================================================
// Кодирование/декодирование записи
// ============================================================

static size_t identEncode(const String &name, const String &serial) {
    uint16_t nameLen   = name.length();
    uint16_t serialLen = serial.length();

    s_identBuf[0] = IDENT_MAGIC0;
    s_identBuf[1] = IDENT_MAGIC1;
    s_identBuf[2] = IDENT_MAGIC2;
    s_identBuf[3] = IDENT_MAGIC3;
    s_identBuf[4] = (uint8_t)(nameLen & 0xFF);
    s_identBuf[5] = (uint8_t)(nameLen >> 8);
    s_identBuf[6] = (uint8_t)(serialLen & 0xFF);
    s_identBuf[7] = (uint8_t)(serialLen >> 8);

    memcpy(&s_identBuf[IDENT_HEADER_LEN], name.c_str(), nameLen);
    memcpy(&s_identBuf[IDENT_HEADER_LEN + nameLen], serial.c_str(), serialLen);

    size_t total = IDENT_HEADER_LEN + nameLen + serialLen;
    uint32_t crc = ns_core_sys::identCrcSkip(s_identBuf, total, 8, 4);
    s_identBuf[8]  = (uint8_t)(crc & 0xFF);
    s_identBuf[9]  = (uint8_t)(crc >> 8);
    s_identBuf[10] = (uint8_t)(crc >> 16);
    s_identBuf[11] = (uint8_t)(crc >> 24);
    return total;
}

static bool identDecode(String &name, String &serial) {
    if (s_identBuf[0] != IDENT_MAGIC0 ||
        s_identBuf[1] != IDENT_MAGIC1 ||
        s_identBuf[2] != IDENT_MAGIC2 ||
        s_identBuf[3] != IDENT_MAGIC3) { return false; }

    uint16_t nameLen   = (uint16_t)(s_identBuf[4] | (s_identBuf[5] << 8));
    uint16_t serialLen = (uint16_t)(s_identBuf[6] | (s_identBuf[7] << 8));

    if (nameLen > IDENT_MAX_NAME || serialLen > IDENT_MAX_SERIAL) { return false; }

    size_t total = IDENT_HEADER_LEN + nameLen + serialLen;
    uint32_t crcSaved = (uint32_t)s_identBuf[8] |
                        ((uint32_t)s_identBuf[9] << 8) |
                        ((uint32_t)s_identBuf[10] << 16) |
                        ((uint32_t)s_identBuf[11] << 24);
    if (ns_core_sys::identCrcSkip(s_identBuf, total, 8, 4) != crcSaved) { return false; }

    name.reserve(nameLen);
    serial.reserve(serialLen);
    for (uint16_t i = 0; i < nameLen; i++)   { name += (char)s_identBuf[IDENT_HEADER_LEN + i]; }
    for (uint16_t i = 0; i < serialLen; i++) { serial += (char)s_identBuf[IDENT_HEADER_LEN + nameLen + i]; }
    return true;
}

// ============================================================
// Публичное API
// ============================================================

bool identStoreLoad(String &name, String &serial) {
    if (!identStoreReadRaw(s_identBuf, sizeof(s_identBuf))) { return false; }
    name = "";
    serial = "";
    return identDecode(name, serial);
}

bool identStoreSave(const String &name, const String &serial) {
    if (name.length() > IDENT_MAX_NAME || serial.length() > IDENT_MAX_SERIAL) { return false; }
    size_t total = identEncode(name, serial);
    return identStoreWriteRaw(s_identBuf, total);
}

bool identStoreErase(void) {
    return identStoreEraseRaw();
}
