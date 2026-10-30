#include "core_fs_engine.h"

#include "common_module.h"

namespace ns_core_fs {

static uint32_t entryMtime(File& file) {
    return (uint32_t)file.getLastWrite();
}

static String entryPath(File& file, const String& parent) {
    String path;
#if defined(ESP32)
    path = file.path();
#else
    path = file.fullName();
#endif
    if (path.length() == 0) {
        String name = file.name();
        if (name.length() == 0) { return String(); }
        if (parent == "/") {
            path = "/" + name;
        } else {
            path = parent + "/" + name;
        }
    }
    return fsNormalizePath(path);
}

static void collectDir(CoreFs& fs, const String& dir, FsEntry* out, size_t cap,
                       size_t& count, bool& truncated, int depth) {
    if (truncated) { return; }
    File root = fs.open(dir, "r");
    if (!root || !root.isDirectory()) { return; }

    File entry = root.openNextFile();
    while (entry) {
        String path = entryPath(entry, dir);
        bool isDir = entry.isDirectory();
        if (path.length() > 0 && path != "/" && path != dir) {
            if (count >= cap) {
                truncated = true;
                entry.close();
                break;
            }
            FsEntry& item = out[count++];
            item.path = path;
            item.dir = isDir;
            item.size = isDir ? 0 : (uint32_t)entry.size();
            item.mtime = isDir ? 0 : entryMtime(entry);

            if (isDir && depth < CORE_FS_MAX_DEPTH) {
                collectDir(fs, path, out, cap, count, truncated, depth + 1);
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

bool fsCollectEntries(CoreFs& fs, FsEntry* out, size_t cap,
                      size_t& count, bool& truncated) {
    count = 0;
    truncated = false;
    if (out == nullptr || cap == 0) { return true; }
    collectDir(fs, "/", out, cap, count, truncated, 1);

    // Лексикографическая сортировка (вставками: список небольшой).
    for (size_t i = 1; i < count; i++) {
        FsEntry key = out[i];
        size_t j = i;
        while (j > 0 && out[j - 1].path.compareTo(key.path) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return true;
}

bool fsBuildInfo(CoreFs& fs, uint32_t& total, uint32_t& used) {
#if defined(ESP32)
    total = (uint32_t)fs.totalBytes();
    used = (uint32_t)fs.usedBytes();
    return true;
#else
    FSInfo info;
    if (!fs.info(info)) { return false; }
    total = (uint32_t)info.totalBytes;
    used = (uint32_t)info.usedBytes;
    return true;
#endif
}

bool fsCreateFile(CoreFs& fs, const String& path) {
    File file = fs.open(path, "w");
    if (!file) { return false; }
    file.close();
    return true;
}

bool fsCreateDir(CoreFs& fs, const String& path) {
    return fs.mkdir(path);
}

bool fsRenamePath(CoreFs& fs, const String& from, const String& to) {
    if (fs.rename(from, to)) { return true; }

    // Fallback для LittleFS: некоторые сборки не переименовывают файл,
    // если он недавно открывался. Копируем содержимое и удаляем исходник.
    File src = fs.open(from, "r");
    if (!src || src.isDirectory()) { return false; }
    File dst = fs.open(to, "w");
    if (!dst) { src.close(); return false; }

    uint8_t buf[256];
    bool ok = true;
    int n = 0;
    while ((n = src.read(buf, sizeof(buf))) > 0) {
        if (dst.write(buf, (size_t)n) != (size_t)n) { ok = false; break; }
    }
    src.close();
    dst.close();
    if (!ok) {
        fs.remove(to);
        return false;
    }
    return fs.remove(from);
}

bool fsDirIsEmpty(CoreFs& fs, const String& path) {
    File dir = fs.open(path, "r");
    if (!dir || !dir.isDirectory()) { return false; }
    File entry = dir.openNextFile();
    bool empty = !entry;
    if (entry) { entry.close(); }
    dir.close();
    return empty;
}

static bool removeRecursive(CoreFs& fs, const String& path) {
    File node = fs.open(path, "r");
    if (!node) { return false; }
    if (!node.isDirectory()) {
        node.close();
        return fs.remove(path);
    }

    File entry = node.openNextFile();
    while (entry) {
        String child = entryPath(entry, path);
        entry.close();
        if (child.length() > 0 && child != path) {
            if (!removeRecursive(fs, child)) {
                node.close();
                return false;
            }
        }
        entry = node.openNextFile();
    }
    node.close();
    return fs.rmdir(path);
}

bool fsRemovePath(CoreFs& fs, const String& path, bool recursive, bool& notEmpty) {
    notEmpty = false;
    File node = fs.open(path, "r");
    if (!node) { return false; }

    if (!node.isDirectory()) {
        node.close();
        return fs.remove(path);
    }
    node.close();

    if (!recursive) {
        if (!fsDirIsEmpty(fs, path)) {
            notEmpty = true;
            return false;
        }
        return fs.rmdir(path);
    }
    return removeRecursive(fs, path);
}

} // namespace ns_core_fs
