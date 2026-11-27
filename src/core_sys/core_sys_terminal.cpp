#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <string.h>

#if defined(ESP32)
#include <WiFi.h>
#endif
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

#include "core_sys/core_sys.h"
#include "core_sys/common_module.h"
#include "core_sys/ident_store.h"
#include "core_state/core_state.h"
#include "components_info.h"
#include "version.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Терминальные команды core_sys
// ============================================================

static const char* const kSysCommands[] = {
    "?", "info", "ver", "stat", "name", "serial", "auth", "reset", "mode", nullptr
};

static const char kSysHelp[] PROGMEM =
    "sys - system information and settings\r\n"
    "  ? | sys          current system state (CPU/RAM/FS/WiFi)\r\n"
    "  info             device serial and chip information\r\n"
    "  ver              firmware, FS, git and component versions\r\n"
    "  stat             component runtime status\r\n"
    "  name [<name>]    show/set device name (A-Za-z0-9-_, 1..63)\r\n"
    "  serial [<s>]     show/set device serial (A-Za-z0-9-_, 1..63)\r\n"
    "  auth             show web auth (user/password in plain text)\r\n"
    "  auth user <s>    set web auth user\r\n"
    "  auth pass <s>    set web auth password (8..63, A-Za-z0-9)\r\n"
    "  auth on|off      enable/disable web auth\r\n"
    "  auth reset       disable and clear user/password (y/n)\r\n"
    "  reset            reboot (y/n)\r\n"
    "  id               show device name/serial (alias)\r\n"
    "  mode [<name>]    show/set core mode (debug: init/normal/ota/fs_update/prog/test)\r\n";

static const char kAuthHelp[] PROGMEM =
    "auth commands:\r\n"
    "  auth                 show auth, user, pass, security question/answer\r\n"
    "  auth user <s>        set user (A-Za-z0-9._-, 1..32)\r\n"
    "  auth pass <s>        set password (A-Za-z0-9, 8..63)\r\n"
    "  auth on              enable auth (requires valid user/pass)\r\n"
    "  auth off             disable auth (user/pass kept, y/n)\r\n"
    "  auth reset           disable and clear user/pass (y/n)\r\n";

static String modeName() {
    const char* n = core_state.enumNameByIndex("system.mode", core_state.getMode());
    return (n != nullptr) ? String(n) : String(core_state.getMode());
}

static void printAuthRebootNote() {
    Serial.println("Saved. Reboot required to apply to AP/OTA.");
}

static void printApClients() {
    BusValue result;
    if (core_state.call("wifi.ap_clients_list", 0, nullptr, result) != 0) { return; }
    if (result.kind != BusValue::STR || result.s.length() == 0) { return; }
    String list = result.s;
    int start = 0;
    while (start < (int)list.length()) {
        int end = list.indexOf("\r\n", start);
        if (end < 0) { end = list.length(); }
        String line = list.substring(start, end);
        if (line.length() > 0) {
            Serial.printf("    %s\r\n", line.c_str());
        }
        start = end + 2;
    }
}

static void printWifiState() {
    bool ap = core_state.getBool("wifi.ap_mode", false);
    bool connected = core_state.getBool("wifi.connected", false);

    if (ap) {
        Serial.println("WiFi:     AP mode");
        Serial.printf("  SSID:   %s\r\n", core_state.getStr("wifi.ap_ssid", "").c_str());
        Serial.printf("  IP:     %s\r\n", core_state.getStr("wifi.ap_ip", "").c_str());
        Serial.printf("  Clients: %d\r\n", (int)core_state.getInt("wifi.ap_clients", 0));
        printApClients();
        return;
    }
    if (connected) {
        Serial.println("WiFi:     STA connected");
        Serial.printf("  SSID:   %s\r\n", core_state.getStr("wifi.slot_name", "").c_str());
        Serial.printf("  IP:     %s\r\n", core_state.getStr("wifi.ip", "").c_str());
        Serial.printf("  RSSI:   %d dBm\r\n", (int)core_state.getInt("wifi.rssi", 0));
        Serial.printf("  DHCP:   %s\r\n", core_state.getBool("wifi.dhcp", true) ? "on" : "off");
        return;
    }
    Serial.println("WiFi:     not connected");
}

