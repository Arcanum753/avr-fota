#include "override_prelude.h"

#include "../../../../src/core_web/common_module.cpp"

#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

static String ct(const char* name) {
    AsyncWebServerRequest req;
    return ns_core_web::getContentType(String(name), &req);
}

static void test_content_type_extensions(void) {
    TEST_ASSERT_EQUAL_STRING("text/html", ct("a.htm").c_str());
    TEST_ASSERT_EQUAL_STRING("text/html", ct("a.html").c_str());
    TEST_ASSERT_EQUAL_STRING("text/css", ct("a.css").c_str());
    TEST_ASSERT_EQUAL_STRING("application/json", ct("a.json").c_str());
    TEST_ASSERT_EQUAL_STRING("application/javascript", ct("a.js").c_str());
    TEST_ASSERT_EQUAL_STRING("image/png", ct("a.png").c_str());
    TEST_ASSERT_EQUAL_STRING("image/gif", ct("a.gif").c_str());
    TEST_ASSERT_EQUAL_STRING("image/jpeg", ct("a.jpg").c_str());
    TEST_ASSERT_EQUAL_STRING("image/x-icon", ct("a.ico").c_str());
    TEST_ASSERT_EQUAL_STRING("text/xml", ct("a.xml").c_str());
    TEST_ASSERT_EQUAL_STRING("application/x-pdf", ct("a.pdf").c_str());
    TEST_ASSERT_EQUAL_STRING("application/x-zip", ct("a.zip").c_str());
    TEST_ASSERT_EQUAL_STRING("application/x-gzip", ct("a.gz").c_str());
    TEST_ASSERT_EQUAL_STRING("text/html", ct("a.hex").c_str());
}

static void test_content_type_default(void) {
    TEST_ASSERT_EQUAL_STRING("text/plain", ct("noext").c_str());
    TEST_ASSERT_EQUAL_STRING("text/plain", ct("a.HTML").c_str()); // регистрозависимо
}

static void test_content_type_download(void) {
    AsyncWebServerRequest req;
    req.addArg("download", "1");
    TEST_ASSERT_EQUAL_STRING("application/octet-stream",
                             ns_core_web::getContentType("a.html", &req).c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_content_type_extensions);
    RUN_TEST(test_content_type_default);
    RUN_TEST(test_content_type_download);
    return UNITY_END();
}
