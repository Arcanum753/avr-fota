#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <IPAddress.h>
#include <stdio.h>
#include <string.h>

#include "core_wifi/core_wifi.h"
#include "core_wifi/core_wifi_types.h"
#include "core_json/core_json.h"
#include "core_state/core_state.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Терминальные команды core_wifi
// ============================================================

static const char* const kWifiCommands[] = {
    "show", "slot", "set", "sys", "apply", "connect", "clients", nullptr
};

static const char kWifiHelp[] PROGMEM =
    "wifi - Wi-Fi configuration\r\n"
    "  wifi show                         current state and slots\r\n"
    "  wifi slot <0..3>                  show slot (password in plain text)\r\n"
    "  wifi set <0..3> ssid <v>          set SSID (empty disables slot)\r\n"
    "  wifi set <0..3> pass <v>          set password (empty = open)\r\n"
    "  wifi set <0..3> dhcp on|off       DHCP\r\n"
    "  wifi set <0..3> ip|mask|gw|dns <a.b.c.d>\r\n"
    "  wifi sys scantime <1..60>         scan budget, minutes\r\n"
    "  wifi sys aptime <0..1440>         AP lifetime, minutes (0 = no AP)\r\n"
    "  wifi sys scan_retries <5..50>     empty scans before AP fallback\r\n"
    "  wifi sys busmode auto|macro       module mode\r\n"
    "  wifi apply                        apply last edited/active slot\r\n"
    "  wifi connect <0..3>               prefer slot on next connect (auto)\r\n"
    "  wifi clients                      AP clients (MAC/IP)\r\n"
    "  wifi help                         this help\r\n";

static String slotPath(int n) {
    return String("/config_wifi") + String(n) + ".json";
}

static void slotDefaults(strWifiConfig& c) {
    c.ssid = "";
    c.password = "";
    c.dhcp = true;
    c.ip = IPAddress(192, 168, 1, 4);
    c.netmask = IPAddress(255, 255, 255, 0);
    c.gateway = IPAddress(192, 168, 1, 1);
    c.dns = IPAddress(192, 168, 1, 1);
}

static bool slotLoad(int n, strWifiConfig& c) {
    slotDefaults(c);
    return core_json.jsonFileLoadSlot(slotPath(n), c.ssid, c.password, c.dhcp,
                                      c.ip, c.netmask, c.gateway, c.dns);
}

static bool slotSave(int n, const strWifiConfig& c) {
    return core_json.jsonFileSaveSlot(slotPath(n), c.ssid, c.password, c.dhcp,
                                      c.ip, c.netmask, c.gateway, c.dns);
}

static void copySsid(char* dst, size_t cap, const String& ssid) {
    if (cap == 0) { return; }
    snprintf(dst, cap, "%s", ssid.c_str());
}

static const char* statusName(enWifiStatus st) {
    if (st == FS_STAT_CONNECTING)     { return "connecting"; }
    if (st == FS_STAT_CONNECTED)      { return "connected"; }
    if (st == FS_STAT_APMODE)         { return "ap"; }
    if (st == FS_STAT_DISCONNECTED)   { return "disconnected"; }
    if (st == FS_STAT_RESET)          { return "reset"; }
    if (st == FS_STAT_WRONGPASSWORDS) { return "wrong password"; }
    return "unknown";
}

static String ipText(const IPAddress& ip) {
    return ip.toString();
}

void CLASS_CORE_WIFI::terminalRegister() {
    TerminalRegisterModule("core_wifi", "wifi", "Wi-Fi configuration",
                           &CLASS_CORE_WIFI::s_terminalCmd, kWifiCommands, kWifiHelp, true);
}