void CLASS_CORE_SYS::terminalRegister() {
    TerminalRegisterModule("core_sys", "sys", "system information and settings",
                           &CLASS_CORE_SYS::s_cmdSys, kSysCommands, kSysHelp, true);

    TerminalRegisterAlias("?",      &CLASS_CORE_SYS::s_cmdSys);
    TerminalRegisterAlias("info",   &CLASS_CORE_SYS::s_cmdInfo);
    TerminalRegisterAlias("ver",    &CLASS_CORE_SYS::s_cmdVer);
    TerminalRegisterAlias("stat",   &CLASS_CORE_SYS::s_cmdStat);
    TerminalRegisterAlias("name",   &CLASS_CORE_SYS::s_cmdName);
    TerminalRegisterAlias("serial", &CLASS_CORE_SYS::s_cmdSerial);
    TerminalRegisterAlias("auth",   &CLASS_CORE_SYS::s_cmdAuth);
    TerminalRegisterAlias("reset",  &CLASS_CORE_SYS::s_cmdReset);
    TerminalRegisterAlias("id",     &CLASS_CORE_SYS::s_cmdId);
}

// ============================================================
// ?
// ============================================================

void CLASS_CORE_SYS::s_cmdSys() {
    char* arg = term.getNext();
    if (arg != nullptr && strcmp(arg, "help") == 0) {
        TerminalPrintText(kSysHelp);
        return;
    }

    // sys mode [<name|index>] — отладочная команда: показать/сменить режим ядра
    if (arg != nullptr && strcmp(arg, "mode") == 0) {
        char* value = term.getNext();
        if (value == nullptr) {
            Serial.printf("Core mode: %s (%d)\r\n", modeName().c_str(), core_state.getMode());
            Serial.print("Modes:");
            for (uint8_t i = 0; i < CORE_MODE_COUNT; i++) {
                const char* n = core_state.enumNameByIndex("system.mode", i);
                Serial.printf(" %s=%u", (n != nullptr) ? n : "?", (unsigned)i);
            }
            Serial.println();
            return;
        }
        int m = core_state.enumIndexByName("system.mode", value);
        if (m < 0) {
            long v = 0;
            if (ns_core_terminal::parseIntStrict(String(value), v, 0, CORE_MODE_COUNT - 1)) {
                m = (int)v;
            }
        }
        if (m < 0 || m >= CORE_MODE_COUNT) {
            Serial.println("Usage: sys mode [init|normal|ota|fs_update|prog|test|<0..5>]");
            return;
        }
        int rc = core_state.requestMode(m);
        if (rc == 0) {
            Serial.printf("Core mode set: %s (%d)\r\n", modeName().c_str(), core_state.getMode());
        } else {
            Serial.printf("Core mode set failed: rc=%d\r\n", rc);
        }
        return;
    }

    Serial.printf("Build:    %s\r\n", BUILD_ENV);
    Serial.printf("Device:   %s\r\n", core_sys.getDeviceName().c_str());
    Serial.printf("Serial:   %s\r\n", core_sys.getDeviceSerial().c_str());
    Serial.printf("Uptime:   %s\r\n",
                  ns_core_terminal::formatUptime(millis() / 1000UL).c_str());

#if defined(ESP32)
    Serial.printf("CPU:      %u MHz, %u core(s)\r\n",
                  (unsigned)ESP.getCpuFreqMHz(), (unsigned)ESP.getChipCores());
    Serial.printf("RAM:      free %lu, min %lu\r\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMinFreeHeap());
#endif
#if defined(ESP8266)
    Serial.printf("CPU:      %u MHz, 1 core\r\n", (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("RAM:      free %lu, max block %lu, frag %u%%\r\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getMaxFreeBlockSize(),
                  (unsigned)ESP.getHeapFragmentation());
#endif

    if (core_sys._fs == nullptr) {
        Serial.println("FS:       not mounted");
    } else {
#if defined(ESP32)
        uint32_t total = (uint32_t)core_sys._fs->totalBytes();
        uint32_t used  = (uint32_t)core_sys._fs->usedBytes();
#endif
#if defined(ESP8266)
        FSInfo info;
        uint32_t total = 0;
        uint32_t used  = 0;
        if (core_sys._fs->info(info)) {
            total = (uint32_t)info.totalBytes;
            used  = (uint32_t)info.usedBytes;
        }
#endif
        Serial.printf("FS:       %lu used / %lu total, %lu free\r\n",
                      (unsigned long)used, (unsigned long)total,
                      (unsigned long)(total - used));
    }

    Serial.printf("Mode:     %s, safe: %s\r\n",
                  modeName().c_str(),
                  core_state.getBool("system.safe", false) ? "yes" : "no");

    char tbuf[24];
    ns_core_sys::timeFormatNowStr(core_sys.timeNow(), tbuf, sizeof(tbuf));
    const char* src = core_sys.timeSourceName();
    Serial.printf("Time:     %s (%s, %s)\r\n", tbuf,
                  (src != nullptr && src[0] != '\0') ? src : "internal",
                  core_sys.timeValid() ? "valid"
                                       : (core_sys.timeFallback() ? "fallback" : "invalid"));

    printWifiState();
}

// ============================================================
// info
// ============================================================

void CLASS_CORE_SYS::s_cmdInfo() {
    Serial.printf("Device name:   %s\r\n", core_sys.getDeviceName().c_str());
    Serial.printf("Device serial: %s\r\n", core_sys.getDeviceSerial().c_str());

#if defined(ESP32)
    Serial.printf("Chip:          %s rev %u\r\n",
                  ESP.getChipModel(), (unsigned)ESP.getChipRevision());
    uint64_t chipId = ESP.getEfuseMac();
    Serial.printf("Chip id:       %08lX%08lX\r\n",
                  (unsigned long)(chipId >> 32),
                  (unsigned long)(chipId & 0xFFFFFFFFULL));
    Serial.printf("Cores:         %u\r\n", (unsigned)ESP.getChipCores());
    Serial.printf("Flash:         %lu bytes\r\n", (unsigned long)ESP.getFlashChipSize());
#endif
#if defined(ESP8266)
    Serial.printf("Chip id:       %08X\r\n", (unsigned)ESP.getChipId());
    Serial.printf("Core:          %s\r\n", ESP.getCoreVersion().c_str());
    Serial.printf("Cores:         1\r\n");
    Serial.printf("Flash:         %lu bytes\r\n", (unsigned long)ESP.getFlashChipRealSize());
#endif

    Serial.printf("CPU:           %u MHz\r\n", (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("SDK:           %s\r\n", ESP.getSdkVersion());
    Serial.printf("MAC:           %s\r\n", WiFi.macAddress().c_str());
    Serial.printf("Reset reason:  %s\r\n", core_sys.getResetReason().c_str());
}

// ============================================================
// ver
// ============================================================

void CLASS_CORE_SYS::s_cmdVer() {
    Serial.printf("Firmware: %s\r\n", FIRMWARE_VERSION);
    Serial.printf("FS:       %s\r\n", core_sys.getFsVersionStr().c_str());
    Serial.printf("Env:      %s\r\n", BUILD_ENV);
    Serial.printf("Git:      %s %s (%s)\r\n", GIT_BRANCH, GIT_COMMIT,
                  GIT_DIRTY ? "dirty" : "clean");

    uint8_t count = 0;
    const CompiledModuleInfo* list = modulesCompiledInfo(count);
    if (list == nullptr || count == 0) {
        Serial.println("Components: no metadata available.");
        return;
    }

    Serial.println("\r\nComponents:");
    Serial.println("NAME                   KIND      VERSION DATE             HASH");
    for (uint8_t i = 0; i < count; i++) {
        const CompiledModuleInfo& m = list[i];
        String hash = (m.commitHash != nullptr) ? String(m.commitHash) : String("-");
        if (hash.length() > 7) { hash = hash.substring(0, 7); }
        Serial.printf("%-22s %-9s %-7s %-16s %s\r\n",
                      (m.name != nullptr) ? m.name : "?",
                      (m.kind != nullptr) ? m.kind : "?",
                      (m.version != nullptr) ? m.version : "-",
                      (m.commitDate != nullptr) ? m.commitDate : "-",
                      hash.c_str());
    }
}

// ============================================================
// stat
// ============================================================

void CLASS_CORE_SYS::s_cmdStat() {
    Serial.printf("Core mode: %s, safe: %s\r\n",
                  modeName().c_str(),
                  core_state.getBool("system.safe", false) ? "yes" : "no");

    uint8_t count = 0;
    const CompiledModuleInfo* list = modulesCompiledInfo(count);
    if (list == nullptr || count == 0) {
        Serial.println("No component metadata available.");
        return;
    }

    Serial.println("NAME                   KIND      MODE     PRIO  FLAGS");
    for (uint8_t i = 0; i < count; i++) {
        const CompiledModuleInfo& m = list[i];

        String mode = "-";
        // system.mode — режим ядра (показан в заголовке), а не режим модуля.
        if (m.ns != nullptr && m.ns[0] != '\0' && strcmp(m.ns, "system") != 0) {
            String full = String(m.ns) + ".mode";
            if (core_state.has(full.c_str())) {
                int v = core_state.getInt(full.c_str(), -1);
                const char* n = core_state.enumNameByIndex(full.c_str(), v);
                mode = (n != nullptr) ? String(n) : String(v);
            }
        }

        String flags;
        if (m.res)        { flags += "res "; }
        if (m.web)        { flags += "web "; }
        if (m.loop)       { flags += "loop "; }
        if (m.timeSource) { flags += "time "; }
        if (TerminalHasModule(m.name)) { flags += "term "; }
        flags.trim();
        if (flags.length() == 0) { flags = "-"; }

        Serial.printf("%-22s %-9s %-8s %4d  %s\r\n",
                      (m.name != nullptr) ? m.name : "?",
                      (m.kind != nullptr) ? m.kind : "?",
                      mode.c_str(), (int)m.prio, flags.c_str());
    }
}

// ============================================================
// name / serial / id
// ============================================================

static void printIdentity(CLASS_CORE_SYS& self) {
    Serial.printf("Device name:   %s\r\n", self.getDeviceName().c_str());
    Serial.printf("Device serial: %s\r\n", self.getDeviceSerial().c_str());
}

void CLASS_CORE_SYS::s_cmdName() {
    String value = TerminalGetArgLine();
    if (value.length() == 0) {
        Serial.printf("Device name: %s\r\n", core_sys.getDeviceName().c_str());
        return;
    }
    if (!ns_core_sys::terminalValidateIdent(value)) {
        Serial.println("Name: 1..63 chars, A-Za-z0-9-_ only.");
        return;
    }
    core_sys.setDeviceName(value);
    bool nvs = core_sys.saveSysIdentStore();
    bool fs  = core_sys.save_configSys();
    Serial.printf("Device name saved%s%s.\r\n",
                  nvs ? "" : " (NVRAM failed)", fs ? "" : " (FS failed)");
    Serial.println("Reboot required to apply hostname/mDNS/AP SSID.");
}

void CLASS_CORE_SYS::s_cmdSerial() {
    String value = TerminalGetArgLine();
    if (value.length() == 0) {
        Serial.printf("Device serial: %s\r\n", core_sys.getDeviceSerial().c_str());
        return;
    }
    if (!ns_core_sys::terminalValidateIdent(value)) {
        Serial.println("Serial: 1..63 chars, A-Za-z0-9-_ only.");
        return;
    }
    core_sys.setDeviceSerial(value);
    bool nvs = core_sys.saveSysIdentStore();
    bool fs  = core_sys.save_configSys();
    Serial.printf("Device serial saved%s%s.\r\n",
                  nvs ? "" : " (NVRAM failed)", fs ? "" : " (FS failed)");
    Serial.println("Reboot required to apply hostname/mDNS/AP SSID.");
}

void CLASS_CORE_SYS::s_cmdId() {
    printIdentity(core_sys);
}

// ============================================================
// auth
// ============================================================

void CLASS_CORE_SYS::s_cmdAuth() {
    char* sub = term.getNext();
    if (sub == nullptr) {
        Serial.printf("auth:  %s\r\n", core_sys._httpAuth.auth ? "on" : "off");
        Serial.printf("user:  %s\r\n", core_sys._httpAuth.wwwUsername.c_str());
        Serial.printf("pass:  %s\r\n", core_sys._httpAuth.wwwPassword.c_str());
        Serial.printf("secq:  %s\r\n", core_sys._httpAuth.wwwQuestion.c_str());
        Serial.printf("seca:  %s\r\n", core_sys._httpAuth.wwwAnswer.c_str());
        return;
    }

    if (strcmp(sub, "help") == 0) {
        TerminalPrintText(kAuthHelp);
        return;
    }

    if (strcmp(sub, "user") == 0) {
        String value = TerminalGetArgLine();
        if (!ns_core_sys::terminalValidateAuthUser(value)) {
            Serial.println("User: 1..32 chars, A-Za-z0-9._- only.");
            return;
        }
        core_sys._httpAuth.wwwUsername = value;
        bool ok = core_sys.saveHTTPAuth();
        Serial.printf("Web auth user saved%s.\r\n", ok ? "" : " (save failed)");
        if (core_sys._httpAuth.auth) { printAuthRebootNote(); }
        return;
    }

    if (strcmp(sub, "pass") == 0) {
        String value = TerminalGetArgLine();
        if (!ns_core_sys::isAdminPassValid(value)) {
            Serial.println("Password: 8..63 chars, A-Za-z0-9 only.");
            return;
        }
        core_sys._httpAuth.wwwPassword = value;
        bool ok = core_sys.saveHTTPAuth();
        Serial.printf("Web auth password saved%s.\r\n", ok ? "" : " (save failed)");
        if (core_sys._httpAuth.auth) { printAuthRebootNote(); }
        return;
    }

    if (strcmp(sub, "on") == 0) {
        if (core_sys._httpAuth.wwwUsername.length() == 0) {
            Serial.println("Set a user first: auth user <name>.");
            return;
        }
        if (!ns_core_sys::isAdminPassValid(core_sys._httpAuth.wwwPassword)) {
            Serial.println("Set a valid password first: auth pass <8..63, A-Za-z0-9>.");
            return;
        }
        core_sys._httpAuth.auth = true;
        bool ok = core_sys.saveHTTPAuth();
        Serial.printf("Web auth enabled%s.\r\n", ok ? "" : " (save failed)");
        printAuthRebootNote();
        return;
    }

    if (strcmp(sub, "off") == 0) {
        TerminalConfirm("Disable web auth (user/password kept)?", &CLASS_CORE_SYS::s_authOffYes);
        return;
    }

    if (strcmp(sub, "reset") == 0) {
        TerminalConfirm("Disable web auth and clear user/password?", &CLASS_CORE_SYS::s_authResetYes);
        return;
    }

    Serial.printf("Unknown auth subcommand '%s'. Type 'auth help'.\r\n", sub);
}

void CLASS_CORE_SYS::s_authOffYes() {
    core_sys._httpAuth.auth = false;
    bool ok = core_sys.saveHTTPAuth();
    Serial.printf("Web auth disabled%s.\r\n", ok ? "" : " (save failed)");
    printAuthRebootNote();
}

void CLASS_CORE_SYS::s_authResetYes() {
    core_sys._httpAuth.auth = false;
    core_sys._httpAuth.wwwUsername = "";
    core_sys._httpAuth.wwwPassword = "";
    bool ok = core_sys.saveHTTPAuth();
    Serial.printf("Web auth reset (user/password cleared)%s.\r\n", ok ? "" : " (save failed)");
    printAuthRebootNote();
}

// ============================================================
// reset
// ============================================================

void CLASS_CORE_SYS::s_cmdReset() {
    TerminalConfirm("Reboot device?", &CLASS_CORE_SYS::s_doReset);
}

void CLASS_CORE_SYS::s_doReset() {
    Serial.println("Rebooting...");
    delay(500);
#if defined(ESP32)
    ESP.restart();
#endif
#if defined(ESP8266)
    ESP.reset();
#endif
}
