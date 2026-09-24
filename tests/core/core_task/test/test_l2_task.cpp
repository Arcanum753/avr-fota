#include "override_prelude.h"

#include "../../../../src/core_task/core_task.cpp"
#include "../../../../src/core_task/core_task_engine.cpp"
#include "../../../../src/core_sys/eertos.cpp"

#include <unity.h>

void setUp(void) {
    InitRTOS();
    ModContext ctx;
    ctx.fs = nullptr;
    core_task.begin(ctx);
}

void tearDown(void) {}

static int g_runs = 0;
static void tickFn(void) { g_runs++; }
static int g_runs2 = 0;
static void tickFn2(void) { g_runs2++; }

static void advance(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) { TimerService(); }
}

static void test_every_not_fire_now(void) {
    g_runs = 0;
    TEST_ASSERT_TRUE(core_task.every("a", tickFn, 100, false));
    advance(99);
    TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
    advance(1);
    TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
}

static void test_every_periodic(void) {
    g_runs = 0;
    core_task.every("a", tickFn, 100, false);
    advance(100); TaskManager();
    advance(100); TaskManager();
    advance(100); TaskManager();
    TEST_ASSERT_EQUAL_INT(3, g_runs);
}

static void test_every_fire_now(void) {
    g_runs = 0;
    core_task.every("a", tickFn, 100, true);
    TEST_ASSERT_EQUAL_INT(1, g_runs);
}

static void test_after_single_shot(void) {
    g_runs = 0;
    core_task.after("a", tickFn, 50);
    advance(50); TaskManager();
    advance(200); TaskManager();
    TEST_ASSERT_EQUAL_INT(1, g_runs);
}

static void test_cancel(void) {
    g_runs = 0;
    core_task.every("a", tickFn, 10, false);
    core_task.cancel("a");
    advance(50); TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
}

static void test_cancel_all(void) {
    g_runs = 0; g_runs2 = 0;
    core_task.every("a", tickFn, 10, false);
    core_task.every("b", tickFn2, 10, false);
    core_task.cancelAll();
    advance(50); TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
    TEST_ASSERT_EQUAL_INT(0, g_runs2);
}

static void test_slot_reuse_by_name(void) {
    g_runs = 0; g_runs2 = 0;
    core_task.every("a", tickFn, 10, false);
    core_task.every("a", tickFn2, 10, false); // тот же слот, новая функция
    advance(10); TaskManager();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
    TEST_ASSERT_EQUAL_INT(1, g_runs2);
}

static void test_slot_overflow(void) {
    for (int i = 0; i < CORE_TASK_SLOTS; i++) {
        char nm[16];
        snprintf(nm, sizeof(nm), "s%d", i);
        TEST_ASSERT_TRUE(core_task.every(nm, tickFn, 1000, false));
    }
    TEST_ASSERT_FALSE(core_task.every("overflow", tickFn, 1000, false));
}

static void test_loop_diag_no_crash(void) {
    g_runs = 0;
    core_task.every("a", tickFn, 1000, false);
    core_task.loop();
    TEST_ASSERT_EQUAL_INT(0, g_runs);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_not_fire_now);
    RUN_TEST(test_every_periodic);
    RUN_TEST(test_every_fire_now);
    RUN_TEST(test_after_single_shot);
    RUN_TEST(test_cancel);
    RUN_TEST(test_cancel_all);
    RUN_TEST(test_slot_reuse_by_name);
    RUN_TEST(test_slot_overflow);
    RUN_TEST(test_loop_diag_no_crash);
    return UNITY_END();
}
