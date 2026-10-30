#include <unity.h>

#include "../../../../src/core_fs/common_module.cpp"
#include "../../../../src/core_fs/core_fs_engine.cpp"

using namespace ns_core_fs;

void setUp(void) {}
void tearDown(void) {}

// ============================================================
// L1: пути и режимы
// ============================================================

static void test_normalize(void) {
    TEST_ASSERT_EQUAL_STRING("/a/b", fsNormalizePath("a\\\\b").c_str());
    TEST_ASSERT_EQUAL_STRING("/a/b", fsNormalizePath("/a//b/").c_str());
    TEST_ASSERT_EQUAL_STRING("/", fsNormalizePath("").c_str());
    TEST_ASSERT_EQUAL_STRING("/", fsNormalizePath("/").c_str());
    TEST_ASSERT_EQUAL_STRING("/macros/a.lua", fsNormalizePath("  macros/a.lua  ").c_str());
}

static void test_safe(void) {
    TEST_ASSERT_TRUE(fsPathIsSafe("/a"));
    TEST_ASSERT_TRUE(fsPathIsSafe("/macros/a.lua"));
    TEST_ASSERT_FALSE(fsPathIsSafe("/"));
    TEST_ASSERT_FALSE(fsPathIsSafe(""));
    TEST_ASSERT_FALSE(fsPathIsSafe("a"));
    TEST_ASSERT_FALSE(fsPathIsSafe("/a/../b"));
    TEST_ASSERT_FALSE(fsPathIsSafe("/a/./b"));
    TEST_ASSERT_FALSE(fsPathIsSafe("/a//b"));

    String longPath = "/";
    while (longPath.length() <= CORE_FS_MAX_PATH) { longPath += 'x'; }
    TEST_ASSERT_FALSE(fsPathIsSafe(longPath));

    String longName = "/x";
    while (longName.length() <= CORE_FS_MAX_NAME) { longName += 'x'; }
    TEST_ASSERT_FALSE(fsPathIsSafe(longName));
}

static void test_secret_and_modes(void) {
    TEST_ASSERT_TRUE(fsIsSecretPath("/secret.json"));
    TEST_ASSERT_TRUE(fsIsSecretPath("/SECRET.JSON"));
    TEST_ASSERT_FALSE(fsIsSecretPath("/config_sys.json"));

    TEST_ASSERT_TRUE(fsWriteAllowed(CORE_MODE_INIT));
    TEST_ASSERT_TRUE(fsWriteAllowed(CORE_MODE_NORMAL));
    TEST_ASSERT_TRUE(fsWriteAllowed(CORE_MODE_TEST));
    TEST_ASSERT_FALSE(fsWriteAllowed(CORE_MODE_OTA));
    TEST_ASSERT_FALSE(fsWriteAllowed(CORE_MODE_FS_UPDATE));
    TEST_ASSERT_FALSE(fsWriteAllowed(CORE_MODE_PROG));
}

// ============================================================
// L2: операции с mock LittleFS
// ============================================================

static void prepareFs(void) {
    LittleFS.clear();
    LittleFS.mkdir("/macros");
    LittleFS.put("/config_sys.json", "{}");
    LittleFS.put("/macros/a.lua", "print(1)");
    LittleFS.put("/macros/b.lua", "print(2)");
}

static void test_collect(void) {
    prepareFs();
    FsEntry entries[16];
    size_t count = 0;
    bool truncated = false;
    TEST_ASSERT_TRUE(fsCollectEntries(LittleFS, entries, 16, count, truncated));
    TEST_ASSERT_FALSE(truncated);
    TEST_ASSERT_EQUAL_UINT(4, count);
    TEST_ASSERT_EQUAL_STRING("/config_sys.json", entries[0].path.c_str());
    TEST_ASSERT_EQUAL_STRING("/macros", entries[1].path.c_str());
    TEST_ASSERT_TRUE(entries[1].dir);
    TEST_ASSERT_EQUAL_STRING("/macros/a.lua", entries[2].path.c_str());
    TEST_ASSERT_EQUAL_UINT(8, entries[2].size);
    TEST_ASSERT_EQUAL_STRING("/macros/b.lua", entries[3].path.c_str());
}

static void test_collect_truncated(void) {
    prepareFs();
    FsEntry entries[2];
    size_t count = 0;
    bool truncated = false;
    TEST_ASSERT_TRUE(fsCollectEntries(LittleFS, entries, 2, count, truncated));
    TEST_ASSERT_TRUE(truncated);
    TEST_ASSERT_EQUAL_UINT(2, count);
}

static void test_create_rename_delete(void) {
    LittleFS.clear();
    TEST_ASSERT_TRUE(fsCreateDir(LittleFS, "/tmp"));
    TEST_ASSERT_TRUE(fsCreateFile(LittleFS, "/tmp/a.txt"));
    TEST_ASSERT_TRUE(LittleFS.hasFile("/tmp/a.txt"));
    TEST_ASSERT_FALSE(fsCreateFile(LittleFS, "/no/parent.txt"));

    TEST_ASSERT_TRUE(fsRenamePath(LittleFS, "/tmp/a.txt", "/tmp/b.txt"));
    TEST_ASSERT_FALSE(LittleFS.hasFile("/tmp/a.txt"));
    TEST_ASSERT_TRUE(LittleFS.hasFile("/tmp/b.txt"));

    bool notEmpty = false;
    TEST_ASSERT_TRUE(fsRemovePath(LittleFS, "/tmp/b.txt", false, notEmpty));
    TEST_ASSERT_FALSE(LittleFS.hasFile("/tmp/b.txt"));
    TEST_ASSERT_TRUE(fsRemovePath(LittleFS, "/tmp", false, notEmpty));
    TEST_ASSERT_FALSE(LittleFS.hasDir("/tmp"));
}

static void test_delete_nonempty(void) {
    prepareFs();
    bool notEmpty = false;
    TEST_ASSERT_FALSE(fsRemovePath(LittleFS, "/macros", false, notEmpty));
    TEST_ASSERT_TRUE(notEmpty);
    TEST_ASSERT_TRUE(LittleFS.hasDir("/macros"));

    TEST_ASSERT_TRUE(fsRemovePath(LittleFS, "/macros", true, notEmpty));
    TEST_ASSERT_FALSE(LittleFS.hasDir("/macros"));
    TEST_ASSERT_FALSE(LittleFS.hasFile("/macros/a.lua"));
}

static void test_info(void) {
    prepareFs();
    uint32_t total = 0, used = 0;
    TEST_ASSERT_TRUE(fsBuildInfo(LittleFS, total, used));
    TEST_ASSERT_EQUAL_UINT(1024 * 1024, total);
    TEST_ASSERT_EQUAL_UINT((uint32_t)(2 + 8 + 8), used);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normalize);
    RUN_TEST(test_safe);
    RUN_TEST(test_secret_and_modes);
    RUN_TEST(test_collect);
    RUN_TEST(test_collect_truncated);
    RUN_TEST(test_create_rename_delete);
    RUN_TEST(test_delete_nonempty);
    RUN_TEST(test_info);
    return UNITY_END();
}
