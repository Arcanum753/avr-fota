#include "LittleFS.h"
#include <string.h>

FS LittleFS;

std::string FS::parentOf(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    size_t p = path.find_last_of('/');
    if (p == std::string::npos || p == 0) return "/";
    return path.substr(0, p);
}

std::string FS::baseOf(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    size_t p = path.find_last_of('/');
    return (p == std::string::npos) ? path : path.substr(p + 1);
}

void FS::ensureParents(const std::string& path) {
    std::string parent = parentOf(path);
    if (parent == "/" || parent.empty()) return;
    std::string cur;
    size_t pos = 0;
    while (pos < parent.size()) {
        size_t next = parent.find('/', pos + 1);
        if (next == std::string::npos) next = parent.size();
        cur = parent.substr(0, next);
        if (!cur.empty()) _dirs.insert(cur);
        pos = next;
    }
}

MockFile FS::open(const String& path, const char* mode) {
    std::string key = path.c_str();
    bool writable = false;
    if (mode && (strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'))) writable = true;

    if (!writable) {
        auto it = _files.find(key);
        if (it != _files.end()) {
            MockFile f(&it->second, false);
            f.setName(key);
            return f;
        }
        if (_dirs.count(key)) {
            std::vector<MockEntry> entries;
            for (auto& d : _dirs) {
                if (d == key || d == "/") continue;
                if (parentOf(d) == key) entries.push_back({ d, true, 0 });
            }
            for (auto& f : _files) {
                if (parentOf(f.first) == key) entries.push_back({ f.first, false, f.second.size() });
            }
            MockFile dir(key, true, 0, 0);
            dir.setEntries(entries);
            return dir;
        }
        return MockFile();
    }

    if (!_dirs.count(parentOf(key))) return MockFile();
    std::string& store = _files[key];
    MockFile f(&store, true);
    f.setName(key);
    return f;
}

bool FS::exists(const String& path) const {
    return _files.count(path.c_str()) > 0 || _dirs.count(path.c_str()) > 0;
}

bool FS::remove(const String& path) {
    std::string key = path.c_str();
    if (_files.erase(key) > 0) return true;
    return rmdir(path);
}

bool FS::rmdir(const String& path) {
    std::string key = path.c_str();
    if (!_dirs.count(key)) return false;
    for (auto& d : _dirs) {
        if (d != key && parentOf(d) == key) return false;
    }
    for (auto& f : _files) {
        if (parentOf(f.first) == key) return false;
    }
    return _dirs.erase(key) > 0;
}

bool FS::rename(const String& from, const String& to) {
    std::string src = from.c_str();
    std::string dst = to.c_str();
    if (src == dst || exists(to)) return false;

    auto it = _files.find(src);
    if (it != _files.end()) {
        if (!_dirs.count(parentOf(dst))) return false;
        _files[dst] = it->second;
        _files.erase(it);
        return true;
    }

    if (!_dirs.count(src)) return false;
    if (!_dirs.count(parentOf(dst))) return false;

    std::map<std::string, std::string> movedFiles;
    std::vector<std::string> oldFiles;
    for (auto& f : _files) {
        if (f.first == src || f.first.rfind(src + "/", 0) == 0) {
            std::string rest = f.first.substr(src.size());
            movedFiles[dst + rest] = f.second;
            oldFiles.push_back(f.first);
        }
    }
    std::set<std::string> movedDirs;
    std::vector<std::string> oldDirs;
    for (auto& d : _dirs) {
        if (d == src || d.rfind(src + "/", 0) == 0) {
            std::string rest = d.substr(src.size());
            movedDirs.insert(dst + rest);
            oldDirs.push_back(d);
        }
    }
    for (auto& k : oldFiles) _files.erase(k);
    for (auto& k : oldDirs) _dirs.erase(k);
    for (auto& f : movedFiles) _files[f.first] = f.second;
    for (auto& d : movedDirs) _dirs.insert(d);
    return true;
}

bool FS::mkdir(const String& path) {
    std::string key = path.c_str();
    if (key.empty() || key == "/" || exists(path)) return false;
    if (!_dirs.count(parentOf(key))) return false;
    _dirs.insert(key);
    return true;
}

bool FS::info(FSInfo& out) const {
    out.totalBytes = totalBytes();
    out.usedBytes = usedBytes();
    out.blockSize = 4096;
    out.pageSize = 256;
    out.maxOpenFiles = 16;
    out.maxPathLength = 255;
    return true;
}

size_t FS::usedBytes() const {
    size_t total = 0;
    for (auto& f : _files) total += f.second.size();
    return total;
}

void FS::put(const std::string& path, const std::string& data) {
    ensureParents(path);
    _files[path] = data;
}
