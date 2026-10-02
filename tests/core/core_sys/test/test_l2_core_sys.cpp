#include "override_prelude_core_sys.h"

#include "../../../../src/core_sys/eertos.cpp"
#include "../../../../src/core_sys/common_module.cpp"
#include "../../../../src/core_sys/ident_store.cpp"

#include "../../../../src/core_json/core_json.cpp"
#include "../../../../src/core_json/core_json_engine.cpp"
#include "../../../../src/core_state/core_state.cpp"
#include "../../../../src/core_state/core_state_engine.cpp"
#include "../../../../src/core_state/common_module.cpp"
#include "../../../../src/core_task/core_task.cpp"
#include "../../../../src/core_task/core_task_engine.cpp"
#include "../../../../src/common/Time.cpp"
#include "../../../../src/core_sys/core_sys_time.cpp"

#include <unity.h>

// Глобальный объект ядра системы (обычно определён в core_sys.cpp, который в
// host-тестах не компилируется).
CLASS_CORE_SYS core_sys;

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

// ============================================================
// Виртуальное время системы (core_sys_time)
// ============================================================

#include <string.h>

// Доступ к protected/private-полям CLASS_CORE_SYS для управления состоянием.
class TestSys : public CLASS_CORE_SYS {
public:
    using CLASS_CORE_SYS::_timeSrc;
    using CLASS_CORE_SYS::_timeSrcCount;
    using CLASS_CORE_SYS::_timeActiveIdx;
    using CLASS_CORE_SYS::_timeLastValid;
    using CLASS_CORE_SYS::_timeLastValidMs;
    using CLASS_CORE_SYS::_timeTzDec;
    using CLASS_CORE_SYS::_timeDst;
    using CLASS_CORE_SYS::_timeSyncIntervalS;
    using CLASS_CORE_SYS::_timeLastRtcSync;
    using CLASS_CORE_SYS::timeTick;
    using CLASS_CORE_SYS::findTimeSource;
    using CLASS_CORE_SYS::load_config_Time;
    using CLASS_CORE_SYS::save_config_Time;
    using CLASS_CORE_SYS::handleTimeInfo;
    using CLASS_CORE_SYS::handleTimeSources;
    using CLASS_CORE_SYS::handleTimeSave;
    using CLASS_CORE_SYS::handleTimeSet;
    using CLASS_CORE_SYS::handleTimeSync;
    using CLASS_CORE_SYS::handleTimeVer;
};
static TestSys& TS() { return *(reinterpret_cast<TestSys*>(&core_sys)); }

static bool        g_aOk = false;
static time_t      g_aT = 0;
static const char* g_aErr = "";
static int         g_aGets = 0;
static bool        g_bOk = false;
static time_t      g_bT = 0;
static const char* g_bErr = "";
static bool        g_bSet = true;
static time_t      g_bSetVal = 0;
static int         g_bSetCalls = 0;

static bool aGet(time_t& o) { g_aGets++; if (!g_aOk) { return false; } o = g_aT; return true; }
static bool bGet(time_t& o) { if (!g_bOk) { return false; } o = g_bT; return true; }
static bool bSet(time_t in) { g_bSetCalls++; g_bSetVal = in; return g_bSet; }
static const char* aStat() { return g_aErr; }
static const char* bStat() { return g_bErr; }

static int g_evSynced = 0, g_evLost = 0, g_evSrcChanged = 0, g_evTzChanged = 0, g_evSrcInvalid = 0;
static String g_lastInvalid;

static int cbSynced(void*, int, const BusValue*, BusValue&)        { g_evSynced++; return 0; }
static int cbLost(void*, int, const BusValue*, BusValue&)          { g_evLost++; return 0; }
static int cbSrcChanged(void*, int, const BusValue*, BusValue&)    { g_evSrcChanged++; return 0; }
static int cbTzChanged(void*, int, const BusValue*, BusValue&)     { g_evTzChanged++; return 0; }
static int cbSrcInvalid(void*, int argc, const BusValue* argv, BusValue&) {
    g_evSrcInvalid++;
    if (argc > 1 && argv[1].kind == BusValue::STR) { g_lastInvalid = argv[1].s; }
    return 0;
}

static bool g_timeResReady = false;

