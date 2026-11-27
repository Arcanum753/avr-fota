#include "common_module.h"

#include "core_fs_types.h"
#include "core_state/core_state_types.h"

namespace ns_core_fs {

String fsNormalizePath(const String& path) {
    String src = path;
    src.trim();
    src.replace("\\", "/");

    String out;
    out.reserve(src.length() + 1);
    out += '/';

    bool prevSlash = true;
    for (unsigned int i = 0; i < src.length(); i++) {
        char c = src.charAt(i);
        if (c == '/') {
            if (!prevSlash) { out += '/'; }
            prevSlash = true;
            continue;
        }
        out += c;
        prevSlash = false;
    }

    while (out.length() > 1 && out.endsWith("/")) {
        out.remove(out.length() - 1);
    }
    return out;
}

bool fsPathIsSafe(const String& path) {
    if (path.length() == 0) { return false; }
    if (path.length() > CORE_FS_MAX_PATH) { return false; }
    if (path.charAt(0) != '/') { return false; }
    if (path == "/") { return false; }

    for (unsigned int i = 0; i < path.length(); i++) {
        if (path.charAt(i) == '\0') { return false; }
    }

    int depth = 0;
    int start = 1;
    for (int i = 1; i <= (int)path.length(); i++) {
        if (i == (int)path.length() || path.charAt(i) == '/') {
            int len = i - start;
            if (len <= 0 || len > CORE_FS_MAX_NAME) { return false; }
            if (len == 1 && path.charAt(start) == '.') { return false; }
            if (len == 2 && path.charAt(start) == '.' && path.charAt(start + 1) == '.') {
                return false;
            }
            depth++;
            start = i + 1;
        }
    }
    return depth <= CORE_FS_MAX_DEPTH;
}

bool fsIsSecretPath(const String& path) {
    return path.equalsIgnoreCase("/secret.json");
}

bool fsWriteAllowed(int mode) {
    return mode == CORE_MODE_INIT || mode == CORE_MODE_NORMAL || mode == CORE_MODE_TEST;
}

} // namespace ns_core_fs
