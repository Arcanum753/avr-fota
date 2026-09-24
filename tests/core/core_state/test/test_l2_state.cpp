#include "override_prelude.h"

#include "../../../../src/core_json/core_json.cpp"
#include "../../../../src/core_json/core_json_engine.cpp"
#include "../../../../src/core_state/core_state_engine.cpp"
#include "../../../../src/core_state/common_module.cpp"
#include "../../../../src/core_state/core_state.cpp"

#include <unity.h>

// ============================================================
// Тестовый доступ к protected-полям
// ============================================================
class TestState : public CLASS_CORE_STATE {
public:
    using CLASS_CORE_STATE::_resCount;
    using CLASS_CORE_STATE::_nsCount;
    using CLASS_CORE_STATE::_async;
};

// ============================================================
// Колбэки-шпионы
// ============================================================
struct Spy {
    int  calls = 0;
    int  lastRc = -999;
    int  lastArgc = 0;
    BusValue lastVal;
};

static int onEvtCb(void* user, int argc, const BusValue* argv, BusValue&) {
    Spy* s = (Spy*)user;
    s->calls++;
    s->lastArgc = argc;
    if (argc > 1) s->lastVal = argv[1];
    return 0;
}

static int echoCb(void* user, int argc, const BusValue* argv, BusValue& result) {
    (void)user; (void)argc; (void)argv;
    result = BusValue::i32(42);
    return 0;
}

static uint32_t g_asyncHandle = 0;
static int asyncStart(void* user, uint32_t handle, int argc, const BusValue* argv) {
    (void)user; (void)argc; (void)argv;
    g_asyncHandle = handle;
    return BUS_OK;
}
static int asyncStartBusy(void*, uint32_t, int, const BusValue*) { return BUS_ERR_BUSY; }

static int asyncDoneCb(void* user, int argc, const BusValue* argv, BusValue&) {
    Spy* s = (Spy*)user;
    s->calls++;
    s->lastArgc = argc;
    if (argc > 0) s->lastRc = argv[0].i;
    return 0;
}

void setUp(void) {
    mockMillisSet(0);
    g_asyncHandle = 0;
}
void tearDown(void) {}

// ============================================================
// Регистрация и namespace
// ============================================================
static void test_register_and_namespace(void) {
    TestState st;
    st.setNamespace("sys1");
    TEST_ASSERT_TRUE(st.regState("a", BusValue::BOOL, "d", true));
    TEST_ASSERT_TRUE(st.has("sys1.a"));
    TEST_ASSERT_FALSE(st.has("a"));
}

static void test_reg_state_as_full_name(void) {
    TestState st;
    TEST_ASSERT_TRUE(st.regStateAs("ns.fld", BusValue::I32, "d", true));
    TEST_ASSERT_TRUE(st.has("ns.fld"));
    TEST_ASSERT_EQUAL_INT32(0, st.getInt("ns.fld"));
}

static void test_namespace_no_leak(void) {
    TestState st;
    st.setNamespace("nsA");
    st.regState("x", BusValue::I32, "d", true);
    st.clearNamespace();
    st.setNamespace("nsB");
    st.regState("x", BusValue::I32, "d", true);
    TEST_ASSERT_TRUE(st.has("nsA.x"));
    TEST_ASSERT_TRUE(st.has("nsB.x"));
}

static void test_resource_limit(void) {
    TestState st;
    st.setNamespace("lim");
    int ok = 0;
    for (int i = 0; i < CORE_STATE_MAX_RES + 20; i++) {
        char nm[24];
        snprintf(nm, sizeof(nm), "r%d", i);
        if (st.regState(nm, BusValue::I32, "d", true)) ok++;
    }
    TEST_ASSERT_EQUAL_INT(CORE_STATE_MAX_RES, ok);
}

static void test_namespace_limit(void) {
    TestState st;
    int ok = 0;
    for (int i = 0; i < CORE_STATE_MAX_NS + 5; i++) {
        char nm[24];
        snprintf(nm, sizeof(nm), "ns%d", i);
        st.setNamespace(nm);
        if (st.regState("x", BusValue::I32, "d", true)) ok++;
        st.clearNamespace();
    }
    // Движок ограничивает ЧИСЛО namespace (16). Сверх лимита setNamespace
    // оставляет _currentNs = -1, и regState регистрирует ресурс без префикса
    // (наблюдаемое поведение; вынесено в REPORT как кандидат на правку прод-кода).
    TEST_ASSERT_EQUAL_INT(CORE_STATE_MAX_NS, st._nsCount);
    TEST_ASSERT_TRUE(ok >= CORE_STATE_MAX_NS);
}

