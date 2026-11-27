#include "override_prelude.h"

#include "../../../../src/core_terminal/ErriezSerialTerminal.cpp"
#include "../../../../src/core_terminal/core_terminal.cpp"

#include <stdio.h>
#include <unity.h>

// --- Заглушки engine-функций (core_terminal_engine.cpp в native-тестах не компилируется) ---
void TerminalHelpCmd() {}
void TerminalEchoCmd() {}
void TerminalDefaultCmd(const char*) {}

// --- Обработчик слота модуля ---
static int g_applied = 0;
static void slotFn() { g_applied++; }

void setUp(void) {}
void tearDown(void) {}

static void test_terminal_register_and_dispatch(void) {
    TerminalInit(); // регистрирует встроенные команды (help/echo/y/n)

    // Заполняем реестр: TERMINAL_MAX_MODULES записей + одна лишняя.
    static char names[TERMINAL_MAX_MODULES + 1][8];
    for (int i = 0; i <= TERMINAL_MAX_MODULES; i++) {
        snprintf(names[i], sizeof(names[i]), "t%d", i);
        TerminalRegisterModule("tst", names[i], "test", slotFn, nullptr, "", false);
    }
    TEST_ASSERT_EQUAL_UINT8(TERMINAL_MAX_MODULES, TerminalModuleCount());
    TEST_ASSERT_TRUE(TerminalHasModule("tst"));
    TEST_ASSERT_FALSE(TerminalHasModule("nope"));

    // Дубликат имени игнорируется.
    TerminalRegisterModule("tst", names[0], "dup", slotFn, nullptr, "", false);
    TEST_ASSERT_EQUAL_UINT8(TERMINAL_MAX_MODULES, TerminalModuleCount());

    // Диспетчеризация зарегистрированной команды верхнего уровня.
    Serial.inject("t0\r");
    TerminalLoop();
    TEST_ASSERT_EQUAL_INT(1, g_applied);

    Serial.clearInput();
    TerminalLoop(); // без ввода повторного вызова нет
    TEST_ASSERT_EQUAL_INT(1, g_applied);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_register_and_dispatch);
    return UNITY_END();
}