static void timeEnsureRes() {
    if (g_timeResReady) { return; }
    ModContext ctx;
    ctx.fs = nullptr;
    core_task.begin(ctx);
    core_state.register_resources();
    core_sys.registerTimeResources();
    core_state.on("time.synced", cbSynced, nullptr);
    core_state.on("time.lost", cbLost, nullptr);
    core_state.on("time.source_changed", cbSrcChanged, nullptr);
    core_state.on("time.tz_changed", cbTzChanged, nullptr);
    core_state.on("time.source_invalid", cbSrcInvalid, nullptr);
    g_timeResReady = true;
}

static void timeReset() {
    InitRTOS();
    mockMillisSet(1000);
    TestSys& s = TS();
    s._timeSrcCount = 0;
    s._timeActiveIdx = 255;
    s._timeLastValid = 0;
    s._timeLastValidMs = 0;
    s._timeTzDec = 0;
    s._timeDst = false;
    s._timeSyncIntervalS = 0;
    s._timeLastRtcSync = 0;
    g_aOk = false; g_aT = 0; g_aErr = ""; g_aGets = 0;
    g_bOk = false; g_bT = 0; g_bErr = ""; g_bSetCalls = 0; g_bSet = true; g_bSetVal = 0;
    g_evSynced = 0; g_evLost = 0; g_evSrcChanged = 0; g_evTzChanged = 0; g_evSrcInvalid = 0;
    g_lastInvalid = "";
    setTime((time_t)0);
    core_state.loop();
}

static void addA(int prio) {
    TEST_ASSERT_TRUE(core_sys.addTimeSource("a", prio, aGet, nullptr, aStat));
}
static void addB(int prio, bool withSet) {
    TEST_ASSERT_TRUE(core_sys.addTimeSource("b", prio, bGet, withSet ? bSet : nullptr, withSet ? bStat : nullptr));
}
static void tickOnce() {
    TS().timeTick();
    core_state.loop();
}

