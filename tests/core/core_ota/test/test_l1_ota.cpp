#include "override_prelude.h"
#include "LittleFS.h"

#include "../../../../src/core_sys/eertos.cpp"
#include "../../../../src/core_led/core_led_engine.cpp"
#include "../../../../src/core_led/common_module.cpp"
#include "../../../../src/core_ota/core_ota_engine.cpp"

#include <unity.h>

// Глобальные объекты, обычно определяемые в core_ota.cpp / core_sys.cpp.
CLASS_CORE_OTA core_ota;
CLASS_CORE_SYS core_sys;

// Методы, определённые в core_ota.cpp (web/версии), в тестах не компилируются.
void CLASS_CORE_OTA::web_Init() {}
String CLASS_CORE_OTA::getVersionStr() { return String("0.000.00000000_0000.0000"); }
String CLASS_CORE_OTA::getGeneratedTime() { return String(); }
String CLASS_CORE_OTA::getCommitDateStr() { return String(); }
void CLASS_CORE_OTA::html_ver_get(AsyncWebServerRequest*) {}

class TestOta : public CLASS_CORE_OTA {
public:
    using CLASS_CORE_OTA::isValidFilename;
    using CLASS_CORE_OTA::prepareSizesForUpdate;
    using CLASS_CORE_OTA::ConfigureOTA;
    using CLASS_CORE_OTA::maxSketchSpace;
    using CLASS_CORE_OTA::freeSketchSpace;
};

void setUp(void) {
    _ota_fsEndCalled = false;
    core_ota._fs = nullptr;
}
void tearDown(void) {}

// ============================================================
// compareVersionDiffs
// ============================================================
static void test_compare_versions(void) {
    TEST_ASSERT_EQUAL_INT8(0, CLASS_CORE_OTA::compareVersionDiffs(0, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT8(1, CLASS_CORE_OTA::compareVersionDiffs(1, 0, 0, 0));
    TEST_ASSERT_EQUAL_INT8(-1, CLASS_CORE_OTA::compareVersionDiffs(-1, 5, 5, 5));
    TEST_ASSERT_EQUAL_INT8(1, CLASS_CORE_OTA::compareVersionDiffs(0, 1, 0, 0));
    TEST_ASSERT_EQUAL_INT8(-1, CLASS_CORE_OTA::compareVersionDiffs(0, -1, 5, 5));
    TEST_ASSERT_EQUAL_INT8(1, CLASS_CORE_OTA::compareVersionDiffs(0, 0, 1, 0));
    TEST_ASSERT_EQUAL_INT8(-1, CLASS_CORE_OTA::compareVersionDiffs(0, 0, -1, 5));
    TEST_ASSERT_EQUAL_INT8(1, CLASS_CORE_OTA::compareVersionDiffs(0, 0, 0, 1));
    TEST_ASSERT_EQUAL_INT8(-1, CLASS_CORE_OTA::compareVersionDiffs(0, 0, 0, -1));
}

// ============================================================
// isValidFilename
// ============================================================
static void test_valid_filename(void) {
    TestOta ota;
    TEST_ASSERT_TRUE(ota.isValidFilename("firmware.bin"));
    TEST_ASSERT_TRUE(ota.isValidFilename("testenv-FIRMWARE-1.002.20260101_1200.0100.bin"));
    TEST_ASSERT_TRUE(ota.isValidFilename("a_b-c.1"));

    TEST_ASSERT_FALSE(ota.isValidFilename(""));
    TEST_ASSERT_FALSE(ota.isValidFilename("a/b.bin"));
    TEST_ASSERT_FALSE(ota.isValidFilename("a b.bin"));
    TEST_ASSERT_FALSE(ota.isValidFilename("файл.bin"));
}

static void test_filename_length_limit(void) {
    TestOta ota;
    String s;
    for (int i = 0; i < 100; i++) s += 'a';
    TEST_ASSERT_TRUE(ota.isValidFilename(s));
    s += 'a';
    TEST_ASSERT_FALSE(ota.isValidFilename(s));
}

// ============================================================
// fileNameCheck
// ============================================================
static void test_filename_upgrade_allowed(void) {
    TestOta ota;
    fileCompareResult r;
    TEST_ASSERT_EQUAL_INT8(1, ota.fileNameCheck("testenv-FIRMWARE-1.003.20260101_1200.0100.bin", &r));
    TEST_ASSERT_EQUAL_INT8(1, r.nameMatch);
}

static void test_filename_downgrade_blocked(void) {
    TestOta ota;
    fileCompareResult r;
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("testenv-FIRMWARE-0.001.20250101_1200.0001.bin", &r));
}

static void test_filename_equal_blocked(void) {
    TestOta ota;
    fileCompareResult r;
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("testenv-FIRMWARE-1.002.20260101_1200.0100.bin", &r));
}

static void test_filename_wrong_env(void) {
    TestOta ota;
    fileCompareResult r;
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("otherenv-FIRMWARE-1.003.20260101_1200.0100.bin", &r));
}

