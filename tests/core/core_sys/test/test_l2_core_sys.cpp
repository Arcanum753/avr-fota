#include "override_prelude.h"

#include "../../../../src/core_sys/eertos.cpp"
#include "../../../../src/core_sys/common_module.cpp"
#include "../../../../src/core_sys/ident_store.cpp"

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ============================================================
// L1: isAdminPassValid
// ============================================================
static void test_admin_pass_valid(void) {
    TEST_ASSERT_TRUE(ns_core_sys::isAdminPassValid("abcdefgh"));
    TEST_ASSERT_TRUE(ns_core_sys::isAdminPassValid("A1b2C3d4"));
    TEST_ASSERT_TRUE(ns_core_sys::isAdminPassValid("0123456789"));
}

static void test_admin_pass_length_bounds(void) {
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid("abcdefg"));    // 7
    TEST_ASSERT_TRUE(ns_core_sys::isAdminPassValid("abcdefgh"));    // 8
    String s63;
    for (int i = 0; i < 63; i++) s63 += 'a';
    TEST_ASSERT_TRUE(ns_core_sys::isAdminPassValid(s63));
    String s64 = s63 + "a";
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid(s64));
}

static void test_admin_pass_charset(void) {
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid("abcd-efg"));
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid("abcd efg"));
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid("абвгдежз"));
    TEST_ASSERT_FALSE(ns_core_sys::isAdminPassValid("abcd!@#$"));
}

// ============================================================
// L1: identCrcSkip
// ============================================================
static void test_crc_skip_ignores_field(void) {
    uint8_t a[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    uint8_t b[8] = { 1, 2, 3, 4, 9, 9, 7, 8 };

    uint32_t fullA = ns_core_sys::identCrcSkip(a, 8, 8, 4);
    uint32_t fullB = ns_core_sys::identCrcSkip(b, 8, 8, 4);
    TEST_ASSERT_TRUE(fullA != fullB);

    uint32_t skipA = ns_core_sys::identCrcSkip(a, 8, 4, 2);
    uint32_t skipB = ns_core_sys::identCrcSkip(b, 8, 4, 2);
    TEST_ASSERT_EQUAL_UINT32(skipA, skipB);
}

static void test_crc_skip_stable(void) {
    uint8_t d[16] = { 0 };
    uint32_t x = ns_core_sys::identCrcSkip(d, sizeof(d), 8, 4);
    uint32_t y = ns_core_sys::identCrcSkip(d, sizeof(d), 8, 4);
    TEST_ASSERT_EQUAL_UINT32(x, y);
    TEST_ASSERT_TRUE(x != 0);
}

// ============================================================
// L2: EERTOS
// ============================================================
static int g_runs = 0;
static void countedTask(void) { g_runs++; }

static void test_eertos_task_queue_one_per_manager(void) {
    InitRTOS();
    g_runs = 0;
    TEST_ASSERT_TRUE(SetTaskEx(countedTask));
    TEST_ASSERT_TRUE(SetTaskEx(countedTask));
    TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
    TaskManager();
    TEST_ASSERT_EQUAL_INT(2, g_runs);
    TaskManager();
    TEST_ASSERT_EQUAL_INT(2, g_runs);
}

static void test_eertos_task_queue_overflow(void) {
    InitRTOS();
    uint32_t before = EertosDroppedCount();
    for (uint32_t i = 0; i < TaskQueueSize; i++) {
        TEST_ASSERT_TRUE(SetTaskEx(countedTask));
    }
    TEST_ASSERT_FALSE(SetTaskEx(countedTask));
    TEST_ASSERT_EQUAL_UINT32(before + 1, EertosDroppedCount());
}

static void test_eertos_timer_idempotent_by_pointer(void) {
    InitRTOS();
    g_runs = 0;
    TEST_ASSERT_TRUE(SetTimerTaskEx(countedTask, 5));
    TEST_ASSERT_TRUE(SetTimerTaskEx(countedTask, 5));
    for (int i = 0; i < 5; i++) { TimerService(); }
    TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
    TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
}

static void test_eertos_timer_reschedule(void) {
    InitRTOS();
    g_runs = 0;
    SetTimerTaskEx(countedTask, 3);
    SetTimerTaskEx(countedTask, 7);
    for (int i = 0; i < 3; i++) { TimerService(); }
    TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
    for (int i = 0; i < 4; i++) { TimerService(); }
    TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
}

static void test_eertos_del_timer(void) {
    InitRTOS();
    g_runs = 0;
    SetTimerTaskEx(countedTask, 2);
    DelTimerTask(countedTask);
    for (int i = 0; i < 5; i++) { TimerService(); }
    TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
}

// ============================================================
// L2: ident_store
// ============================================================
static void test_ident_load_empty(void) {
    EEPROM.mockWipe();
    String name = "x", serial = "y";
    TEST_ASSERT_FALSE(identStoreLoad(name, serial));
}

static void test_ident_roundtrip(void) {
    EEPROM.mockWipe();
    TEST_ASSERT_TRUE(identStoreSave("Device-1", "SN-12345"));
    String name, serial;
    TEST_ASSERT_TRUE(identStoreLoad(name, serial));
    TEST_ASSERT_EQUAL_STRING("Device-1", name.c_str());
    TEST_ASSERT_EQUAL_STRING("SN-12345", serial.c_str());
}

static void test_ident_max_length(void) {
    EEPROM.mockWipe();
    String n63;
    for (int i = 0; i < IDENT_MAX_NAME; i++) n63 += 'n';
    TEST_ASSERT_TRUE(identStoreSave(n63, "s"));
    String tooLong = n63 + "x";
    TEST_ASSERT_FALSE(identStoreSave(tooLong, "s"));
}

static void test_ident_crc_detects_corruption(void) {
    EEPROM.mockWipe();
    TEST_ASSERT_TRUE(identStoreSave("name", "serial"));
    EEPROM.mockCorrupt(12, (uint8_t)('X'));
    String name, serial;
    TEST_ASSERT_FALSE(identStoreLoad(name, serial));
}

static void test_ident_erase(void) {
    EEPROM.mockWipe();
    identStoreSave("name", "serial");
    TEST_ASSERT_TRUE(identStoreErase());
    String name, serial;
    TEST_ASSERT_FALSE(identStoreLoad(name, serial));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_admin_pass_valid);
    RUN_TEST(test_admin_pass_length_bounds);
    RUN_TEST(test_admin_pass_charset);
    RUN_TEST(test_crc_skip_ignores_field);
    RUN_TEST(test_crc_skip_stable);
    RUN_TEST(test_eertos_task_queue_one_per_manager);
    RUN_TEST(test_eertos_task_queue_overflow);
    RUN_TEST(test_eertos_timer_idempotent_by_pointer);
    RUN_TEST(test_eertos_timer_reschedule);
    RUN_TEST(test_eertos_del_timer);
    RUN_TEST(test_ident_load_empty);
    RUN_TEST(test_ident_roundtrip);
    RUN_TEST(test_ident_max_length);
    RUN_TEST(test_ident_crc_detects_corruption);
    RUN_TEST(test_ident_erase);
    return UNITY_END();
}
