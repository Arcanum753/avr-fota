#include "LittleFS.h"
#include <string.h>

FS LittleFS;

MockFile FS::open(const String& path, const char* mode) {
    std::string key = path.c_str();
    bool writable = false;
    if (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))) writable = true;

    if (!writable) {
        auto it = _files.find(key);
        if (it == _files.end()) return MockFile();
        MockFile f(&it->second, false);
        f.setName(key);
        return f;
    }

    std::string& store = _files[key];
    MockFile f(&store, true);
    f.setName(key);
    return f;
}

bool FS::exists(const String& path) const {
    return _files.count(path.c_str()) > 0;
}

bool FS::remove(const String& path) {
    return _files.erase(path.c_str()) > 0;
}

bool FS::rename(const String& from, const String& to) {
    auto it = _files.find(from.c_str());
    if (it == _files.end()) return false;
    _files[to.c_str()] = it->second;
    _files.erase(it);
    return true;
}

bool FS::mkdir(const String&) {
    return true;
}
