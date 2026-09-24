#include "override_prelude.h"

#include "../../../../src/core_terminal/ErriezSerialTerminal.cpp"
#include "../../../../src/core_terminal/core_terminal.cpp"

#include <unity.h>

// --- Заглушки command-функций (реализации живут в core_terminal_engine.cpp,
//     который в native-тестах не компилируется) ---
static int g_testCalls = 0;
void TerminalHelp (void) {}
void TerminalEcho (void) {}
void EspReset() {}
void InfoShow() {}
void test() { g_testCalls++; }
void DirsShow() {}
void avr() {}
void udpp() {}
void udpc() {}
void udps() {}
void BlinkCmd() {}
void TermIdent() {}

// --- Слоты модулей ---
static int g_applied = 0;
static void slotFn() { g_applied++; }

void setUp(void) {}
void tearDown(void) {}

static void test_terminal_init_slots_and_dispatch(void) {
    TerminalInit(); // регистрирует базовые команды

    for (int i = 0; i < TERMINAL_MODULE_SLOTS + 1; i++) {
        TerminalRegisterModule(slotFn); // 9-й должен быть проигнорирован
    }

    // Симулируем ввод команды "1" — она зарегистрирована в TerminalInit.
    Serial.inject("1\r");

    TerminalLoop(); // лениво применяет слоты, затем читает Serial

    TEST_ASSERT_EQUAL_INT(TERMINAL_MODULE_SLOTS, g_applied);
    TEST_ASSERT_EQUAL_INT(1, g_testCalls);

    Serial.clearInput();
    TerminalLoop(); // повторное применение слотов не происходит
    TEST_ASSERT_EQUAL_INT(TERMINAL_MODULE_SLOTS, g_applied);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_init_slots_and_dispatch);
    return UNITY_END();
}
