// deps: json.c
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "test_helpers.h"

static void test_basic_parse(void)
{
    const char *text =
        "{\"id\":2345,\"title\":\"Some\\u00e9CTF \\ud83d\\udea9\",\"weight\":74.85,"
        "\"onsite\":false,\"nested\":{\"a\":[1,2,3],\"b\":null},\"arr\":[]}";
    json_value_t *v = json_parse(text, strlen(text), 16);
    CHECK(v != NULL, "basic object should parse");
    if (!v) return;

    CHECK_STR_EQ(json_get_string(v, "title", "<none>"), "Some\xc3\xa9""CTF \xf0\x9f\x9a\xa9",
                 "title should decode \\u escapes and surrogate pairs to UTF-8");

    double w = 0;
    CHECK(json_get_number(v, "weight", &w) && w == 74.85, "weight should parse as 74.85");
    CHECK(json_get_bool(v, "onsite", -1) == 0, "onsite should be false");

    json_value_t *nested = json_object_get(v, "nested");
    json_value_t *a = json_object_get(nested, "a");
    CHECK(json_array_size(a) == 3, "nested array should have 3 elements");
    CHECK(json_array_get(a, 1)->u.number == 2.0, "a[1] should be 2");
    CHECK(json_array_size(json_object_get(v, "arr")) == 0, "empty array should have size 0");

    long id = 0;
    CHECK(json_get_int(v, "id", &id) && id == 2345, "id should parse as 2345");

    json_free(v);
}

static void test_malformed(void)
{
    CHECK(json_parse("{bad", 4, 16) == NULL, "truncated object should fail to parse");
    CHECK(json_parse("", 0, 16) == NULL, "empty input should fail to parse");
    CHECK(json_parse("{\"a\":1,}", 8, 16) == NULL, "trailing comma should fail to parse");
    CHECK(json_parse("[1,2,]", 6, 16) == NULL, "trailing comma in array should fail to parse");
    CHECK(json_parse("{\"a\" 1}", 7, 16) == NULL, "missing colon should fail to parse");
    CHECK(json_parse("nul", 3, 16) == NULL, "truncated literal should fail to parse");
    CHECK(json_parse("{\"a\":1} garbage", 15, 16) == NULL, "trailing garbage should fail to parse");
}

static void test_depth_limit(void)
{
    const char *deep = "[[[[[[[[[[1]]]]]]]]]]"; /* 10 levels of nesting */
    json_value_t *ok = json_parse(deep, strlen(deep), 16);
    CHECK(ok != NULL, "10 levels of nesting should parse under a depth limit of 16");
    json_free(ok);

    json_value_t *rejected = json_parse(deep, strlen(deep), 3);
    CHECK(rejected == NULL, "10 levels of nesting should be rejected under a depth limit of 3");
}

static void test_null_byte_escape_dropped(void)
{
    /* A U+0000 escape inside a JSON string is dropped rather than embedded, since the
     * rest of this codebase treats decoded strings as plain C strings. */
    const char *text = "{\"a\":\"x\\u0000y\"}";
    json_value_t *v = json_parse(text, strlen(text), 16);
    CHECK(v != NULL, "string with a \\u0000 escape should still parse");
    CHECK_STR_EQ(json_get_string(v, "a", "?"), "xy", "\\u0000 should be dropped, not embedded");
    json_free(v);
}

int main(void)
{
    test_basic_parse();
    test_malformed();
    test_depth_limit();
    test_null_byte_escape_dropped();
    TEST_SUMMARY();
}
