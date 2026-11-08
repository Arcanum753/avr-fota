#include "core_web/FSWebServerLib.h"

#include <Arduino.h>
#include <string.h>

#include "core_ntp/core_ntp.h"
#include "core_ntp/common_module.h"

#include "core_terminal/core_terminal.h"
#include "core_terminal/common_module.h"

// ============================================================
// Терминальные команды core_ntp
// ============================================================

static const char* const kNtpCommands[] = { "show", "period", "server", "sync", nullptr };

static const char kNtpHelp[] PROGMEM =
    "ntp - NTP client\r\n"
    "  ntp show                 period, servers, last sync, status\r\n"
    "  ntp period <0..720>      minutes; 0 disables NTP\r\n"
    "  ntp server <0|1|2> <h>   server 0 is required, 1/2 may be empty\r\n"
    "  ntp sync                 request an immediate sync\r\n"
    "  ntp help                 this help\r\n";

void CLASS_CORE_NTP::terminalRegister() {
    TerminalRegisterModule("core_ntp", "ntp", "NTP client",
                           &CLASS_CORE_NTP::s_terminalCmd, kNtpCommands, kNtpHelp, true);
}

bool CLASS_CORE_NTP::forceSync() {
    if (!updateTimeFromNTP || !_ntpStarted) { return false; }
    // getTime() асинхронный: инициирует запрос, результат приходит в колбэк.
    (void)NTP.getTime();
    return true;
}

void CLASS_CORE_NTP::s_terminalCmd() {
    char* sub = term.getNext();
    if (sub == nullptr || strcmp(sub, "help") == 0) {
        TerminalPrintText(kNtpHelp);
        return;
    }

    if (strcmp(sub, "show") == 0) {
        Serial.printf("NTP period: %ld min (%s)\r\n",
                      core_ntp._ntpConfig.updateNTPTimeEvery,
                      (core_ntp._ntpConfig.updateNTPTimeEvery > 0) ? "enabled" : "disabled");
        Serial.printf("Servers:  0=%s\r\n", core_ntp._ntpConfig.ntpServerName0.c_str());
        Serial.printf("          1=%s\r\n", core_ntp._ntpConfig.ntpServerName1.c_str());
        Serial.printf("          2=%s\r\n", core_ntp._ntpConfig.ntpServerName2.c_str());
        Serial.printf("Active:   %s\r\n", core_ntp._ntpServerNow.c_str());
        uint32_t last = core_ntp.ntpLastSyncEpoch();
        if (last > 0) {
            Serial.printf("Last sync: %lu (%s)\r\n", (unsigned long)last,
                          core_ntp.ntpTimeFresh() ? "fresh" : "stale");
        } else {
            Serial.println("Last sync: never");
        }
        const char* status = core_ntp.ntpStatusText();
        Serial.printf("Status:   %s\r\n", (status != nullptr && status[0] != '\0')
                                              ? status : "ok");
        Serial.printf("Started:  %s\r\n", core_ntp._ntpStarted ? "yes" : "no");
        return;
    }

    if (strcmp(sub, "period") == 0) {
        char* arg = term.getNext();
        long period = 0;
        if (arg == nullptr ||
            !ns_core_terminal::parseIntStrict(String(arg), period, 0, NTP_PERIOD_MAX_MIN)) {
            Serial.printf("Usage: ntp period <0..%d>\r\n", NTP_PERIOD_MAX_MIN);
            return;
        }
        core_ntp._ntpConfig.updateNTPTimeEvery = period;
        core_ntp._pendingApply = true;
        bool saved = core_ntp.save_configNTP();
        Serial.printf("NTP period set to %ld min%s.\r\n", period,
                      saved ? "" : " (save failed)");
        return;
    }

    if (strcmp(sub, "server") == 0) {
        char* idxArg = term.getNext();
        long idx = 0;
        if (idxArg == nullptr ||
            !ns_core_terminal::parseIntStrict(String(idxArg), idx, 0, NTP_SERVER_COUNT - 1)) {
            Serial.println("Usage: ntp server <0|1|2> <host>");
            return;
        }
        String host = TerminalGetArgLine();
        if (!ns_core_ntp::ntpServerNameValid(host, idx == 0)) {
            Serial.printf("Invalid NTP server name (length 1..%d).\r\n", NTP_SERVER_NAME_MAX);
            return;
        }
        if (idx == 0) { core_ntp._ntpConfig.ntpServerName0 = host; }
        if (idx == 1) { core_ntp._ntpConfig.ntpServerName1 = host; }
        if (idx == 2) { core_ntp._ntpConfig.ntpServerName2 = host; }
        core_ntp._pendingApply = true;
        bool saved = core_ntp.save_configNTP();
        Serial.printf("NTP server %ld set to '%s'%s.\r\n", idx, host.c_str(),
                      saved ? "" : " (save failed)");
        return;
    }

    if (strcmp(sub, "sync") == 0) {
        if (!core_ntp.forceSync()) {
            Serial.println("NTP is not running (period 0 or provider not started).");
            return;
        }
        Serial.println("NTP sync requested.");
        return;
    }

    Serial.printf("Unknown ntp subcommand '%s'. Type 'ntp help'.\r\n", sub);
}
