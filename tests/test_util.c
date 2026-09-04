// deps: util.c
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "test_helpers.h"
#include "util.h"

static void test_utf8(void)
{
    CHECK(utf8_valid("hello"), "ASCII should be valid UTF-8");
    CHECK(utf8_valid("caf\xc3\xa9"), "cafe with a valid 2-byte sequence should be valid UTF-8");
    CHECK(!utf8_valid("caf\xc3"), "a truncated multi-byte sequence should be invalid UTF-8");
    CHECK(!utf8_valid("\xed\xa0\x80"), "an encoded surrogate half should be invalid UTF-8");
    CHECK(!utf8_valid("\xc0\x80"), "an overlong encoding should be invalid UTF-8");

    CHECK(utf8_strlen("caf\xc3\xa9") == 4, "cafe (with e-acute) should be 4 codepoints");

    char *sub = utf8_substr_alloc("caf\xc3\xa9s", 4);
    CHECK_STR_EQ(sub, "caf\xc3\xa9", "utf8_substr_alloc(4) should cut after the e-acute, not split it");
    free(sub);
}

static void test_sanitize_string(void)
{
    char *s = sanitize_string_alloc("<b>hello</b>   world", 255);
    CHECK_STR_EQ(s, "hello  world", "tags stripped, 3+ run of spaces collapsed to 2");
    free(s);

    char *s2 = sanitize_string_alloc("  padded  ", 255);
    CHECK_STR_EQ(s2, "padded", "leading/trailing whitespace should be trimmed");
    free(s2);

    char *s3 = sanitize_string_alloc("abcdef", 3);
    CHECK_STR_EQ(s3, "abc", "should truncate to max_len codepoints");
    free(s3);
}

static void test_html_and_json_escape(void)
{
    char *h = html_escape_alloc("<b>a & \"b\" 'c'</b>");
    CHECK_STR_EQ(h, "&lt;b&gt;a &amp; &quot;b&quot; &#039;c&#039;&lt;/b&gt;", "HTML escaping");
    free(h);

    char *j = json_escape_alloc("a\"b\\c\nd");
    CHECK_STR_EQ(j, "a\\\"b\\\\c\\nd", "JSON escaping");
    free(j);
}

/* SSRF/private-IP filter -- see the security audit's U1/U2 findings. Every
 * string below must be rejected (treated as an internal/obfuscated host);
 * every hostname must be accepted (not a literal IP at all). */
static void test_ssrf_filter(void)
{
    static const char *internal[] = {
        "127.0.0.1", "169.254.169.254", "10.0.0.5", "192.168.1.1", "172.16.0.1",
        "localhost", "metadata.google.internal",
        "::1", "[::1]", "::ffff:127.0.0.1",
        "::127.0.0.1", "::169.254.169.254",          /* IPv4-compatible IPv6 */
        "2130706433", "0x7f000001", "017700000001", "127.1", /* obfuscated IPv4 */
        NULL
    };
    for (int i = 0; internal[i]; i++) {
        CHECK(is_internal_host(internal[i]) == 1, internal[i]);
    }

    static const char *external[] = {
        "ctftime.org", "nnsc.tf", "api.example.com", "sub.domain.co.uk",
        "example-with-dashes.com", "a.b", "8.8.8.8" /* public IP, not private/reserved */,
        NULL
    };
    for (int i = 0; external[i]; i++) {
        CHECK(is_internal_host(external[i]) == 0, external[i]);
    }
}

static void test_iso8601(void)
{
    time_t t = 0;
    CHECK(parse_iso8601("2026-03-28T18:00:00+00:00", &t), "valid RFC3339 with offset should parse");
    CHECK(parse_iso8601("2026-03-28T18:00:00Z", &t), "valid RFC3339 with Z should parse");

    CHECK(!parse_iso8601("2026-13-01T00:00:00Z", &t), "month 13 should be rejected");
    CHECK(!parse_iso8601("2026-01-45T00:00:00Z", &t), "day 45 should be rejected");
    CHECK(!parse_iso8601("2026-01-01T99:00:00Z", &t), "hour 99 should be rejected");
    CHECK(!parse_iso8601("2026-01-01T00:99:00Z", &t), "minute 99 should be rejected");
    CHECK(!parse_iso8601("not a date", &t), "garbage should be rejected");

    /* +02:00 means the wall-clock time is 2 hours ahead of UTC, so the UTC
     * instant is 2 hours *earlier*. */
    time_t t_plus, t_z;
    parse_iso8601("2026-01-01T10:00:00+02:00", &t_plus);
    parse_iso8601("2026-01-01T08:00:00Z", &t_z);
    CHECK(t_plus == t_z, "timezone offset should be applied correctly");
}

static void test_mysql_datetime_roundtrip(void)
{
    char buf[20];
    format_mysql_datetime(1780329600, buf, sizeof(buf)); /* 2026-05-30 00:00:00 UTC-ish */
    time_t back = 0;
    CHECK(parse_mysql_datetime(buf, &back), "formatted datetime should parse back");
    CHECK(back == 1780329600, "round-tripped timestamp should match");
}

int main(void)
{
    test_utf8();
    test_sanitize_string();
    test_html_and_json_escape();
    test_ssrf_filter();
    test_iso8601();
    test_mysql_datetime_roundtrip();
    TEST_SUMMARY();
}
