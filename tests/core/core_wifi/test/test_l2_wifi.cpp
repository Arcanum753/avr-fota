#include "override_prelude.h"
#include "IPAddress.h"
#include "WiFi.h"
#include "ESP.h"
#include "core_state/core_state.h"

// Тестовый доступ к private/protected членам автомата Wi-Fi.
#define private public
#define protected public
#include "core_wifi/core_wifi.h"
#undef private
#undef protected

#include "../../../../src/core_sys/eertos.cpp"
#include "../../../../src/core_led/core_led_engine.cpp"
#include "../../../../src/core_led/common_module.cpp"
#include "../../../../src/core_state/core_state_engine.cpp"
#include "../../../../src/core_state/common_module.cpp"
#include "../../../../src/core_wifi/core_wifi_engine.cpp"

#include <unity.h>

// Глобальные объекты, обычно определяемые вне engine-файла.
CLASS_CORE_SYS core_sys;
CLASS_CORE_NTP core_ntp;
CLASS_CORE_STATE core_state;
CLASS_CORE_WIFI core_wifi;

// load_configWifi живёт в core_wifi.cpp (web/конфиг) — не компилируется в тестах.
bool CLASS_CORE_WIFI::load_configWifi(int) { return true; }

void setUp(void) {
    WiFi.reset();
    mockEspRestartReset();
    core_ntp = CLASS_CORE_NTP();
    core_wifi.wifiStatus = FS_STAT_CONNECTING;
    core_wifi.WifiScan = WF_STAT_SCANING;
    core_wifi._wifiFailCount[0] = core_wifi._wifiFailCount[1] = 0;
    core_wifi._wifiFailCount[2] = core_wifi._wifiFailCount[3] = 0;
    core_wifi._wifiInitFailCount = 0;
    core_wifi._stateSeconds = 0;
    core_wifi._nextStaScanAt = 0;
    core_wifi._scanActive = false;
    core_wifi._scanStartedAt = 0;
    core_wifi._apScanPhaseUntil = 0;
    core_wifi._applyWifiPending = false;
    core_wifi._enterApPending = false;
    core_wifi._ignoreDisconnect = false;
    core_wifi._suppressDisc = 0;
    core_wifi._wifiAPLifeTime = 5;
    core_wifi.scanTime = 1;
    core_wifi._strWifi0[0] = core_wifi._strWifi1[0] = 0;
    core_wifi._strWifi2[0] = core_wifi._strWifi3[0] = 0;
    core_state.setMode(CORE_MODE_NORMAL);
}
void tearDown(void) {}

// ============================================================
// staTick: WIFI_SCAN_FAILED -> бэкофф / рестарт / отсрочка
// ============================================================
static void test_scan_failed_backoff(void) {
    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_wifi._scanActive = false;
    // Драйвер не принял запуск скана.
    WiFi.setScanStartResult(WIFI_SCAN_FAILED);
    core_wifi.staTick();
    TEST_ASSERT_EQUAL_UINT8(1, core_wifi._wifiInitFailCount);
    TEST_ASSERT_EQUAL_UINT32(WIFI_INIT_FAIL_PAUSE_SEC, core_wifi._nextStaScanAt);
    TEST_ASSERT_FALSE(core_wifi._scanActive);
    TEST_ASSERT_EQUAL_INT(0, mockEspRestartCount());
}

// Ложный таймаут библиотеки: скан уже запущен, scanComplete() вернул FAILED,
// но драйвер ещё сканирует — результат нельзя выбрасывать.
static void test_scan_failed_within_grace_keeps_waiting(void) {
    core_wifi._stateSeconds = 10;
    core_wifi.staTick();                 // запуск скана (RUNNING)
    TEST_ASSERT_TRUE(core_wifi._scanActive);
    TEST_ASSERT_EQUAL_UINT32(10, core_wifi._scanStartedAt);

    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_wifi._stateSeconds = 16;        // 6 с: внутренний таймаут библиотеки
    core_wifi.staTick();
    TEST_ASSERT_EQUAL_UINT8(0, core_wifi._wifiInitFailCount);
    TEST_ASSERT_TRUE(core_wifi._scanActive);
    TEST_ASSERT_EQUAL_UINT32(0, core_wifi._nextStaScanAt);
}

// После ложного таймаута скан всё-таки завершился — подключаемся, не теряя результат.
static void test_late_scan_result_after_library_timeout(void) {
    WiFi.addNetwork("net0");
    strcpy(core_wifi._strWifi0, "net0");
    core_wifi._stateSeconds = 0;
    core_wifi.staTick();                 // запуск скана
    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_wifi._stateSeconds = 6;
    core_wifi.staTick();                 // таймаут библиотеки, но ждём результат
    TEST_ASSERT_TRUE(core_wifi._scanActive);

    WiFi.setScanResult(1);               // SCAN_DONE: результат пришёл
    core_wifi._stateSeconds = 7;
    core_wifi.staTick();
    TEST_ASSERT_FALSE(core_wifi._scanActive);
    TEST_ASSERT_EQUAL_UINT8(0, core_wifi._wifiInitFailCount);
}

