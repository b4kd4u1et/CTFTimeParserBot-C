// deps: util.c event.c formatter.c
#include <stdlib.h>
#include <string.h>

#include "event.h"
#include "formatter.h"
#include "test_helpers.h"
#include "util.h"

/* Regression test for the audit's positive finding on formatter_event()'s
 * fixed `char *lines[9]` array: exercise every optional line (dates, meta,
 * onsite location, description, links) at once, the true worst case, and
 * make sure it produces sane output instead of corrupting memory (ASan/
 * valgrind would catch an overflow; this also checks the content). */
static void test_event_all_fields_present(void)
{
    ctf_event_t ev;
    ctf_event_init(&ev);
    ev.id = 1;
    snprintf(ev.title, sizeof(ev.title), "%s", "Some <b>CTF</b> & \"Friends\"");
    snprintf(ev.url, sizeof(ev.url), "%s", "https://example.com/");
    ev.has_url = 1;
    snprintf(ev.ctftime_url, sizeof(ev.ctftime_url), "%s", "https://ctftime.org/event/1/");
    ev.has_ctftime_url = 1;
    snprintf(ev.start_time, sizeof(ev.start_time), "%s", "2026-09-04 16:00:00");
    ev.has_start_time = 1;
    snprintf(ev.finish_time, sizeof(ev.finish_time), "%s", "2026-09-06 16:00:00");
    ev.has_finish_time = 1;
    snprintf(ev.format, sizeof(ev.format), "%s", "Jeopardy");
    ev.has_format = 1;
    ev.weight = 25.5;
    ev.has_weight = 1;
    ev.onsite = 1;
    snprintf(ev.location, sizeof(ev.location), "%s", "Berlin");
    ev.has_location = 1;
    ev.description = strdup("A great CTF with fun challenges.");
    ev.is_safe = 1;

    char *msg = formatter_event(&ev);
    CHECK(msg != NULL, "formatter_event should not return NULL");
    CHECK(strstr(msg, "&lt;b&gt;CTF&lt;/b&gt;") != NULL, "title HTML should be escaped");
    CHECK(strstr(msg, "&amp;") != NULL, "ampersand should be escaped");
    CHECK(strstr(msg, "Berlin") != NULL, "onsite location should appear");
    CHECK(strstr(msg, "Jeopardy") != NULL, "format should appear");
    CHECK(strstr(msg, "Weight: 25.50") != NULL, "weight should appear formatted to 2 decimals");
    CHECK(strstr(msg, "href=\"https://example.com/\"") != NULL, "event url should appear as an href");
    CHECK(strstr(msg, "href=\"https://ctftime.org/event/1/\"") != NULL, "ctftime url should appear as an href");
    CHECK(strstr(msg, "great CTF") != NULL, "description should appear");

    free(msg);
    ctf_event_free(&ev);
}

/* Regression test for the audit's CS2/formatter interaction: a URL with a
 * raw quote must never break out of the href="..." attribute. */
static void test_href_attribute_escaping(void)
{
    ctf_event_t ev;
    ctf_event_init(&ev);
    ev.id = 1;
    snprintf(ev.title, sizeof(ev.title), "%s", "Evil");
    /* Simulates a value that predates the CS2 input-side rejection --
     * output escaping must still hold on its own as a second layer. */
    snprintf(ev.url, sizeof(ev.url), "%s", "https://example.com/x\"><script>alert(1)</script>");
    ev.has_url = 1;
    ev.is_safe = 1;

    char *msg = formatter_event(&ev);
    CHECK(strstr(msg, "\"><script>") == NULL, "a raw quote must never break out of href=\"...\"");
    CHECK(strstr(msg, "&quot;&gt;&lt;script&gt;") != NULL, "the quote and tag should be HTML-escaped instead");

    free(msg);
    ctf_event_free(&ev);
}

static void test_online_vs_onsite(void)
{
    ctf_event_t ev;
    ctf_event_init(&ev);
    ev.id = 1;
    snprintf(ev.title, sizeof(ev.title), "%s", "Online CTF");
    ev.onsite = 0;

    char *msg = formatter_event(&ev);
    CHECK(strstr(msg, "Online") != NULL, "an event without onsite+location should say Online");
    free(msg);
    ctf_event_free(&ev);
}

static void test_digest_splits_on_length(void)
{
    /* Build enough events that the digest must split into more than one
     * Telegram message part (each part capped at 4096 characters). */
    size_t n = 80;
    ctf_event_t *events = calloc(n, sizeof(ctf_event_t));
    for (size_t i = 0; i < n; i++) {
        ctf_event_init(&events[i]);
        events[i].id = (unsigned int) i + 1;
        snprintf(events[i].title, sizeof(events[i].title),
                 "A Reasonably Long CTF Event Title Number %zu With Extra Padding Text", i);
        snprintf(events[i].start_time, sizeof(events[i].start_time), "2026-09-%02d 12:00:00",
                 (int) (i % 28) + 1);
        events[i].has_start_time = 1;
        snprintf(events[i].format, sizeof(events[i].format), "Jeopardy");
        events[i].has_format = 1;
    }

    size_t parts_count = 0;
    char **parts = formatter_digest(events, n, 14, &parts_count);
    CHECK(parts_count > 1, "80 padded events should not fit in a single 4096-char digest part");

    for (size_t i = 0; i < parts_count; i++) {
        size_t len = utf8_strlen(parts[i]); /* the splitting logic caps by codepoints, not bytes */
        CHECK(len <= 4096, "each digest part must respect the 4096-codepoint Telegram limit");
    }

    formatter_free_parts(parts, parts_count);
    for (size_t i = 0; i < n; i++) {
        ctf_event_free(&events[i]);
    }
    free(events);
}

static void test_digest_empty(void)
{
    size_t parts_count = 999;
    char **parts = formatter_digest(NULL, 0, 14, &parts_count);
    CHECK(parts == NULL, "digest of zero events should return NULL");
    CHECK(parts_count == 0, "digest of zero events should report 0 parts");
}

int main(void)
{
    test_event_all_fields_present();
    test_href_attribute_escaping();
    test_online_vs_onsite();
    test_digest_splits_on_length();
    test_digest_empty();
    TEST_SUMMARY();
}