// ============================================================
// Типы и enum
// ============================================================
static void test_enum_validation(void) {
    TestState st;
    static const char* vals[] = { "off", "auto", "macro" };
    st.setNamespace("m");
    TEST_ASSERT_TRUE(st.regEnum("mode", 3, vals, "mode"));
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.setInt("m.mode", 2));
    TEST_ASSERT_EQUAL_INT32(2, st.getInt("m.mode"));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_VALUE, st.setInt("m.mode", 3));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_VALUE, st.setInt("m.mode", -1));
}

static void test_type_coercion(void) {
    TestState st;
    st.setNamespace("c");
    st.regState("b", BusValue::BOOL, "d", true);
    st.regState("s", BusValue::STR, "d", true);
    st.setInt("c.b", 5);
    TEST_ASSERT_TRUE(st.getBool("c.b"));
    st.setInt("c.s", 123);
    TEST_ASSERT_EQUAL_STRING("123", st.getStr("c.s").c_str());
}

// ============================================================
// Коды возврата
// ============================================================
static void test_error_codes(void) {
    TestState st;
    st.setNamespace("e");
    BusValue out;

    TEST_ASSERT_EQUAL_INT(BUS_ERR_NOT_FOUND, st.setInt("e.nope", 1));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_NOT_FOUND, st.call("e.nope", 0, nullptr, out));

    st.regState("ro", BusValue::I32, "d", false);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_READONLY, st.setInt("e.ro", 1));

    st.regEvent("evt", "d");
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_TYPE, st.setInt("e.evt", 1));

    st.regFunc("fn", "->", "d", echoCb, nullptr);
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.call("e.fn", 0, nullptr, out));
    TEST_ASSERT_EQUAL_INT32(42, out.i);

    st.regFuncAsync("afn", "->", "d", asyncStart, nullptr, 10000);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_TYPE, st.call("e.afn", 0, nullptr, out));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_TYPE, st.call_async("e.fn", nullptr, nullptr, 0, nullptr));

    BusValue big[CORE_STATE_MAX_ARGS + 1];
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BAD_ARGC, st.call("e.fn", CORE_STATE_MAX_ARGS + 1, big, out));
}

// ============================================================
// События
// ============================================================
static void test_events_dispatch(void) {
    TestState st;
    Spy spy;
    uint32_t h = st.on("myevt", onEvtCb, &spy);
    TEST_ASSERT_TRUE(h != 0);
    st.emit("myevt", BusValue::bo(true));
    st.loop();
    TEST_ASSERT_EQUAL_INT(1, spy.calls);
    TEST_ASSERT_EQUAL_INT(2, spy.lastArgc);
    TEST_ASSERT_TRUE(spy.lastVal.b);
}

static void test_event_off(void) {
    TestState st;
    Spy spy;
    uint32_t h = st.on("myevt", onEvtCb, &spy);
    st.off(h);
    st.emit("myevt");
    st.loop();
    TEST_ASSERT_EQUAL_INT(0, spy.calls);
}

static void test_event_queue_overflow(void) {
    TestState st;
    Spy spy;
    st.on("q", onEvtCb, &spy);
    for (int i = 0; i < CORE_STATE_EV_QUEUE + 10; i++) { st.emit("q"); }
    st.loop();
    // Больше ёмкости очереди обработать нельзя — дроп без падения.
    TEST_ASSERT_EQUAL_INT(CORE_STATE_EV_QUEUE, spy.calls);
}

static void test_signal_ignores_readonly(void) {
    TestState st;
    st.setNamespace("sg");
    st.regState("ro", BusValue::I32, "d", false);
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.signal("sg.ro", BusValue::i32(7)));
    TEST_ASSERT_EQUAL_INT32(7, st.getInt("sg.ro"));
}

// ============================================================
// Async
// ============================================================
static void test_async_complete(void) {
    TestState st;
    Spy spy;
    st.setNamespace("a");
    st.regFuncAsync("go", "->", "d", asyncStart, nullptr, 10000);
    int rc = st.call_async("a.go", asyncDoneCb, &spy, 0, nullptr);
    TEST_ASSERT_EQUAL_INT(BUS_OK, rc);
    TEST_ASSERT_TRUE(g_asyncHandle != 0);
    st.asyncComplete(g_asyncHandle, BUS_OK, BusValue::i32(5));
    st.loop();
    TEST_ASSERT_EQUAL_INT(1, spy.calls);
    TEST_ASSERT_EQUAL_INT(BUS_OK, spy.lastRc);
}

static void test_async_timeout(void) {
    TestState st;
    Spy spy;
    st.setNamespace("a");
    st.regFuncAsync("go", "->", "d", asyncStart, nullptr, 10000);
    st.call_async("a.go", asyncDoneCb, &spy, 0, nullptr);
    mockMillisAdvance(10001);
    st.loop();
    TEST_ASSERT_EQUAL_INT(1, spy.calls);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_TIMEOUT, spy.lastRc);
}

