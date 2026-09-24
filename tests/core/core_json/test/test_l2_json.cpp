#include "override_prelude.h"
#include "LittleFS.h"

#include "../../../../src/core_json/core_json.cpp"
#include "../../../../src/core_json/core_json_engine.cpp"

#include <unity.h>

void setUp(void) {
    LittleFS.clear();
    core_json.setFs(&LittleFS);
}
void tearDown(void) {}

// ============================================================
// Файловые write/read
// ============================================================
static void test_file_rw_scalar(void) {
    TEST_ASSERT_TRUE(core_json.jsonFileWriteStr("/c.json", "name", "dev"));
    TEST_ASSERT_TRUE(core_json.jsonFileWriteInt("/c.json", "num", 42));
    TEST_ASSERT_TRUE(core_json.jsonFileWriteBool("/c.json", "flag", true));

    String s;
    int32_t i = 0;
    bool b = false;
    TEST_ASSERT_TRUE(core_json.jsonFileReadStr("/c.json", "name", s));
    TEST_ASSERT_EQUAL_STRING("dev", s.c_str());
    TEST_ASSERT_TRUE(core_json.jsonFileReadInt("/c.json", "num", i));
    TEST_ASSERT_EQUAL_INT32(42, i);
    TEST_ASSERT_TRUE(core_json.jsonFileReadBool("/c.json", "flag", b));
    TEST_ASSERT_TRUE(b);
}

static void test_file_merge_preserves_fields(void) {
    core_json.jsonFileWriteStr("/m.json", "a", "keep");
    core_json.jsonFileWriteInt("/m.json", "b", 7);

    String a;
    int32_t b = 0;
    TEST_ASSERT_TRUE(core_json.jsonFileReadStr("/m.json", "a", a));
    TEST_ASSERT_TRUE(core_json.jsonFileReadInt("/m.json", "b", b));
    TEST_ASSERT_EQUAL_STRING("keep", a.c_str());
    TEST_ASSERT_EQUAL_INT32(7, b);
}

static void test_file_read_missing(void) {
    String s;
    int32_t i = 0;
    bool b = false;
    TEST_ASSERT_FALSE(core_json.jsonFileReadStr("/nope.json", "x", s));
    TEST_ASSERT_FALSE(core_json.jsonFileReadInt("/nope.json", "x", i));
    TEST_ASSERT_FALSE(core_json.jsonFileReadBool("/nope.json", "x", b));
}

// ============================================================
// Парсинг строк
// ============================================================
static void test_parse_scalar(void) {
    const char* json = "{\"s\":\"hi\",\"i\":5,\"b\":true}";
    String s;
    int32_t i = 0;
    bool b = false;
    TEST_ASSERT_TRUE(core_json.jsonParseStr(json, "s", s));
    TEST_ASSERT_TRUE(core_json.jsonParseInt(json, "i", i));
    TEST_ASSERT_TRUE(core_json.jsonParseBool(json, "b", b));
    TEST_ASSERT_EQUAL_STRING("hi", s.c_str());
    TEST_ASSERT_EQUAL_INT32(5, i);
    TEST_ASSERT_TRUE(b);
}

static void test_parse_invalid_json(void) {
    String s;
    TEST_ASSERT_FALSE(core_json.jsonParseStr("{invalid", "s", s));
}

static void test_parse_nested(void) {
    const char* json = "{\"filesystem\":{\"version\":{\"full_string\":\"1.2.3\",\"build\":9}}}";
    String s;
    int32_t i = 0;
    int64_t i64 = 0;
    TEST_ASSERT_TRUE(core_json.jsonParseNestedStr(json, "filesystem|version|full_string", s));
    TEST_ASSERT_EQUAL_STRING("1.2.3", s.c_str());
    TEST_ASSERT_TRUE(core_json.jsonParseNestedInt(json, "filesystem|version|build", i));
    TEST_ASSERT_EQUAL_INT32(9, i);
    TEST_ASSERT_TRUE(core_json.jsonParseNestedInt64(json, "filesystem|version|build", i64));
    TEST_ASSERT_EQUAL_INT64(9, i64);
    TEST_ASSERT_FALSE(core_json.jsonParseNestedStr(json, "filesystem|nope", s));
}

static void test_array_helpers(void) {
    const char* json = "{\"files\":[{\"name\":\"a\"},{\"name\":\"b\"}]}";
    TEST_ASSERT_EQUAL_INT(2, core_json.jsonGetArraySize(json, "files"));
    TEST_ASSERT_EQUAL_INT(0, core_json.jsonGetArraySize(json, "missing"));
    String s;
    TEST_ASSERT_TRUE(core_json.jsonGetArrayStr(json, "files", 1, "name", s));
    TEST_ASSERT_EQUAL_STRING("b", s.c_str());
    TEST_ASSERT_FALSE(core_json.jsonGetArrayStr(json, "files", 5, "name", s));
}

static void test_builders(void) {
    String s = core_json.jsonBuildObj("k", "v");
    TEST_ASSERT_TRUE(s.indexOf("\"k\"") >= 0);
    TEST_ASSERT_TRUE(s.indexOf("\"v\"") >= 0);
    String n = core_json.jsonBuildObjInt("n", 12);
    TEST_ASSERT_TRUE(n.indexOf("12") >= 0);
}

// ============================================================
// Слот-конфиг (round-trip)
// ============================================================
static void test_slot_config_roundtrip(void) {
    IPAddress ip(192, 168, 1, 10);
    IPAddress nm(255, 255, 255, 0);
    IPAddress gw(192, 168, 1, 1);
    IPAddress dns(8, 8, 8, 8);

    String json = core_json.jsonBuildSlotConfig("ssid1", "pass1", true, ip, nm, gw, dns);
    String ssid, pass;
    bool dhcp = false;
    IPAddress ip2, nm2, gw2, dns2;
    int count = core_json.jsonParseSlotConfig(json, ssid, pass, dhcp, ip2, nm2, gw2, dns2);
    TEST_ASSERT_EQUAL_INT(7, count);
    TEST_ASSERT_EQUAL_STRING("ssid1", ssid.c_str());
    TEST_ASSERT_EQUAL_STRING("pass1", pass.c_str());
    TEST_ASSERT_TRUE(dhcp);
    TEST_ASSERT_TRUE(ip == ip2);
    TEST_ASSERT_TRUE(dns == dns2);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_file_rw_scalar);
    RUN_TEST(test_file_merge_preserves_fields);
    RUN_TEST(test_file_read_missing);
    RUN_TEST(test_parse_scalar);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_nested);
    RUN_TEST(test_array_helpers);
    RUN_TEST(test_builders);
    RUN_TEST(test_slot_config_roundtrip);
    return UNITY_END();
}