static void test_filename_garbage_and_empty(void) {
    TestOta ota;
    fileCompareResult r;
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("", &r));
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("random.bin", &r));
    TEST_ASSERT_EQUAL_INT8(-1, ota.fileNameCheck("testenv-FIRMWARE-nope.bin", &r));
}

// ============================================================
// L2: fsEnd / fsRemount
// ============================================================
static void test_fs_end_sets_flag(void) {
    core_ota._fs = &LittleFS;
    core_ota.fsEnd();
    TEST_ASSERT_TRUE(_ota_fsEndCalled);
}

static void test_fs_end_without_fs(void) {
    core_ota.fsEnd();
    TEST_ASSERT_FALSE(_ota_fsEndCalled);
}

static void test_fs_remount(void) {
    core_ota._fs = &LittleFS;
    core_ota.fsRemount();
    TEST_ASSERT_TRUE(true);
}

// ============================================================
// L2: compareWithCurrentFsVersion
// ============================================================
static void test_compare_firmware_uses_build_version(void) {
    fileCompareResult r;
    r.fileType = FILE_TYPE_FIRMWARE;
    r.majorDiff = 1; r.minorDiff = 0; r.dateDiff = 0; r.buildDiff = 0;
    TEST_ASSERT_EQUAL_INT8(1, core_ota.compareWithCurrentFsVersion(&r, "fw.bin"));
    TEST_ASSERT_EQUAL_INT32(VERSION_MAJOR, r.fsCurrentMajor);
    TEST_ASSERT_EQUAL_INT32(VERSION_MINOR, r.fsCurrentMinor);
}

static void test_compare_fs_uses_core_sys(void) {
    fileCompareResult r;
    r.fileType = FILE_TYPE_FILESYSTEM;
    r.majorDiff = 0; r.minorDiff = 1; r.dateDiff = 0; r.buildDiff = 0;
    TEST_ASSERT_EQUAL_INT8(1, core_ota.compareWithCurrentFsVersion(&r, "fs.bin"));
    TEST_ASSERT_EQUAL_INT32(0, r.fsCurrentMajor);
    TEST_ASSERT_EQUAL_INT32(1, r.fsCurrentMinor);
}

static void test_compare_equal(void) {
    fileCompareResult r;
    r.fileType = FILE_TYPE_FIRMWARE;
    r.majorDiff = 0; r.minorDiff = 0; r.dateDiff = 0; r.buildDiff = 0;
    TEST_ASSERT_EQUAL_INT8(0, core_ota.compareWithCurrentFsVersion(&r, "fw.bin"));
}

// ============================================================
// L2: LED-макросы, prepareSizesForUpdate, ConfigureOTA
// ============================================================
void ledMacrosUpdateFirmware();
void ledMacrosUpdateFilesystem();
void ledMacrosUpdateError();

static void test_led_macros_callable(void) {
    ledMacrosUpdateFirmware();
    ledMacrosUpdateFilesystem();
    ledMacrosUpdateError();
    TEST_PASS();
}

static void test_prepare_sizes_for_update(void) {
    TestOta t;
    t.prepareSizesForUpdate();
    TEST_ASSERT_TRUE(t.maxSketchSpace > 0);
    TEST_ASSERT_TRUE(t.freeSketchSpace > 0);
}

static void test_configure_ota_success(void) {
    TestOta t;
    TEST_ASSERT_TRUE(t.ConfigureOTA("host", "pass"));
}

static void test_configure_ota_rejects_empty(void) {
    TestOta t;
    TEST_ASSERT_FALSE(t.ConfigureOTA("", "pass"));
    TEST_ASSERT_FALSE(t.ConfigureOTA("host", ""));
}

static void test_register_custom_routes(void) {
    core_ota.registerCustomRoutes();
    TEST_PASS();
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_led_macros_callable);
    RUN_TEST(test_prepare_sizes_for_update);
    RUN_TEST(test_configure_ota_success);
    RUN_TEST(test_configure_ota_rejects_empty);
    RUN_TEST(test_register_custom_routes);
    RUN_TEST(test_compare_versions);
    RUN_TEST(test_valid_filename);
    RUN_TEST(test_filename_length_limit);
    RUN_TEST(test_filename_upgrade_allowed);
    RUN_TEST(test_filename_downgrade_blocked);
    RUN_TEST(test_filename_equal_blocked);
    RUN_TEST(test_filename_wrong_env);
    RUN_TEST(test_filename_garbage_and_empty);
    RUN_TEST(test_fs_end_sets_flag);
    RUN_TEST(test_fs_end_without_fs);
    RUN_TEST(test_fs_remount);
    RUN_TEST(test_compare_firmware_uses_build_version);
    RUN_TEST(test_compare_fs_uses_core_sys);
    RUN_TEST(test_compare_equal);
    return UNITY_END();
}