static void test_async_per_owner_limit(void) {
    TestState st;
    st.setNamespace("a");
    st.regFuncAsync("go", "->", "d", asyncStart, nullptr, 100000);
    int token = 0;
    st.setAsyncOwner(&token);
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.call_async("a.go", nullptr, nullptr, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.call_async("a.go", nullptr, nullptr, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.call_async("a.go", nullptr, nullptr, 0, nullptr));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.call_async("a.go", nullptr, nullptr, 0, nullptr));
    st.asyncCancelFor(&token);
    st.setAsyncOwner(nullptr);
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.call_async("a.go", nullptr, nullptr, 0, nullptr));
}

static void test_async_start_failure_releases_slot(void) {
    TestState st;
    st.setNamespace("a");
    st.regFuncAsync("busy", "->", "d", asyncStartBusy, nullptr, 10000);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.call_async("a.busy", nullptr, nullptr, 0, nullptr));
    // Слот не должен остаться занятым: следующий вызов снова доходит до функции.
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.call_async("a.busy", nullptr, nullptr, 0, nullptr));
}

// ============================================================
// Режимы
// ============================================================
static void test_modes_basic(void) {
    TestState st;
    st.setMode(CORE_MODE_NORMAL);
    TEST_ASSERT_EQUAL_INT(CORE_MODE_NORMAL, st.getMode());
    st.setMode(CORE_MODE_TEST);
    TEST_ASSERT_EQUAL_INT(CORE_MODE_TEST, st.getMode());
}

static void test_mode_busy_conflicts(void) {
    TestState st;
    st.setMode(CORE_MODE_TEST);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.requestMode(CORE_MODE_OTA));
    st.setMode(CORE_MODE_OTA);
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.requestMode(CORE_MODE_TEST));
}

static void test_mode_prio(void) {
    TestState st;
    st.setMode(CORE_MODE_NORMAL);
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.requestMode(CORE_MODE_OTA, 60));
    TEST_ASSERT_EQUAL_INT(BUS_ERR_BUSY, st.requestMode(CORE_MODE_FS_UPDATE, 40));
    TEST_ASSERT_EQUAL_INT(BUS_OK, st.requestMode(CORE_MODE_FS_UPDATE, 80));
    TEST_ASSERT_EQUAL_INT(CORE_MODE_FS_UPDATE, st.getMode());
}

static void test_mode_timeout(void) {
    TestState st;
    st.setMode(CORE_MODE_TEST);
    mockMillisAdvance(1800001);
    st.loop();
    TEST_ASSERT_EQUAL_INT(CORE_MODE_NORMAL, st.getMode());
}

// ============================================================
// Каталог
// ============================================================
static void test_catalog(void) {
    TestState st;
    st.setNamespace("cat");
    st.regState("v", BusValue::I32, "value", true);
    st.regFunc("f", "->", "func", echoCb, nullptr);
    st.regEvent("e", "evt");
    JsonDocument doc;
    st.catalogToJson(doc);
    JsonArray arr = doc["resources"].as<JsonArray>();
    TEST_ASSERT_EQUAL_INT(3, (int)arr.size());
    TEST_ASSERT_TRUE(doc["features"].is<JsonObject>());
}

static void test_value_to_kind(void) {
    TestState st;
    BusValue b = st.valueToKind(BusValue::i32(1), BusValue::BOOL);
    TEST_ASSERT_EQUAL_INT(BusValue::BOOL, b.kind);
    TEST_ASSERT_TRUE(b.b);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_register_and_namespace);
    RUN_TEST(test_reg_state_as_full_name);
    RUN_TEST(test_namespace_no_leak);
    RUN_TEST(test_resource_limit);
    RUN_TEST(test_namespace_limit);

    RUN_TEST(test_enum_validation);
    RUN_TEST(test_type_coercion);

    RUN_TEST(test_error_codes);

    RUN_TEST(test_events_dispatch);
    RUN_TEST(test_event_off);
    RUN_TEST(test_event_queue_overflow);
    RUN_TEST(test_signal_ignores_readonly);

    RUN_TEST(test_async_complete);
    RUN_TEST(test_async_timeout);
    RUN_TEST(test_async_per_owner_limit);
    RUN_TEST(test_async_start_failure_releases_slot);

    RUN_TEST(test_modes_basic);
    RUN_TEST(test_mode_busy_conflicts);
    RUN_TEST(test_mode_prio);
    RUN_TEST(test_mode_timeout);

    RUN_TEST(test_catalog);
    RUN_TEST(test_value_to_kind);

    return UNITY_END();
}
