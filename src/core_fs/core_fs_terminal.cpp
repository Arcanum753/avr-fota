#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "core_fs/core_fs.h"
#include "core_fs/core_fs_engine.h"
#include "core_fs/core_fs_types.h"
#include "core_fs/common_module.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

#include "common/common.h"

// ============================================================
// Терминальные команды core_fs
// ============================================================

static const char* const kFsCommands[] = { "ls", nullptr };

static const char kFsHelp[] PROGMEM =
    "fs - filesystem listing\r\n"
    "  fs ls [path]      list directory (default /)\r\n"
    "  fs ls -r [path]   list recursively\r\n"
    "  fs help           this help\r\n";

void CLASS_CORE_FS::terminalRegister() {
    TerminalRegisterModule("core_fs", "fs", "filesystem listing",
                           &CLASS_CORE_FS::s_terminalCmd, kFsCommands, kFsHelp, false);
}

void CLASS_CORE_FS::s_terminalCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) {
        TerminalPrintText(kFsHelp);
        return;
    }
    if (strcmp(sub, "ls") != 0) {
        Serial.printf("Unknown fs subcommand '%s'. Type 'fs help'.\r\n", sub);
        return;
    }

    bool recursive = false;
    String path = "/";
    char* a1 = term.getNext();
    if (a1 != nullptr) {
        if (strcmp(a1, "-r") == 0) {
            recursive = true;
            char* a2 = term.getNext();
            if (a2 != nullptr) { path = String(a2); }
        } else {
            path = String(a1);
            char* a2 = term.getNext();
            if (a2 != nullptr) {
                if (strcmp(a2, "-r") == 0) {
                    recursive = true;
                } else {
                    Serial.println("Usage: fs ls [-r] [path]");
                    return;
                }
            }
        }
        if (term.getNext() != nullptr) {
            Serial.println("Usage: fs ls [-r] [path]");
            return;
        }
    }

    path = ns_core_fs::fsNormalizePath(path);
    if (path != "/" && !ns_core_fs::fsPathIsSafe(path)) {
        Serial.printf("Invalid path: %s\r\n", path.c_str());
        return;
    }
    if (core_fs._fs == nullptr) {
        Serial.println("Filesystem is not mounted.");
        return;
    }

    const size_t cap = 128;
    FsEntry* entries = new (std::nothrow) FsEntry[cap];
    if (entries == nullptr) {
        Serial.println("Out of memory.");
        return;
    }

    size_t count = 0;
    bool truncated = false;
    bool ok = ns_core_fs::fsListDir(*core_fs._fs, path, recursive, entries, cap, count, truncated);
    if (!ok) {
        Serial.printf("Not a directory: %s\r\n", path.c_str());
        delete[] entries;
        return;
    }

    Serial.printf("\r\n%s %s:\r\n", recursive ? "Tree" : "Directory", path.c_str());
    Serial.println("TYPE  SIZE            DATE              NAME");

    uint32_t dirs = 0;
    uint32_t files = 0;
    uint32_t total = 0;
    for (size_t i = 0; i < count; i++) {
        const FsEntry& e = entries[i];
        char sizeText[24];
        String sizeHuman;
        if (e.dir) {
            dirs++;
            strncpy(sizeText, "-", sizeof(sizeText));
        } else {
            files++;
            total += e.size;
            sizeHuman = formatBytes((size_t)e.size);
            snprintf(sizeText, sizeof(sizeText), "%s (%lu)", sizeHuman.c_str(),
                     (unsigned long)e.size);
        }

        char dateText[24];
        strncpy(dateText, "-", sizeof(dateText));
        if (!e.dir && e.mtime > 0) {
            time_t t = (time_t)e.mtime;
            struct tm* ptm = gmtime(&t);
            if (ptm != nullptr) {
                strftime(dateText, sizeof(dateText), "%Y-%m-%d %H:%M", ptm);
            }
        }

        Serial.printf("%c     %-15s %-17s %s\r\n",
                      e.dir ? 'd' : 'f', sizeText, dateText, e.path.c_str());
        if ((i & 0x0F) == 0) { yield(); }
    }

    Serial.printf("%lu dirs, %lu files, %s total\r\n",
                  (unsigned long)dirs, (unsigned long)files,
                  formatBytes((size_t)total).c_str());
    if (truncated) {
        Serial.printf("Warning: output limited to %lu entries.\r\n", (unsigned long)cap);
    }
    delete[] entries;
}