void CLASS_CORE_WIFI::s_terminalCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) {
        TerminalPrintText(kWifiHelp);
        return;
    }

    if (strcmp(sub, "show") == 0) {
        Serial.printf("Mode:    %s\r\n", (core_wifi._busMode == WIFI_MODE_MACRO) ? "macro" : "auto");
        Serial.printf("Target:  %s\r\n",
                      (core_wifi._target == WIFI_TARGET_AP) ? "ap"
                      : (core_wifi._target == WIFI_TARGET_STA) ? "sta" : "auto");
        Serial.printf("Status:  %s\r\n", statusName(core_wifi.wifiStatus));

        bool ap = core_state.getBool("wifi.ap_mode", false);
        if (ap) {
            Serial.printf("AP SSID: %s\r\n", core_state.getStr("wifi.ap_ssid", "").c_str());
            Serial.printf("AP IP:   %s\r\n", core_state.getStr("wifi.ap_ip", "").c_str());
            Serial.printf("AP clients: %d\r\n", (int)core_state.getInt("wifi.ap_clients", 0));
        } else if (core_state.getBool("wifi.connected", false)) {
            Serial.printf("SSID:    %s\r\n", core_state.getStr("wifi.slot_name", "").c_str());
            Serial.printf("IP:      %s\r\n", core_state.getStr("wifi.ip", "").c_str());
            Serial.printf("RSSI:    %d dBm\r\n", (int)core_state.getInt("wifi.rssi", 0));
            Serial.printf("DHCP:    %s\r\n", core_state.getBool("wifi.dhcp", true) ? "on" : "off");
        } else {
            Serial.println("STA:     not connected");
        }

        Serial.printf("Scan budget: %u min, AP lifetime: %u min, scan_retries: %u\r\n",
                      (unsigned)core_wifi._wifiScanTime, (unsigned)core_wifi._wifiAPLifeTime,
                      (unsigned)core_wifi._scanRetries);
        Serial.println("Slots:");
        for (int i = 0; i < 4; i++) {
            strWifiConfig c;
            bool ok = slotLoad(i, c);
            Serial.printf("  %d: %-32s %s%s\r\n", i,
                          c.ssid.length() ? c.ssid.c_str() : "(empty)",
                          c.dhcp ? "dhcp" : "static",
                          ok ? "" : " (default)");
        }
        return;
    }

    if (strcmp(sub, "slot") == 0) {
        char* nArg = term.getNext();
        long n = 0;
        if (nArg == nullptr || !ns_core_terminal::parseIntStrict(String(nArg), n, 0, 3)) {
            Serial.println("Usage: wifi slot <0..3>");
            return;
        }
        strWifiConfig c;
        if (!slotLoad((int)n, c)) {
            Serial.println("Slot file not found, defaults shown.");
        }
        Serial.printf("Slot %ld:\r\n", n);
        Serial.printf("  ssid:    %s\r\n", c.ssid.c_str());
        Serial.printf("  pass:    %s\r\n", c.password.c_str());
        Serial.printf("  dhcp:    %s\r\n", c.dhcp ? "on" : "off");
        Serial.printf("  ip:      %s\r\n", ipText(c.ip).c_str());
        Serial.printf("  mask:    %s\r\n", ipText(c.netmask).c_str());
        Serial.printf("  gateway: %s\r\n", ipText(c.gateway).c_str());
        Serial.printf("  dns:     %s\r\n", ipText(c.dns).c_str());
        return;
    }

    if (strcmp(sub, "set") == 0) {
        char* nArg = term.getNext();
        long n = 0;
        if (nArg == nullptr || !ns_core_terminal::parseIntStrict(String(nArg), n, 0, 3)) {
            Serial.println("Usage: wifi set <0..3> <ssid|pass|dhcp|ip|mask|gw|dns> <v>");
            return;
        }
        char* field = term.getNext();
        if (field == nullptr) {
            Serial.println("Usage: wifi set <0..3> <ssid|pass|dhcp|ip|mask|gw|dns> <v>");
            return;
        }
        String value;
        if (strcmp(field, "ssid") == 0 || strcmp(field, "pass") == 0) {
            value = TerminalGetArgLine();
        } else {
            char* rest = term.getRemaining();
            value = ns_core_terminal::unquote(String(rest != nullptr ? rest : ""));
        }
        if (value.length() == 0 && strcmp(field, "ssid") != 0 && strcmp(field, "pass") != 0) {
            Serial.println("Missing value.");
            return;
        }

        strWifiConfig c;
        slotLoad((int)n, c);

        if (strcmp(field, "ssid") == 0) {
            if (value.length() > 32) { Serial.println("SSID max 32 characters."); return; }
            c.ssid = value;
        } else if (strcmp(field, "pass") == 0) {
            if (value.length() > 63) { Serial.println("Password max 63 characters."); return; }
            c.password = value;
        } else if (strcmp(field, "dhcp") == 0) {
            bool on = false;
            if (!ns_core_terminal::parseOnOff(value, on)) {
                Serial.println("Usage: wifi set <0..3> dhcp on|off");
                return;
            }
            c.dhcp = on;
        } else if (strcmp(field, "ip") == 0 || strcmp(field, "mask") == 0 ||
                   strcmp(field, "gw") == 0 || strcmp(field, "dns") == 0) {
            IPAddress ip;
            if (!ip.fromString(value.c_str())) {
                Serial.println("Invalid IPv4 address.");
                return;
            }
            if (strcmp(field, "ip") == 0)      { c.ip = ip; }
            if (strcmp(field, "mask") == 0)    { c.netmask = ip; }
            if (strcmp(field, "gw") == 0)      { c.gateway = ip; }
            if (strcmp(field, "dns") == 0)     { c.dns = ip; }
        } else {
            Serial.printf("Unknown field '%s'.\r\n", field);
            return;
        }

        if (!c.dhcp && (c.ip == IPAddress(0, 0, 0, 0) ||
                        c.netmask == IPAddress(0, 0, 0, 0) ||
                        c.gateway == IPAddress(0, 0, 0, 0))) {
            Serial.println("Static mode requires ip, mask and gw.");
            return;
        }

        if (!slotSave((int)n, c)) {
            Serial.println("Failed to save slot.");
            return;
        }
        if (n == 0) { copySsid(core_wifi._strWifi0, sizeof(core_wifi._strWifi0), c.ssid); }
        if (n == 1) { copySsid(core_wifi._strWifi1, sizeof(core_wifi._strWifi1), c.ssid); }
        if (n == 2) { copySsid(core_wifi._strWifi2, sizeof(core_wifi._strWifi2), c.ssid); }
        if (n == 3) { copySsid(core_wifi._strWifi3, sizeof(core_wifi._strWifi3), c.ssid); }
        core_wifi._wifiFailCount[n] = 0;
        core_wifi._lastTouchedSlot = (int8_t)n;
        if ((int8_t)n == core_wifi._activeSlot) {
            core_wifi._activeDhcp = c.dhcp;
            core_state.signal("wifi.dhcp", BusValue::bo(core_wifi._activeDhcp));
        }
        Serial.printf("Slot %ld saved. Run 'wifi apply' to reconnect.\r\n", n);
        return;
    }

    if (strcmp(sub, "sys") == 0) {
        char* field = term.getNext();
        char* valArg = term.getNext();
        if (field == nullptr || valArg == nullptr) {
            Serial.println("Usage: wifi sys <scantime|aptime|scan_retries|busmode> <v>");
            return;
        }
        String value(valArg);

        if (strcmp(field, "scantime") == 0) {
            long v = 0;
            if (!ns_core_terminal::parseIntStrict(value, v, 1, 60)) {
                Serial.println("scantime: 1..60 minutes.");
                return;
            }
            core_wifi._wifiScanTime = (uint16_t)v;
            core_wifi.scanTime = core_wifi._wifiScanTime * MINUTES;
        } else if (strcmp(field, "aptime") == 0) {
            long v = 0;
            if (!ns_core_terminal::parseIntStrict(value, v, 0, 1440)) {
                Serial.println("aptime: 0..1440 minutes.");
                return;
            }
            core_wifi._wifiAPLifeTime = (uint16_t)v;
        } else if (strcmp(field, "scan_retries") == 0) {
            long v = 0;
            if (!ns_core_terminal::parseIntStrict(value, v, WIFI_SCAN_RETRIES_MIN, WIFI_SCAN_RETRIES_MAX)) {
                Serial.printf("scan_retries: %d..%d.\r\n", WIFI_SCAN_RETRIES_MIN, WIFI_SCAN_RETRIES_MAX);
                return;
            }
            core_wifi.setScanRetries((uint8_t)v);
        } else if (strcmp(field, "busmode") == 0) {
            value.toLowerCase();
            if (value == "auto") {
                core_wifi.setWifiMode(WIFI_MODE_AUTO);
            } else if (value == "macro") {
                core_wifi.setWifiMode(WIFI_MODE_MACRO);
#if !defined(MODULE_MACROS)
                Serial.println("Warning: MODULE_MACROS is not built; macro mode has no driver.");
#endif
            } else {
                Serial.println("busmode: auto|macro.");
                return;
            }
        } else {
            Serial.printf("Unknown sys field '%s'.\r\n", field);
            return;
        }

        bool saved = core_wifi.save_configWifiSys();
        Serial.printf("Wi-Fi sys '%s' set%s.\r\n", field, saved ? "" : " (save failed)");
        return;
    }

    if (strcmp(sub, "apply") == 0) {
        int n = (core_wifi._lastTouchedSlot >= 0) ? core_wifi._lastTouchedSlot
                : (core_wifi._activeSlot >= 0) ? core_wifi._activeSlot : 0;
        strWifiConfig c;
        if (!slotLoad(n, c) || c.ssid.length() == 0) {
            Serial.printf("Slot %d is empty; nothing to apply.\r\n", n);
            return;
        }
        core_wifi._wifiConfig = c;
        if (n == 0) { copySsid(core_wifi._strWifi0, sizeof(core_wifi._strWifi0), c.ssid); }
        if (n == 1) { copySsid(core_wifi._strWifi1, sizeof(core_wifi._strWifi1), c.ssid); }
        if (n == 2) { copySsid(core_wifi._strWifi2, sizeof(core_wifi._strWifi2), c.ssid); }
        if (n == 3) { copySsid(core_wifi._strWifi3, sizeof(core_wifi._strWifi3), c.ssid); }
        core_wifi._lastTouchedSlot = (int8_t)n;
        core_wifi._forcedSlot = (int8_t)n;
        core_wifi._activeDhcp = c.dhcp;
        core_state.signal("wifi.dhcp", BusValue::bo(core_wifi._activeDhcp));
        core_wifi._applyWifiPending = true;
        Serial.printf("Applying slot %d (%s).\r\n", n, c.ssid.c_str());
        return;
    }

    if (strcmp(sub, "connect") == 0) {
        char* nArg = term.getNext();
        long n = 0;
        if (nArg == nullptr || !ns_core_terminal::parseIntStrict(String(nArg), n, 0, 3)) {
            Serial.println("Usage: wifi connect <0..3>");
            return;
        }
        if (core_wifi._busMode == WIFI_MODE_MACRO) {
            Serial.println("wifi connect requires auto mode: run 'wifi sys busmode auto'.");
            return;
        }
        strWifiConfig c;
        if (!slotLoad((int)n, c) || c.ssid.length() == 0) {
            Serial.printf("Slot %ld is empty.\r\n", n);
            return;
        }
        core_wifi._wifiConfig = c;
        if (n == 0) { copySsid(core_wifi._strWifi0, sizeof(core_wifi._strWifi0), c.ssid); }
        if (n == 1) { copySsid(core_wifi._strWifi1, sizeof(core_wifi._strWifi1), c.ssid); }
        if (n == 2) { copySsid(core_wifi._strWifi2, sizeof(core_wifi._strWifi2), c.ssid); }
        if (n == 3) { copySsid(core_wifi._strWifi3, sizeof(core_wifi._strWifi3), c.ssid); }
        core_wifi._lastTouchedSlot = (int8_t)n;
        core_wifi._activeSlot = (int8_t)n;
        core_wifi._activeDhcp = c.dhcp;
        core_wifi._forcedSlot = (int8_t)n;
        core_state.signal("wifi.dhcp", BusValue::bo(core_wifi._activeDhcp));
        core_wifi._applyWifiPending = true;
        Serial.printf("Connecting to slot %ld (%s).\r\n", n, c.ssid.c_str());
        return;
    }

    if (strcmp(sub, "clients") == 0) {
        if (!core_state.getBool("wifi.ap_mode", false)) {
            Serial.println("Not in AP mode.");
            return;
        }
        String list = core_wifi.apClientsText();
        if (list.length() == 0) {
            Serial.println("No AP clients.");
        } else {
            Serial.print(list);
        }
        return;
    }

    Serial.printf("Unknown wifi subcommand '%s'. Type 'wifi help'.\r\n", sub);
}
