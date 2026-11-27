#include "override_prelude.h"

#include "../../../../src/core_sys/eertos.cpp"
#include "../../../../src/core_led/core_led_engine.cpp"
#include "../../../../src/core_led/common_module.cpp"

#include <unity.h>

void setUp(void) {
    InitRTOS();
    ledMacroRst();
    mockDigitalWriteReset();
}
void tearDown(void) {}

// ============================================================
// Разбор паттернов
// ============================================================
static void test_pattern_helpers(void) {
    TEST_ASSERT_EQUAL_UINT8(3, ns_core_led::ledPatLen("*.*"));
    TEST_ASSERT_EQUAL_UINT8(0, ns_core_led::ledPatLen(""));
    TEST_ASSERT_EQUAL_CHAR('*', ns_core_led::ledPatAt("*.*", 0));
    TEST_ASSERT_EQUAL_CHAR('.', ns_core_led::ledPatAt("*.*", 1));
    TEST_ASSERT_EQUAL_CHAR('*', ns_core_led::ledPatAt("*.*", 2));
}

// ============================================================
// Приоритеты слотов
// ============================================================
static void test_priority_top_slot(void) {
    ledSetState(LED_PRIO_DEV, "*", -1);      // on
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
    ledSetState(LED_PRIO_WIFI, ".", -1);     // off — выше приоритетом
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
    ledClearState(LED_PRIO_WIFI);
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
    ledClearState(LED_PRIO_DEV);
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
}

// ============================================================
// Конечный паттерн не прерывается сменой указателя
// ============================================================
static void test_finite_pattern_not_preempted(void) {
    ledSetState(LED_PRIO_DEV, "*..", 3);
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
    ledSetState(LED_PRIO_DEV, "...", 3); // тот же приоритет, конечный — игнор
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
}

// ============================================================
// Идемпотентность по указателю паттерна
// ============================================================
static void test_same_pattern_pointer_idempotent(void) {
    const char* p = "*.*";
    ledSetState(LED_PRIO_DEV, p, 5);
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());   // pos0 '*'
    ledMacroBlinker();
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());  // pos1 '.'
    ledSetState(LED_PRIO_DEV, p, 5);                      // не сбрасывает позицию
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
    ledMacroBlinker();
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());   // pos2 '*'
}

// ============================================================
// Продвижение и завершение паттерна
// ============================================================
static void test_blinker_expires(void) {
    ledSetState(LED_PRIO_DEV, "*", 2);
    TEST_ASSERT_TRUE(ledMacroBlinker());               // times 2 -> 1
    TEST_ASSERT_TRUE(ledMacroBlinker());               // times 1 -> 0, слот снят
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
    TEST_ASSERT_FALSE(ledMacroBlinker());              // активных слотов нет
}

// ============================================================
// Постоянное свечение
// ============================================================
static void test_steady(void) {
    ledSetSteady(true);
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
    ledSetSteady(false);
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
}

// ============================================================
// Ручной слот (терминал)
// ============================================================
static void test_manual_set(void) {
    LedMacroSet("***", -1);
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
    LedMacroSet("...", -1);
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
}

static void test_manual_long_string_is_clamped(void) {
    String big;
    for (int i = 0; i < LEDSTRINGLIMIT + 50; i++) { big += '*'; }
    LedMacroSet(big.c_str(), -1); // не должно падать/переполняться
    TEST_ASSERT_EQUAL_INT(LOW, mockLastDigitalValue());
}

// ============================================================
// Сброс
// ============================================================
static void test_macro_reset(void) {
    ledSetState(LED_PRIO_MANUAL, "*", -1);
    ledMacroRst();
    TEST_ASSERT_EQUAL_INT(HIGH, mockLastDigitalValue());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pattern_helpers);
    RUN_TEST(test_priority_top_slot);
    RUN_TEST(test_finite_pattern_not_preempted);
    RUN_TEST(test_same_pattern_pointer_idempotent);
    RUN_TEST(test_blinker_expires);
    RUN_TEST(test_steady);
    RUN_TEST(test_manual_set);
    RUN_TEST(test_manual_long_string_is_clamped);
    RUN_TEST(test_macro_reset);
    return UNITY_END();
}
