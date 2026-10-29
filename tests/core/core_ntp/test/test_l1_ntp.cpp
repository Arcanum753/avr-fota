// L1: чистые хелперы core_ntp (ns_core_ntp) на host-моке Arduino.
// «Живое» поведение источника времени проверяется на устройстве (см. план).

#include "../../../../src/core_ntp/common_module.cpp"

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ============================================================
// Адаптивный stale
// ============================================================
static void test_stale_floor(void) {
    // minStaleSec (1800) держит нижнюю границу при периоде до 15 мин.
    TEST_ASSERT_EQUAL_UINT32(1800, ns_core_ntp::ntpStaleSec(0, 1800));
    TEST_ASSERT_EQUAL_UINT32(1800, ns_core_ntp::ntpStaleSec(1, 1800));
    TEST_ASSERT_EQUAL_UINT32(1800, ns_core_ntp::ntpStaleSec(15, 1800));
}

static void test_stale_period_scales(void) {
    // stale = 2 * period * 60 при периоде > 15 мин.
    TEST_ASSERT_EQUAL_UINT32(1920, ns_core_ntp::ntpStaleSec(16, 1800));
    TEST_ASSERT_EQUAL_UINT32(3000, ns_core_ntp::ntpStaleSec(25, 1800));
    TEST_ASSERT_EQUAL_UINT32(7200, ns_core_ntp::ntpStaleSec(60, 1800));
    TEST_ASSERT_EQUAL_UINT32(86400, ns_core_ntp::ntpStaleSec(720, 1800));
}

static void test_stale_custom_floor(void) {
    // Порог-минимум передаётся параметром (CORE_SYS_TIME_NTP_STALE_S).
    TEST_ASSERT_EQUAL_UINT32(600, ns_core_ntp::ntpStaleSec(5, 600));
    TEST_ASSERT_EQUAL_UINT32(3600, ns_core_ntp::ntpStaleSec(15, 3600));
}

// ============================================================
// Прошедшее время с учётом переполнения millis()
// ============================================================
static void test_elapsed_never_synced(void) {
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, ns_core_ntp::ntpElapsedSec(0, 0));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFUL, ns_core_ntp::ntpElapsedSec(123456, 0));
}

static void test_elapsed_normal(void) {
    TEST_ASSERT_EQUAL_UINT32(0, ns_core_ntp::ntpElapsedSec(5000, 5000));
    TEST_ASSERT_EQUAL_UINT32(0, ns_core_ntp::ntpElapsedSec(5999, 5000));
    TEST_ASSERT_EQUAL_UINT32(1, ns_core_ntp::ntpElapsedSec(6000, 5000));
    TEST_ASSERT_EQUAL_UINT32(300, ns_core_ntp::ntpElapsedSec(305000, 5000));
}

static void test_elapsed_millis_wrap(void) {
    // last = 0xFFFFFC18, now = 1000 -> 2000 мс -> 2 с (переполнение millis()).
    uint32_t last = 0xFFFFFC18UL;
    TEST_ASSERT_EQUAL_UINT32(2, ns_core_ntp::ntpElapsedSec(1000, last));
    // Ровно на границе переполнения: now = 0, прошло 1000 мс.
    TEST_ASSERT_EQUAL_UINT32(1, ns_core_ntp::ntpElapsedSec(0, last));
}

// ============================================================
// Валидация имени сервера
// ============================================================
static void test_name_valid_required(void) {
    TEST_ASSERT_TRUE(ns_core_ntp::ntpServerNameValid("pool.ntp.org", true));
    TEST_ASSERT_FALSE(ns_core_ntp::ntpServerNameValid("", true));
}

static void test_name_valid_optional(void) {
    TEST_ASSERT_TRUE(ns_core_ntp::ntpServerNameValid("", false));
    TEST_ASSERT_TRUE(ns_core_ntp::ntpServerNameValid("1.ru.pool.ntp.org", false));
}

static void test_name_length_limit(void) {
    String s39;
    String s40;
    for (int i = 0; i < 39; i++) { s39 += 'a'; }
    for (int i = 0; i < 40; i++) { s40 += 'a'; }
    // Предел — 39 символов (SERVER_NAME_LENGTH=40, toCharArray сохраняет 39).
    TEST_ASSERT_EQUAL_UINT32(39, s39.length());
    TEST_ASSERT_EQUAL_UINT32(40, s40.length());
    TEST_ASSERT_TRUE(ns_core_ntp::ntpServerNameValid(s39, true));
    TEST_ASSERT_FALSE(ns_core_ntp::ntpServerNameValid(s40, true));
    TEST_ASSERT_FALSE(ns_core_ntp::ntpServerNameValid(s40, false));
}

// ============================================================
// Циклический выбор серверов
// ============================================================
static void test_next_server_cycle(void) {
    const String s0 = "a";
    const String s1 = "b";
    const String s2 = "c";
    TEST_ASSERT_EQUAL_INT(1, ns_core_ntp::ntpNextServerIndex(0, s0, s1, s2));
    TEST_ASSERT_EQUAL_INT(2, ns_core_ntp::ntpNextServerIndex(1, s0, s1, s2));
    TEST_ASSERT_EQUAL_INT(0, ns_core_ntp::ntpNextServerIndex(2, s0, s1, s2));
}

static void test_next_server_skips_empty(void) {
    const String s0 = "a";
    const String s1 = "";
    const String s2 = "c";
    TEST_ASSERT_EQUAL_INT(2, ns_core_ntp::ntpNextServerIndex(0, s0, s1, s2));
    TEST_ASSERT_EQUAL_INT(0, ns_core_ntp::ntpNextServerIndex(2, s0, s1, s2));
    // Только основной сервер: остаёмся на нём.
    TEST_ASSERT_EQUAL_INT(0, ns_core_ntp::ntpNextServerIndex(0, s0, s1, s1));
}

static void test_next_server_all_empty(void) {
    const String empty = "";
    TEST_ASSERT_EQUAL_INT(-1, ns_core_ntp::ntpNextServerIndex(0, empty, empty, empty));
}

static void test_next_server_out_of_range(void) {
    const String s0 = "a";
    const String s1 = "b";
    const String s2 = "c";
    // Некорректный текущий индекс нормализуется к 0.
    TEST_ASSERT_EQUAL_INT(1, ns_core_ntp::ntpNextServerIndex(-5, s0, s1, s2));
    TEST_ASSERT_EQUAL_INT(1, ns_core_ntp::ntpNextServerIndex(7, s0, s1, s2));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_floor);
    RUN_TEST(test_stale_period_scales);
    RUN_TEST(test_stale_custom_floor);
    RUN_TEST(test_elapsed_never_synced);
    RUN_TEST(test_elapsed_normal);
    RUN_TEST(test_elapsed_millis_wrap);
    RUN_TEST(test_name_valid_required);
    RUN_TEST(test_name_valid_optional);
    RUN_TEST(test_name_length_limit);
    RUN_TEST(test_next_server_cycle);
    RUN_TEST(test_next_server_skips_empty);
    RUN_TEST(test_next_server_all_empty);
    RUN_TEST(test_next_server_out_of_range);
    return UNITY_END();
}
