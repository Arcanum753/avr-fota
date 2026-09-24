#ifndef _MOCK_LITTLEFS_H
#define _MOCK_LITTLEFS_H

// In-memory LittleFS-мок для native-тестов.

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <map>

#include "Arduino.h"

class MockFile : public Print {
public:
    MockFile() : _store(nullptr), _pos(0), _writable(false), _valid(false) {}
    MockFile(std::string* store, bool writable)
        : _store(store), _pos(0), _writable(writable), _valid(true) {
        if (writable && _store) _store->clear();
    }

    operator bool() const { return _valid; }
    bool operator==(bool b) const { return _valid == b; }

    size_t size() const { return _store ? _store->size() : 0; }
    const char* name() const { return _name.c_str(); }
    void setName(const std::string& n) { _name = n; }
    uint32_t position() const { return (uint32_t)_pos; }
    bool seek(uint32_t p) { if (_store && p <= _store->size()) { _pos = p; return true; } return false; }
    int available() { return _store ? (int)(_store->size() - _pos) : 0; }
    int read() {
        if (!_store || _pos >= _store->size()) return -1;
        return (unsigned char)(*_store)[_pos++];
    }
    int peek() {
        if (!_store || _pos >= _store->size()) return -1;
        return (unsigned char)(*_store)[_pos];
    }
    size_t readBytes(char* buf, size_t len) {
        if (!_store) return 0;
        size_t n = 0;
        while (n < len && _pos < _store->size()) { buf[n++] = (*_store)[_pos++]; }
        return n;
    }
    size_t readBytes(uint8_t* buf, size_t len) { return readBytes((char*)buf, len); }
    String readString() {
        if (!_store) return String();
        String s = _store->substr(_pos);
        _pos = _store->size();
        return s;
    }
    String readStringUntil(char terminator) {
        String s;
        if (!_store) return s;
        while (_pos < _store->size()) {
            char c = (*_store)[_pos++];
            if (c == terminator) break;
            s += c;
        }
        return s;
    }
    size_t write(uint8_t c) override {
        if (!_writable || !_store) return 0;
        if (_pos < _store->size()) (*_store)[_pos] = (char)c;
        else _store->push_back((char)c);
        _pos++;
        return 1;
    }
    size_t write(const uint8_t* buf, size_t len) override {
        if (!_writable || !_store) return 0;
        for (size_t i = 0; i < len; i++) write(buf[i]);
        return len;
    }
    void flush() {}
    void close() { _valid = false; }

private:
    std::string* _store;
    size_t       _pos;
    bool         _writable;
    bool         _valid;
    std::string  _name;
};

typedef MockFile File;

class FS {
public:
    bool begin(bool = false) { return true; }    void end() {}
    MockFile open(const String& path, const char* mode = "r");
    bool exists(const String& path) const;
    bool remove(const String& path);
    bool rename(const String& from, const String& to);
    bool mkdir(const String& path);
    void format() { _files.clear(); }

    // Тестовые хелперы.
    void clear() { _files.clear(); }
    bool hasFile(const std::string& path) const { return _files.count(path) > 0; }
    std::string raw(const std::string& path) const {
        auto it = _files.find(path);
        return it == _files.end() ? std::string() : it->second;
    }
    void put(const std::string& path, const std::string& data) { _files[path] = data; }

private:
    std::map<std::string, std::string> _files;
};

extern FS LittleFS;

#endif // _MOCK_LITTLEFS_H
