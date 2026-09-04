// deps: json.c util.c event.c content_security.c
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "content_security.h"
#include "json.h"
#include "test_helpers.h"

static int sanitize_from_json(const char *json_text, unsigned int id, ctf_event_t *ev)
{
    json_value_t *raw = json_parse(json_text, strlen(json_text), 16);
    if (!raw) {
        return -1;
    }
    char fallback[64];
    snprintf(fallback, sizeof(fallback), "https://ctftime.org/event/%u", id);
    int ok = content_security_sanitize(raw, id, fallback, ev);
    json_free(raw);
    return ok;
}

static void test_normal_event(void)
{
    ctf_event_t ev;
    int ok = sanitize_from_json(
        "{\"id\":1,\"title\":\"NNS CTF 2026\",\"description\":\"Join us <b>now</b>!\","
        "\"url\":\"https://nnsc.tf/\",\"ctftime_url\":\"https://ctftime.org/event/1/\","
        "\"weight\":25.5,\"onsite\":false}",
        1, &ev);
    CHECK(ok == 1, "well-formed event should be accepted");
    CHECK(ev.is_safe == 1, "well-formed event should be marked safe");
    CHECK_STR_EQ(ev.title, "NNS CTF 2026", "title should round-trip");
    CHECK(ev.has_url == 1, "url should be accepted");
    CHECK(ev.description && strcmp(ev.description, "Join us now!") == 0, "description tags should be stripped");
    ctf_event_free(&ev);
}

static void test_empty_title_rejected(void)
{
    ctf_event_t ev;
    int ok = sanitize_from_json("{\"id\":1,\"title\":\"\"}", 1, &ev);
    CHECK(ok == 0, "an event with an empty title must be rejected entirely");
}

static void test_xss_and_ssti_flagged_unsafe(void)
{
    ctf_event_t ev;
    int ok = sanitize_from_json("{\"id\":1,\"title\":\"<script>alert(1)</script> {{7*7}}\"}", 1, &ev);
    CHECK(ok == 1, "an XSS/SSTI title should still be stored");
    CHECK(ev.is_safe == 0, "an XSS/SSTI title must be flagged unsafe");
    CHECK(strstr(ev.title, "<script>") == NULL, "raw <script> tag must not survive into the stored title");
    ctf_event_free(&ev);
}

static void test_tag_split_ssti_evasion_is_caught(void)
{
    /* Regression test for the audit's CS1 finding: checking only the raw
     * (pre-strip_tags) value let a tag-split payload reassemble into a
     * flagged pattern after sanitization without ever being caught. */
    ctf_event_t ev;
    int ok = sanitize_from_json("{\"id\":1,\"title\":\"<div>{</div>{7*7}}\"}", 1, &ev);
    CHECK(ok == 1, "tag-split SSTI payload should still be stored");
    CHECK_STR_EQ(ev.title, "{{7*7}}", "tags should be stripped, reassembling the payload");
    CHECK(ev.is_safe == 0, "the reassembled SSTI pattern must be caught and flagged unsafe");
    ctf_event_free(&ev);
}

static void test_sqli_pattern_flagged_unsafe(void)
{
    ctf_event_t ev;
    int ok = sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"description\":\"1; DROP TABLE users x\"}", 1, &ev);
    CHECK(ok == 1, "event with a SQLi-shaped description should still be stored");
    CHECK(ev.is_safe == 0, "SQLi-shaped description must be flagged unsafe");
    ctf_event_free(&ev);
}

static void test_ssrf_urls_rejected(void)
{
    ctf_event_t ev1;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"url\":\"http://169.254.169.254/latest/meta-data/\"}", 1, &ev1);
    CHECK(ev1.has_url == 0, "cloud metadata URL must be rejected");
    CHECK(ev1.is_safe == 0, "cloud metadata URL must flag the event unsafe");
    ctf_event_free(&ev1);

    ctf_event_t ev2;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"url\":\"http://192.168.1.1/admin\"}", 1, &ev2);
    CHECK(ev2.has_url == 0, "private-IP URL must be rejected");
    ctf_event_free(&ev2);

    ctf_event_t ev3;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"url\":\"javascript:alert(1)\"}", 1, &ev3);
    CHECK(ev3.has_url == 0, "javascript: scheme must be rejected");
    ctf_event_free(&ev3);

    ctf_event_t ev4;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"url\":\"https://user:pass@evil.com/\"}", 1, &ev4);
    CHECK(ev4.has_url == 0, "credentials-in-URL must be rejected");
    ctf_event_free(&ev4);
}

static void test_url_metacharacters_rejected(void)
{
    /* Regression test for the audit's CS2 hardening: a URL should never be
     * accepted with a raw quote/angle-bracket, even though formatter.c also
     * HTML-escapes it at output time -- defense in depth. */
    ctf_event_t ev;
    sanitize_from_json(
        "{\"id\":1,\"title\":\"Fine\",\"url\":\"https://example.com/x\\\"><svg/onload=alert(1)>\"}",
        1, &ev);
    CHECK(ev.has_url == 0, "a URL containing a raw '\"'/'<'/'>' must be rejected");
    ctf_event_free(&ev);
}

static void test_ctftime_domain_allowlist(void)
{
    ctf_event_t ev;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"ctftime_url\":\"https://evil.com/event/1\"}", 1, &ev);
    CHECK(ev.has_ctftime_url == 0, "a ctftime_url pointing off-domain must be rejected");
    ctf_event_free(&ev);

    ctf_event_t ev2;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\"}", 1, &ev2);
    CHECK(ev2.has_ctftime_url == 1, "a missing ctftime_url should fall back to the constructed one");
    ctf_event_free(&ev2);
}

static void test_weight_range(void)
{
    ctf_event_t ev1;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"weight\":1e300}", 1, &ev1);
    CHECK(ev1.has_weight == 0, "a weight far outside DECIMAL(8,5) range must be dropped");
    ctf_event_free(&ev1);

    ctf_event_t ev2;
    sanitize_from_json("{\"id\":1,\"title\":\"Fine\",\"weight\":74.85}", 1, &ev2);
    CHECK(ev2.has_weight == 1, "a normal weight must be kept");
    CHECK(fabs(ev2.weight - 74.85) < 0.0001, "weight value should round-trip");
    ctf_event_free(&ev2);
}

static void test_forced_id_overrides_body(void)
{
    /* Anti-spoofing: the id always comes from the request path, never the
     * response body (the JSON here doesn't even carry an "id" field). */
    ctf_event_t ev;
    int ok = sanitize_from_json("{\"title\":\"Fine\"}", 777, &ev);
    CHECK(ok == 1, "event without an id field in the body should still be accepted");
    CHECK(ev.id == 777, "id must come from forced_id, not the response body");
    ctf_event_free(&ev);
}

int main(void)
{
    test_normal_event();
    test_empty_title_rejected();
    test_xss_and_ssti_flagged_unsafe();
    test_tag_split_ssti_evasion_is_caught();
    test_sqli_pattern_flagged_unsafe();
    test_ssrf_urls_rejected();
    test_url_metacharacters_rejected();
    test_ctftime_domain_allowlist();
    test_weight_range();
    test_forced_id_overrides_body();
    TEST_SUMMARY();
}