// Скан сорван по-настоящему: за окном WIFI_SCAN_STUCK_SEC — бэкофф.
static void test_scan_failed_after_grace_backs_off(void) {
    core_wifi._stateSeconds = 0;
    core_wifi.staTick();                 // запуск скана
    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_wifi._stateSeconds = WIFI_SCAN_STUCK_SEC;
    core_wifi.staTick();
    TEST_ASSERT_EQUAL_UINT8(1, core_wifi._wifiInitFailCount);
    TEST_ASSERT_FALSE(core_wifi._scanActive);
    TEST_ASSERT_EQUAL_UINT32(WIFI_SCAN_STUCK_SEC + WIFI_INIT_FAIL_PAUSE_SEC,
                             core_wifi._nextStaScanAt);
}

static void test_scan_failed_restart_in_normal_mode(void) {
    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_state.setMode(CORE_MODE_NORMAL);
    // Каждый цикл: запуск скана, затем таймаут/срыв за окном WIFI_SCAN_STUCK_SEC.
    for (int i = 0; i < WIFI_INIT_FAIL_MAX; i++) {
        core_wifi._stateSeconds += WIFI_INIT_FAIL_PAUSE_SEC;
        core_wifi.staTick(); // запуск асинхронного скана
        core_wifi._stateSeconds += WIFI_SCAN_STUCK_SEC;
        core_wifi.staTick(); // ложный таймаут истёк -> сорванный скан
    }
    TEST_ASSERT_EQUAL_INT(1, mockEspRestartCount());
    TEST_ASSERT_EQUAL_UINT8(WIFI_INIT_FAIL_MAX, core_wifi._wifiInitFailCount);
}

static void test_scan_failed_deferred_in_prog_mode(void) {
    WiFi.setScanResult(WIFI_SCAN_FAILED);
    core_state.setMode(CORE_MODE_PROG);
    // Доводим счётчик до порога: при достижении WIFI_INIT_FAIL_MAX рестарт
    // откладывается (небезопасный режим), немедленного рестарта нет.
    for (int i = 0; i < WIFI_INIT_FAIL_MAX; i++) {
        core_wifi._stateSeconds += WIFI_INIT_FAIL_PAUSE_SEC;
        core_wifi.staTick(); // запуск асинхронного скана
        core_wifi._stateSeconds += WIFI_SCAN_STUCK_SEC;
        core_wifi.staTick(); // ложный таймаут истёк -> сорванный скан
    }
    TEST_ASSERT_EQUAL_INT(0, mockEspRestartCount());
    TEST_ASSERT_EQUAL_UINT8(WIFI_INIT_FAIL_MAX, core_wifi._wifiInitFailCount);
    TEST_ASSERT_TRUE(core_wifi._restartDeferredSince != 0);
}

// Отложенный рестарт не бесконечен: через WIFI_INIT_FAIL_DEFER_MAX_SEC — рестарт.
static void test_scan_failed_deferred_timeout_restarts(void) {
    core_state.setMode(CORE_MODE_PROG);
    core_wifi._wifiInitFailCount = WIFI_INIT_FAIL_MAX;
    core_wifi._restartDeferredSince = 100;
    core_wifi._scanActive = false;
    WiFi.setScanStartResult(WIFI_SCAN_FAILED);

    core_wifi._stateSeconds = 100 + WIFI_INIT_FAIL_DEFER_MAX_SEC;
    core_wifi._nextStaScanAt = 0;
    core_wifi.staTick();   // порог превышен, окно отсрочки истекло -> рестарт
    TEST_ASSERT_EQUAL_INT(1, mockEspRestartCount());
}

static void test_connect_budget_expires_to_ap_wait(void) {
    core_wifi.WifiScan = WF_SCAN_NO_NEED;
    core_wifi.connectionTimout = 0;
    core_wifi.scanTime = 2;
    core_wifi.staTick();
    core_wifi.staTick(); // 2 >= 2 -> enterApWait -> AP-режим
    TEST_ASSERT_EQUAL(FS_STAT_APMODE, core_wifi.wifiStatus);
}

// ============================================================
// Счётчики неудач по SSID
// ============================================================
static void test_wrong_password_counter(void) {
    strcpy(core_wifi._strWifi3, "s3");
    core_wifi.wifiSsidSetPSWDwrong("s3");
    TEST_ASSERT_EQUAL_UINT8(1, core_wifi._wifiFailCount[3]);
    core_wifi.wifiSsidSetPSWDwrong("s3");
    TEST_ASSERT_EQUAL_UINT8(2, core_wifi._wifiFailCount[3]);
    TEST_ASSERT_EQUAL_UINT8(0, core_wifi._wifiFailCount[2]);
}

