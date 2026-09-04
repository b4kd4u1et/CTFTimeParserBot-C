// deps: log.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "test_helpers.h"

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t) len + 1);
    size_t n = fread(buf, 1, (size_t) len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Regression test for the audit's L1 finding: a title containing raw
 * newlines and an ANSI escape byte must never reach the log file
 * unescaped -- that would let external data forge fake log lines or
 * inject terminal control sequences (CWE-117). */
static void test_control_characters_are_escaped(void)
{
    const char *path = "/tmp/ctf_test_log_injection.log";
    unlink(path);

    const char *evil =
        "Legit CTF\n[2026-01-01 00:00:00] [ERROR] FAKE injected line\x1b[31mRED\x1b[0m\ttab";
    log_msg(path, "info", "Event #%u saved: \"%s\"", 42u, evil);

    char *content = read_file(path);
    CHECK(content != NULL, "log file should have been written");
    if (!content) {
        return;
    }

    size_t newline_count = 0;
    int has_esc = 0;
    for (size_t i = 0; content[i]; i++) {
        if (content[i] == '\n') newline_count++;
        if (content[i] == '\x1b') has_esc = 1;
    }
    CHECK(newline_count == 1, "the whole log entry must stay on exactly one line (only the trailing newline)");
    CHECK(!has_esc, "a raw ESC byte must never reach the log file");
    CHECK(strstr(content, "\\x0a") != NULL, "the embedded newline should appear as a visible \\x0a escape");
    CHECK(strstr(content, "\\x1b") != NULL, "the embedded ESC should appear as a visible \\x1b escape");
    CHECK(strstr(content, "\\x09") != NULL, "the embedded tab should appear as a visible \\x09 escape");

    free(content);
    unlink(path);
}

static void test_normal_message_and_rotation_smoke(void)
{
    const char *path = "/tmp/ctf_test_log_normal.log";
    unlink(path);

    log_msg(path, "info", "Hello %s, count=%d", "world", 42);
    char *content = read_file(path);
    CHECK(content != NULL, "log file should exist after a normal write");
    if (content) {
        CHECK(strstr(content, "[INFO] Hello world, count=42") != NULL,
              "level should be upper-cased and the message formatted correctly");
        free(content);
    }
    unlink(path);
}

int main(void)
{
    test_control_characters_are_escaped();
    test_normal_message_and_rotation_smoke();
    TEST_SUMMARY();
}