// --- L1: чистые хелперы ---
static void test_time_helpers(void) {
    ns_core_sys::TimeSourceView v[3];
    memset(v, 0, sizeof(v));
    strcpy(v[0].name, "a"); v[0].prio = 10; v[0].enabled = true;  v[0].validLast = true;
    strcpy(v[1].name, "b"); v[1].prio = 50; v[1].enabled = true;  v[1].validLast = false;
    strcpy(v[2].name, "c"); v[2].prio = 90; v[2].enabled = false; v[2].validLast = true;

    TEST_ASSERT_EQUAL_INT(0, ns_core_sys::timeSelectActive(v, 3));
    TEST_ASSERT_EQUAL_INT(-1, ns_core_sys::timeSelectActive(v, 0));
    v[1].validLast = true;   // b (prio 50) становится лучшим валидным
    TEST_ASSERT_EQUAL_INT(1, ns_core_sys::timeSelectActive(v, 3));
    v[2].enabled = true;     // c (prio 90) теперь лучший
    TEST_ASSERT_EQUAL_INT(2, ns_core_sys::timeSelectActive(v, 3));

    String csv;
    ns_core_sys::timeCsvNames(v, 3, csv);
    TEST_ASSERT_EQUAL_STRING("a,b,c", csv.c_str());

    int32_t tz = 0;
    TEST_ASSERT_TRUE(ns_core_sys::timeParseTz("30", tz));
    TEST_ASSERT_EQUAL_INT32(30, tz);
    TEST_ASSERT_TRUE(ns_core_sys::timeParseTz("-120", tz));
    TEST_ASSERT_EQUAL_INT32(-120, tz);
    TEST_ASSERT_TRUE(ns_core_sys::timeParseTz("130", tz));
    TEST_ASSERT_FALSE(ns_core_sys::timeParseTz("131", tz));
    TEST_ASSERT_FALSE(ns_core_sys::timeParseTz("-121", tz));
    TEST_ASSERT_FALSE(ns_core_sys::timeParseTz("abc", tz));
    TEST_ASSERT_FALSE(ns_core_sys::timeParseTz("", tz));
    TEST_ASSERT_FALSE(ns_core_sys::timeParseTz("30.5", tz));

    char buf[24];
    ns_core_sys::timeFormatNowStr((time_t)1700000000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2023-11-14 22:13:20", buf);

    // Backward-jump: откат день назад отклоняется, +1 сек — допустим.
    TEST_ASSERT_FALSE(ns_core_sys::timeSelectBackJump((time_t)100, (time_t)200, true));
    TEST_ASSERT_TRUE(ns_core_sys::timeSelectBackJump((time_t)199, (time_t)200, true));
    TEST_ASSERT_TRUE(ns_core_sys::timeSelectBackJump((time_t)100, (time_t)200, false));
    TEST_ASSERT_TRUE(ns_core_sys::timeSelectBackJump((time_t)100, (time_t)0, true));
}

// --- L2: регистрация источников ---
static void test_time_register_duplicate_overflow(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    TEST_ASSERT_EQUAL_INT(1, (int)TS()._timeSrcCount);
    TEST_ASSERT_EQUAL_INT(0, TS().findTimeSource("a"));
    TEST_ASSERT_EQUAL_INT(-1, TS().findTimeSource("nope"));
    TEST_ASSERT_FALSE(core_sys.addTimeSource("a", 10, aGet, nullptr, nullptr));   // дубликат

    while (TS()._timeSrcCount < CORE_SYS_TIME_MAX_SOURCES) {
        String nm = "s" + String((int)TS()._timeSrcCount);
        TEST_ASSERT_TRUE(core_sys.addTimeSource(nm.c_str(), 1, aGet, nullptr, nullptr));
    }
    TEST_ASSERT_FALSE(core_sys.addTimeSource("extra", 1, aGet, nullptr, nullptr)); // переполнение
}

static void test_time_tick_first_and_switch(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();
    TEST_ASSERT_TRUE(core_sys.timeValid());
    TEST_ASSERT_EQUAL_STRING("a", core_sys.timeSourceName());
    TEST_ASSERT_EQUAL_INT(1, g_evSynced);
    TEST_ASSERT_EQUAL_UINT32(1700000000u, (uint32_t)core_sys.timeNow());

    // Смена активного на b (время вперёд — backjump не мешает).
    addB(50, false);
    g_aOk = false;
    g_bOk = true; g_bT = 1700000010;
    g_evSrcChanged = 0;
    tickOnce();
    TEST_ASSERT_EQUAL_STRING("b", core_sys.timeSourceName());
    TEST_ASSERT_EQUAL_INT(1, g_evSrcChanged);
    TEST_ASSERT_EQUAL_INT(1, g_evSynced);   // synced — только при первом успехе
    TEST_ASSERT_EQUAL_UINT32(1700000010u, (uint32_t)core_sys.timeNow());
}

static void test_time_tick_lost_and_recover(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();

    g_aOk = false;
    tickOnce();
    TEST_ASSERT_FALSE(core_sys.timeValid());
    TEST_ASSERT_EQUAL_STRING("", core_sys.timeSourceName());
    TEST_ASSERT_EQUAL_INT(1, g_evLost);
    tickOnce();
    TEST_ASSERT_EQUAL_INT(1, g_evLost);   // фронт true->false — один раз

    // Возврат источника (время вперёд): TimeLib снова ведома валидным временем.
    mockMillisAdvance(5000);
    g_aOk = true; g_aT = 1700000030;
    tickOnce();
    TEST_ASSERT_TRUE(core_sys.timeValid());
    TEST_ASSERT_EQUAL_STRING("a", core_sys.timeSourceName());
    TEST_ASSERT_EQUAL_UINT32(1700000030u, (uint32_t)core_sys.timeNow());
}

static void test_time_tick_backjump_rejected(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();

    g_aOk = false;
    tickOnce();                    // потеря активного

    g_evSrcInvalid = 0; g_lastInvalid = "";
    addB(50, false);
    g_bOk = true; g_bT = 1699999900;   // сильно в прошлом
    mockMillisAdvance(1000);           // ещё в пределах grace
    tickOnce();
    // b ни разу не был валиден, поэтому фронта (и события) нет — только причина для UI.
    TEST_ASSERT_EQUAL_INT(0, g_evSrcInvalid);
    TEST_ASSERT_EQUAL_STRING("time in past (use time.sync_from)", TS()._timeSrc[1].lastReason);
    TEST_ASSERT_FALSE(core_sys.timeValid());

    tickOnce();
    TEST_ASSERT_EQUAL_INT(0, g_evSrcInvalid);   // и повторно не эмитится
    TEST_ASSERT_FALSE(core_sys.timeValid());
}

static void test_time_source_invalid_edge(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();

    g_evSrcInvalid = 0;
    g_aErr = "boom";
    tickOnce();
    TEST_ASSERT_EQUAL_INT(1, g_evSrcInvalid);
    tickOnce();
    TEST_ASSERT_EQUAL_INT(1, g_evSrcInvalid);

    g_aErr = "";
    g_aOk = true;
    tickOnce();
    TEST_ASSERT_TRUE(core_sys.timeValid());
}

static void test_time_cmd_set_and_sync_from(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    addB(50, true);   // b поддерживает запись
    g_bOk = false;

    BusValue args[1];
    args[0] = BusValue::tm(1712345678);
    BusValue res;
    TEST_ASSERT_EQUAL_INT(BUS_OK, core_state.call("time.set", 1, args, res));
    TEST_ASSERT_EQUAL_UINT32(1712345678u, (uint32_t)core_sys.timeNow());
    TEST_ASSERT_EQUAL_INT(1, g_bSetCalls);       // запись только в источник с set()
    TEST_ASSERT_EQUAL_UINT32(1712345678u, (uint32_t)g_bSetVal);

    // Форсированный опрос конкретного источника.
    g_aOk = true; g_aT = 1712345700;
    args[0] = BusValue::str("a");
    TEST_ASSERT_EQUAL_INT(BUS_OK, core_state.call("time.sync_from", 1, args, res));
    TEST_ASSERT_EQUAL_STRING("a", core_sys.timeSourceName());

    args[0] = BusValue::str("nope");
    TEST_ASSERT_EQUAL_INT(BUS_ERR_NOT_FOUND, core_state.call("time.sync_from", 1, args, res));
}

static void test_time_fallback_keeps_now(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();

    g_aOk = false;
    tickOnce();                    // вход в fallback
    time_t t1 = core_sys.timeNow();
    mockMillisAdvance(1000);
    tickOnce();
    time_t t2 = core_sys.timeNow();
    TEST_ASSERT_TRUE(t2 >= t1);    // аварийный timestamp продолжает идти
    TEST_ASSERT_FALSE(core_sys.timeValid());
}

static void test_time_rtc_reverse_sync_interval(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    addB(50, true);            // подчинённый источник с записью
    g_aOk = true; g_aT = 1700000000;
    g_bOk = false;
    TS()._timeSyncIntervalS = 10;
    tickOnce();                // первый успех: _timeLastRtcSync = millis()/1000
    TEST_ASSERT_EQUAL_INT(0, g_bSetCalls);

    mockMillisAdvance(11000);
    tickOnce();                // интервал истёк — одна запись в b
    TEST_ASSERT_EQUAL_INT(1, g_bSetCalls);

    tickOnce();                // без истечения интервала — не пишем
    TEST_ASSERT_EQUAL_INT(1, g_bSetCalls);
}

static void test_time_config_defaults(void) {
    timeReset();
    TEST_ASSERT_FALSE(TS().load_config_Time());       // файла нет -> дефолты в памяти
    TEST_ASSERT_EQUAL_UINT32(3600, TS()._timeSyncIntervalS);
    TEST_ASSERT_EQUAL_INT32(0, TS()._timeTzDec);
    TEST_ASSERT_FALSE(TS()._timeDst);
}

static void test_time_web_handlers(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();

    // /time/info
    AsyncWebServerRequest reqInfo;
    TS().handleTimeInfo(&reqInfo);
    TEST_ASSERT_EQUAL_INT(200, reqInfo.lastCode());
    TEST_ASSERT_TRUE(reqInfo.lastBody().indexOf("time_now_str|") >= 0);
    TEST_ASSERT_TRUE(reqInfo.lastBody().indexOf("time_sources|a") >= 0);

    // /time/sources (AsyncResponseStream)
    AsyncWebServerRequest reqSrc;
    TS().handleTimeSources(&reqSrc);
    TEST_ASSERT_TRUE(reqSrc.streamBody().indexOf("{\"name\":\"a\"") >= 0);
    TEST_ASSERT_TRUE(reqSrc.streamBody().indexOf("\"prio\":100") >= 0);

    // /time/ver
    AsyncWebServerRequest reqVer;
    TS().handleTimeVer(&reqVer);
    TEST_ASSERT_TRUE(reqVer.lastBody().indexOf("timeversion|") >= 0);

    // /time/save (валидный tz/dst + переопределения источника)
    AsyncWebServerRequest reqSave;
    reqSave.addArg("tz", "45");
    reqSave.addArg("dst", "true");
    reqSave.addArg("syncIntervalS", "60");
    reqSave.addArg("a_prio", "10");
    reqSave.addArg("a_enabled", "false");
    TS().handleTimeSave(&reqSave);
    TEST_ASSERT_EQUAL_INT(200, reqSave.lastCode());
    TEST_ASSERT_EQUAL_INT32(45, TS()._timeTzDec);
    TEST_ASSERT_TRUE(TS()._timeDst);
    TEST_ASSERT_EQUAL_UINT32(60, TS()._timeSyncIntervalS);
    TEST_ASSERT_EQUAL_INT32(10, TS()._timeSrc[0].prio);
    TEST_ASSERT_FALSE(TS()._timeSrc[0].enabled);

    // /time/save с недопустимым tz
    AsyncWebServerRequest reqBad;
    reqBad.addArg("tz", "999");
    TS().handleTimeSave(&reqBad);
    TEST_ASSERT_EQUAL_INT(400, reqBad.lastCode());

    // /time/set
    AsyncWebServerRequest reqSet;
    reqSet.addArg("value", "1720000000");
    TS().handleTimeSet(&reqSet);
    TEST_ASSERT_EQUAL_INT(200, reqSet.lastCode());
    TEST_ASSERT_EQUAL_UINT32(1720000000u, (uint32_t)core_sys.timeNow());

    // /time/set без value
    AsyncWebServerRequest reqSetBad;
    TS().handleTimeSet(&reqSetBad);
    TEST_ASSERT_EQUAL_INT(400, reqSetBad.lastCode());

    // /time/sync
    TS()._timeSrc[0].enabled = true;
    g_aOk = true; g_aT = 1720000100;
    AsyncWebServerRequest reqSync;
    reqSync.addArg("source", "a");
    TS().handleTimeSync(&reqSync);
    TEST_ASSERT_EQUAL_INT(200, reqSync.lastCode());
    TEST_ASSERT_EQUAL_STRING("a", core_sys.timeSourceName());

    // /time/sync неизвестного источника и без параметра
    AsyncWebServerRequest reqSyncBad;
    reqSyncBad.addArg("source", "nope");
    TS().handleTimeSync(&reqSyncBad);
    TEST_ASSERT_EQUAL_INT(200, reqSyncBad.lastCode());
    AsyncWebServerRequest reqSyncNone;
    TS().handleTimeSync(&reqSyncNone);
    TEST_ASSERT_EQUAL_INT(400, reqSyncNone.lastCode());
}

static void test_time_config_roundtrip_and_reconcile(void) {
    timeReset(); timeEnsureRes();
    addA(100);
    addB(50, true);

    // save_config_Time выполняется (в host-моке FS без монтирования — без записи).
    TS().save_config_Time();

    // Реконсиляция tz/dst из шины применяется на следующем такте.
    core_state.setInt("time.tz", 30);
    core_state.setBool("time.dst", true);
    g_aOk = true; g_aT = 1700000000;
    tickOnce();
    TEST_ASSERT_EQUAL_INT32(30, TS()._timeTzDec);
    TEST_ASSERT_TRUE(TS()._timeDst);
    TEST_ASSERT_EQUAL_INT8(3, core_sys.timeZoneHours());
    TEST_ASSERT_EQUAL_INT8(0, core_sys.timeZoneMinutes());
    TEST_ASSERT_TRUE(core_sys.daylight());
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
    RUN_TEST(test_time_helpers);
    RUN_TEST(test_time_register_duplicate_overflow);
    RUN_TEST(test_time_tick_first_and_switch);
    RUN_TEST(test_time_tick_lost_and_recover);
    RUN_TEST(test_time_tick_backjump_rejected);
    RUN_TEST(test_time_source_invalid_edge);
    RUN_TEST(test_time_cmd_set_and_sync_from);
    RUN_TEST(test_time_fallback_keeps_now);
    RUN_TEST(test_time_rtc_reverse_sync_interval);
    RUN_TEST(test_time_config_defaults);
    RUN_TEST(test_time_web_handlers);
    RUN_TEST(test_time_config_roundtrip_and_reconcile);
    return UNITY_END();
}