static void test_any_slot_free(void) {
    core_wifi._wifiFailCount[0] = MAX_WIFI_FAIL_COUNT;
    core_wifi._wifiFailCount[1] = MAX_WIFI_FAIL_COUNT;
    core_wifi._wifiFailCount[2] = MAX_WIFI_FAIL_COUNT;
    core_wifi._wifiFailCount[3] = MAX_WIFI_FAIL_COUNT;
    TEST_ASSERT_FALSE(core_wifi.anySlotFree());
    core_wifi._wifiFailCount[2] = 0;
    TEST_ASSERT_TRUE(core_wifi.anySlotFree());
}

static void test_reset_wifi_fail_counters(void) {
    core_wifi._wifiFailCount[0] = 3;
    core_wifi._wifiFailCount[1] = 2;
    core_wifi.resetWifiFailCounters();
    for (int i = 0; i < 4; i++) { TEST_ASSERT_EQUAL_UINT8(0, core_wifi._wifiFailCount[i]); }
}

static void test_periodic_counter_reset_in_second_tick(void) {
    core_wifi._wifiFailCount[0] = MAX_WIFI_FAIL_COUNT;
    core_wifi._stateSeconds = 59;
    core_wifi.wifiStatus = FS_STAT_CONNECTED;
    core_wifi.secondTick();
    TEST_ASSERT_EQUAL_UINT8(0, core_wifi._wifiFailCount[0]);
}

// ============================================================
// scanWifi: приоритет слотов 3 -> 0
// ============================================================
static void test_scan_wifi_priority(void) {
    WiFi.addNetwork("net0");
    WiFi.addNetwork("net3");
    WiFi.setScanResult(2);
    strcpy(core_wifi._strWifi0, "net0");
    strcpy(core_wifi._strWifi3, "net3");
    TEST_ASSERT_EQUAL_INT(3, core_wifi.scanWifi());
}

static void test_scan_wifi_skips_blocked_slot(void) {
    WiFi.addNetwork("net3");
    WiFi.addNetwork("net0");
    WiFi.setScanResult(2);
    strcpy(core_wifi._strWifi3, "net3");
    strcpy(core_wifi._strWifi0, "net0");
    core_wifi._wifiFailCount[3] = MAX_WIFI_FAIL_COUNT;
    TEST_ASSERT_EQUAL_INT(0, core_wifi.scanWifi());
}

static void test_scan_wifi_none(void) {
    WiFi.addNetwork("other");
    WiFi.setScanResult(1);
    strcpy(core_wifi._strWifi0, "net0");
    TEST_ASSERT_EQUAL_INT(-1, core_wifi.scanWifi());
}

// ============================================================
// AP: apTick / enterApWait
// ============================================================
static void test_ap_tick_leaves_to_scan(void) {
    core_wifi.wifiStatus = FS_STAT_APMODE;
    core_wifi._wifiAPLifeTime = 1;
    core_wifi._apUptime = 59;
    core_wifi._apClientActivity = false;
    core_wifi.apTick(); // ++ -> 60 >= 1*60 -> leaveApToScan
    TEST_ASSERT_EQUAL(FS_STAT_CONNECTING, core_wifi.wifiStatus);
}

static void test_enter_ap_wait_without_ap(void) {
    core_wifi._wifiAPLifeTime = 0;
    core_wifi.wifiStatus = FS_STAT_CONNECTING;
    core_wifi.enterApWait();
    TEST_ASSERT_EQUAL(FS_STAT_CONNECTING, core_wifi.wifiStatus);
}

static void test_enter_ap_wait_with_ap(void) {
    core_wifi._wifiAPLifeTime = 5;
    core_wifi.wifiStatus = FS_STAT_CONNECTING;
    core_wifi.enterApWait();
    TEST_ASSERT_EQUAL(FS_STAT_APMODE, core_wifi.wifiStatus);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_scan_failed_backoff);
    RUN_TEST(test_scan_failed_within_grace_keeps_waiting);
    RUN_TEST(test_late_scan_result_after_library_timeout);
    RUN_TEST(test_scan_failed_after_grace_backs_off);
    RUN_TEST(test_scan_failed_restart_in_normal_mode);
    RUN_TEST(test_scan_failed_deferred_in_prog_mode);
    RUN_TEST(test_scan_failed_deferred_timeout_restarts);
    RUN_TEST(test_connect_budget_expires_to_ap_wait);
    RUN_TEST(test_wrong_password_counter);
    RUN_TEST(test_any_slot_free);
    RUN_TEST(test_reset_wifi_fail_counters);
    RUN_TEST(test_periodic_counter_reset_in_second_tick);
    RUN_TEST(test_scan_wifi_priority);
    RUN_TEST(test_scan_wifi_skips_blocked_slot);
    RUN_TEST(test_scan_wifi_none);
    RUN_TEST(test_ap_tick_leaves_to_scan);
    RUN_TEST(test_enter_ap_wait_without_ap);
    RUN_TEST(test_enter_ap_wait_with_ap);
    return UNITY_END();
}
