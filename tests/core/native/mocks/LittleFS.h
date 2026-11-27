#ifndef _MOCK_LITTLEFS_H
#define _MOCK_LITTLEFS_H

// In-memory LittleFS-мок для native-тестов: файлы + каталоги.

#include <stdint.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <set>
#include <map>

#include "Arduino.h"

struct FSInfo {
    size_t totalBytes;
    size_t usedBytes;
    size_t blockSize;
    size_t pageSize;
    size_t maxOpenFiles;
    size_t maxPathLength;
};

struct MockEntry {
    std::string name;   // полный путь
    bool        dir;
    size_t      size;
};

class MockFile : public Print {
public:
    MockFile() : _store(nullptr), _pos(0), _writable(false), _valid(false),
                 _isDir(false), _metaSize(0), _mtime(0), _entryPos(0) {}
    MockFile(std::string* store, bool writable)
        : _store(store), _pos(0), _writable(writable), _valid(true),
          _isDir(false), _metaSize(0), _mtime(0), _entryPos(0) {
        if (writable && _store) _store->clear();
    }
    MockFile(const std::string& path, bool dir, size_t size, uint32_t mtime)
        : _store(nullptr), _pos(0), _writable(false), _valid(true),
          _isDir(dir), _metaSize(size), _mtime(mtime), _entryPos(0), _path(path), _name(path) {}

    operator bool() const { return _valid; }
    bool operator==(bool b) const { return _valid == b; }

    size_t size() const { return _isDir ? 0 : (_store ? _store->size() : _metaSize); }
    const char* name() const { return _name.c_str(); }
    const char* path() const { return _path.c_str(); }
    const char* fullName() const { return _path.c_str(); }
    void setName(const std::string& n) { _name = n; _path = n; }
    bool isDirectory() const { return _isDir; }
    time_t getLastWrite() const { return (time_t)_mtime; }
    time_t fileTime() const { return (time_t)_mtime; }
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
    size_t read(uint8_t* buf, size_t len) { return readBytes(buf, len); }
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

    void setEntries(const std::vector<MockEntry>& entries) { _entries = entries; _entryPos = 0; }
    MockFile openNextFile() {
        if (!_isDir || _entryPos >= _entries.size()) { return MockFile(); }
        const MockEntry& e = _entries[_entryPos++];
        return MockFile(e.name, e.dir, e.size, 0);
    }

private:
    std::string* _store;
    size_t       _pos;
    bool         _writable;
    bool         _valid;
    bool         _isDir;
    size_t       _metaSize;
    uint32_t     _mtime;
    std::vector<MockEntry> _entries;
    size_t       _entryPos;
    std::string  _name;
    std::string  _path;
};

typedef MockFile File;

class FS {
public:
    bool begin(bool = false) { return true; }
    void end() {}
    MockFile open(const String& path, const char* mode = "r");
    bool exists(const String& path) const;
    bool remove(const String& path);
    bool rmdir(const String& path);
    bool rename(const String& from, const String& to);
    bool mkdir(const String& path);
    bool info(FSInfo& out) const;
    size_t totalBytes() const { return 1024 * 1024; }
    size_t usedBytes() const;
    void format() { _files.clear(); _dirs.clear(); _dirs.insert("/"); }

    // Тестовые хелперы.
    void clear() { format(); }
    bool hasFile(const std::string& path) const { return _files.count(path) > 0; }
    bool hasDir(const std::string& path) const { return _dirs.count(path) > 0; }
    std::string raw(const std::string& path) const {
        auto it = _files.find(path);
        return it == _files.end() ? std::string() : it->second;
    }
    void put(const std::string& path, const std::string& data);

    FS() { _dirs.insert("/"); }

private:
    static std::string parentOf(const std::string& path);
    static std::string baseOf(const std::string& path);
    void ensureParents(const std::string& path);

    std::map<std::string, std::string> _files;
    std::set<std::string> _dirs;
};

extern FS LittleFS;

#endif // _MOCK_LITTLEFS_H
