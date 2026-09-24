#include "override_prelude.h"
#include "../../../../src/common/common.cpp"

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

// ============================================================
// hex2bin / h2int
// ============================================================

static void test_hex2bin_valid(void) {
    TEST_ASSERT_EQUAL_UINT8(0, hex2bin('0'));
    TEST_ASSERT_EQUAL_UINT8(9, hex2bin('9'));
    TEST_ASSERT_EQUAL_UINT8(10, hex2bin('A'));
    TEST_ASSERT_EQUAL_UINT8(15, hex2bin('F'));
    TEST_ASSERT_EQUAL_UINT8(10, hex2bin('a'));
    TEST_ASSERT_EQUAL_UINT8(15, hex2bin('f'));
}

static void test_hex2bin_invalid(void) {
    TEST_ASSERT_EQUAL_UINT8(0xFF, hex2bin('g'));
    TEST_ASSERT_EQUAL_UINT8(0xFF, hex2bin(' '));
    TEST_ASSERT_EQUAL_UINT8(0xFF, hex2bin(0));
}

static void test_h2int(void) {
    TEST_ASSERT_EQUAL_UINT8(0, h2int('0'));
    TEST_ASSERT_EQUAL_UINT8(15, h2int('F'));
    TEST_ASSERT_EQUAL_UINT8(15, h2int('f'));
    TEST_ASSERT_EQUAL_UINT8(0, h2int('z'));
}

// ============================================================
// escapeHtml
// ============================================================

static void test_escape_html_basic(void) {
    TEST_ASSERT_EQUAL_STRING("&lt;b&gt;", escapeHtml("<b>").c_str());
    TEST_ASSERT_EQUAL_STRING("a&amp;b", escapeHtml("a&b").c_str());
    TEST_ASSERT_EQUAL_STRING("&quot;x&quot;", escapeHtml("\"x\"").c_str());
}

static void test_escape_html_cvt_separator(void) {
    TEST_ASSERT_EQUAL_STRING("a&#124;b", escapeHtml("a|b").c_str());
}

static void test_escape_html_newlines(void) {
    // Каждый из CR и LF заменяется пробелом, поэтому CRLF даёт два пробела.
    TEST_ASSERT_EQUAL_STRING("a  b", escapeHtml("a\r\nb").c_str());
    TEST_ASSERT_EQUAL_STRING("a b", escapeHtml("a\nb").c_str());
}

static void test_escape_html_cyrillic_passthrough(void) {
    TEST_ASSERT_EQUAL_STRING("Привет", escapeHtml("Привет").c_str());
}

static void test_escape_html_empty(void) {
    TEST_ASSERT_EQUAL_STRING("", escapeHtml("").c_str());
}

// ============================================================
// escapeJson
// ============================================================

static void test_escape_json_quotes(void) {
    TEST_ASSERT_EQUAL_STRING("\\\"x\\\"", escapeJson("\"x\"").c_str());
}

static void test_escape_json_backslash(void) {
    TEST_ASSERT_EQUAL_STRING("a\\\\b", escapeJson("a\\b").c_str());
}

static void test_escape_json_controls(void) {
    TEST_ASSERT_EQUAL_STRING("a\\nb", escapeJson("a\nb").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\rb", escapeJson("a\rb").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\tb", escapeJson("a\tb").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\bb", escapeJson(String("a") + (char)8 + "b").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\fb", escapeJson(String("a") + (char)12 + "b").c_str());
}

static void test_escape_json_low_bytes(void) {
    // 0x01 -> \u0001
    char in[3] = { 'a', 1, 0 };
    TEST_ASSERT_EQUAL_STRING("a\\u0001", escapeJson(in).c_str());
}

static void test_escape_json_cyrillic_passthrough(void) {
    TEST_ASSERT_EQUAL_STRING("Привет", escapeJson("Привет").c_str());
}

// ============================================================
// urldecode
// ============================================================

static void test_urldecode_plus(void) {
    TEST_ASSERT_EQUAL_STRING("a b", urldecode("a+b").c_str());
}

static void test_urldecode_percent(void) {
    TEST_ASSERT_EQUAL_STRING("A B", urldecode("%41%20B").c_str());
    TEST_ASSERT_EQUAL_STRING("~", urldecode("%7e").c_str());
}

static void test_urldecode_plain(void) {
    TEST_ASSERT_EQUAL_STRING("hello", urldecode("hello").c_str());
}

// ============================================================
// checkRange
// ============================================================

static void test_check_range(void) {
    TEST_ASSERT_TRUE(checkRange("0"));
    TEST_ASSERT_TRUE(checkRange("255"));
    TEST_ASSERT_FALSE(checkRange("256"));
    TEST_ASSERT_FALSE(checkRange("-1"));
    TEST_ASSERT_TRUE(checkRange("128"));
}

// ============================================================
// formatBytes
// ============================================================

static void test_format_bytes(void) {
    TEST_ASSERT_EQUAL_STRING("0B", formatBytes(0).c_str());
    TEST_ASSERT_EQUAL_STRING("1023B", formatBytes(1023).c_str());
    TEST_ASSERT_EQUAL_STRING("1.00KB", formatBytes(1024).c_str());
    TEST_ASSERT_EQUAL_STRING("1.00MB", formatBytes(1024UL * 1024UL).c_str());
    TEST_ASSERT_EQUAL_STRING("1.00GB", formatBytes(1024UL * 1024UL * 1024UL).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_hex2bin_valid);
    RUN_TEST(test_hex2bin_invalid);
    RUN_TEST(test_h2int);

    RUN_TEST(test_escape_html_basic);
    RUN_TEST(test_escape_html_cvt_separator);
    RUN_TEST(test_escape_html_newlines);
    RUN_TEST(test_escape_html_cyrillic_passthrough);
    RUN_TEST(test_escape_html_empty);

    RUN_TEST(test_escape_json_quotes);
    RUN_TEST(test_escape_json_backslash);
    RUN_TEST(test_escape_json_controls);
    RUN_TEST(test_escape_json_low_bytes);
    RUN_TEST(test_escape_json_cyrillic_passthrough);

    RUN_TEST(test_urldecode_plus);
    RUN_TEST(test_urldecode_percent);
    RUN_TEST(test_urldecode_plain);

    RUN_TEST(test_check_range);
    RUN_TEST(test_format_bytes);

    return UNITY_END();
}
