#ifndef CTF_UTIL_H
#define CTF_UTIL_H

#include <stddef.h>
#include <time.h>

/* ---- UTF-8 helpers ------------------------------------------------- */

/* Validates that `s` is well-formed UTF-8. Returns 1 if valid, 0 otherwise. */
int utf8_valid(const char *s);

/* Number of UTF-8 codepoints in `s` (not byte length). */
size_t utf8_strlen(const char *s);

/* Returns a newly malloc'd copy of `s` truncated to at most `max_chars`
 * UTF-8 codepoints. Never splits a multi-byte sequence. */
char *utf8_substr_alloc(const char *s, size_t max_chars);

/* ---- String helpers -------------------------------------------------
 * All functions below that "return" a string write into a caller-owned
 * buffer of a given C string. Helpers documented as "_alloc" return a
 * newly malloc'd string that the caller must free().
 */

/* Trim PHP-style whitespace (" \t\n\r\0\x0B") from both ends, in place. */
char *trim_inplace(char *s);

/* Removes anything that looks like an HTML/XML tag ("<...>"), a crude
 * equivalent of PHP's strip_tags(). Result is a newly malloc'd string. */
char *strip_tags_alloc(const char *s);

/* Collapses runs of 3 or more whitespace characters into exactly two
 * regular spaces (mirrors PHP preg_replace('/\s{3,}/', '  ', $v)).
 * Result is a newly malloc'd string. */
char *collapse_whitespace_alloc(const char *s);

/* Full sanitize pipeline used by ContentSecurity::sanitizeString():
 *   reject (return "") if invalid UTF-8,
 *   strip tags, collapse whitespace, trim, truncate to max_len codepoints.
 * Always returns a newly malloc'd, NUL-terminated string (never NULL). */
char *sanitize_string_alloc(const char *input, size_t max_len);

/* Escapes &, <, >, ", ' for Telegram HTML mode (ENT_QUOTES equivalent).
 * Result is a newly malloc'd string. */
char *html_escape_alloc(const char *s);

/* Escapes a string for embedding inside a JSON string literal (quotes,
 * backslashes, control characters). Result is a newly malloc'd string
 * WITHOUT the surrounding quotes. */
char *json_escape_alloc(const char *s);

/* ---- Network / SSRF helpers ------------------------------------------ */

/* Returns 1 if `host` is localhost, a cloud metadata endpoint, or (when
 * the host is a literal IPv4/IPv6 address) falls in a private/reserved
 * range. Hostnames that are not literal IP addresses are never resolved
 * (no blocking DNS) and always return 0, mirroring the PHP original. */
int is_internal_host(const char *host);

/* ---- Growable string buffer ------------------------------------------ */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

void strbuf_init(strbuf_t *b);
void strbuf_append(strbuf_t *b, const char *s);
void strbuf_append_fmt(strbuf_t *b, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Returns the buffer's owned string (caller must free()) and resets `b`
 * to an empty, still-usable state. Never returns NULL. */
char *strbuf_release(strbuf_t *b);

void strbuf_free(strbuf_t *b);

/* ---- Time helpers ------------------------------------------------- */

/* Parses an ISO-8601 / RFC-3339 timestamp (e.g. "2026-03-28T18:00:00+00:00"
 * or with trailing "Z") into a UTC unix timestamp. Returns 1 on success. */
int parse_iso8601(const char *s, time_t *out);

/* Parses a MySQL "YYYY-MM-DD HH:MM:SS" datetime string (always UTC) into
 * a unix timestamp. Returns 1 on success. */
int parse_mysql_datetime(const char *s, time_t *out);

/* Formats a unix timestamp as a MySQL "YYYY-MM-DD HH:MM:SS" string into
 * `out` (must be at least 20 bytes). */
void format_mysql_datetime(time_t t, char *out, size_t out_size);

#endif
