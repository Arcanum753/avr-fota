#ifndef _MOCK_EEPROM_H
#define _MOCK_EEPROM_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define MOCK_EEPROM_SIZE 4096

class EEPROMClass {
public:
    bool begin(size_t size) {
        if (size > MOCK_EEPROM_SIZE) size = MOCK_EEPROM_SIZE;
        _size = size;
        memcpy(_data, _persistent, MOCK_EEPROM_SIZE);
        _dirty = false;
        _inUse = true;
        return true;
    }
    void end() {
        if (_dirty) commit();
        _inUse = false;
    }
    uint8_t* getDataPtr() { _dirty = true; return _data; }
    const uint8_t* getConstDataPtr() const { return _persistent; }
    bool commit() {
        memcpy(_persistent, _data, MOCK_EEPROM_SIZE);
        _dirty = false;
        return true;
    }
    uint8_t read(size_t addr) const { return (addr < MOCK_EEPROM_SIZE) ? _persistent[addr] : 0xFF; }
    void write(size_t addr, uint8_t v) {
        if (addr < MOCK_EEPROM_SIZE) { _data[addr] = v; _dirty = true; }
    }
    void put(size_t addr, uint8_t v) { write(addr, v); }
    template <typename T> T get(size_t addr, const T& def = T()) {
        if (addr + sizeof(T) > MOCK_EEPROM_SIZE) return def;
        T v; memcpy(&v, _persistent + addr, sizeof(T)); return v;
    }
    template <typename T> void put(size_t addr, const T& v) {
        if (addr + sizeof(T) > MOCK_EEPROM_SIZE) return;
        memcpy(_data + addr, &v, sizeof(T)); _dirty = true;
    }

    // Тестовые хелперы.
    void mockWipe() {
        memset(_persistent, 0xFF, MOCK_EEPROM_SIZE);
        memset(_data, 0xFF, MOCK_EEPROM_SIZE);
        _dirty = false;
    }
    void mockCorrupt(size_t addr, uint8_t v) { if (addr < MOCK_EEPROM_SIZE) _persistent[addr] = v; }

private:
    uint8_t _data[MOCK_EEPROM_SIZE] = { 0 };
    uint8_t _persistent[MOCK_EEPROM_SIZE];
    size_t  _size = 0;
    bool    _dirty = false;
    bool    _inUse = false;
};

extern EEPROMClass EEPROM;

#endif // _MOCK_EEPROM_H
