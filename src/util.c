#include "util.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Growable string buffer ------------------------------------------ */

void strbuf_init(strbuf_t *b)
{
    b->cap = 64;
    b->data = malloc(b->cap);
    b->len = 0;
    if (b->data) {
        b->data[0] = '\0';
    } else {
        b->cap = 0; /* cap must never claim capacity a NULL buffer doesn't have */
    }
}

/* Ensures room for `extra` more bytes plus a NUL terminator. Returns 1 on
 * success, 0 on failure (allocation failure, or a request so large that the
 * required capacity would overflow size_t) -- callers must not write past
 * b->cap when this returns 0. */
static int strbuf_reserve(strbuf_t *b, size_t extra)
{
    if (extra > SIZE_MAX - b->len - 1) {
        return 0; /* b->len + extra + 1 would overflow size_t */
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) {
        return 1;
    }
    size_t newcap = b->cap ? b->cap : 64;
    while (newcap < need) {
        if (newcap > SIZE_MAX / 2) {
            newcap = need;
            break;
        }
        newcap *= 2;
    }
    char *newdata = realloc(b->data, newcap);
    if (!newdata) {
        return 0;
    }
    b->data = newdata;
    b->cap = newcap;
    return 1;
}

void strbuf_append(strbuf_t *b, const char *s)
{
    if (!s || !*s) {
        return;
    }
    size_t slen = strlen(s);
    if (!strbuf_reserve(b, slen)) {
        return; /* OOM/overflow: best-effort buffer, drop this append rather than corrupt memory */
    }
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

void strbuf_append_fmt(strbuf_t *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        va_end(ap2);
        return;
    }
    if (!strbuf_reserve(b, (size_t) needed)) {
        va_end(ap2);
        return;
    }
    vsnprintf(b->data + b->len, (size_t) needed + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t) needed;
}

char *strbuf_release(strbuf_t *b)
{
    char *out = b->data ? b->data : strdup("");
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    strbuf_init(b);
    return out;
}

void strbuf_free(strbuf_t *b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* ---- UTF-8 helpers ------------------------------------------------- */

int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *) s;

    while (*p) {
        unsigned char c = *p;
        int len;
        unsigned int cp, min_cp;

        if (c < 0x80) {
            p++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            len = 1; cp = c & 0x1Fu; min_cp = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            len = 2; cp = c & 0x0Fu; min_cp = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            len = 3; cp = c & 0x07u; min_cp = 0x10000;
        } else {
            return 0;
        }

        p++;
        for (int i = 0; i < len; i++) {
            if ((*p & 0xC0) != 0x80) {
                return 0;
            }
            cp = (cp << 6) | (unsigned int) (*p & 0x3F);
            p++;
        }

        if (cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return 0;
        }
    }

    return 1;
}

size_t utf8_strlen(const char *s)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *) s;

    while (*p) {
        if ((*p & 0xC0) != 0x80) {
            n++;
        }
        p++;
    }

    return n;
}

char *utf8_substr_alloc(const char *s, size_t max_chars)
{
    size_t count = 0;
    const unsigned char *start = (const unsigned char *) s;
    const unsigned char *end = start;

    while (*end) {
        if ((*end & 0xC0) != 0x80) {
            if (count == max_chars) {
                break;
            }
            count++;
        }
        end++;
    }

    size_t byte_len = (size_t) (end - start);
    char *out = malloc(byte_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, s, byte_len);
    out[byte_len] = '\0';
    return out;
}

/* ---- String helpers -------------------------------------------------- */

static int is_php_trim_char(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0' || c == 0x0B;
}

char *trim_inplace(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && is_php_trim_char((unsigned char) s[start])) {
        start++;
    }
    size_t end = len;
    while (end > start && is_php_trim_char((unsigned char) s[end - 1])) {
        end--;
    }
    size_t newlen = end - start;
    if (start > 0) {
        memmove(s, s + start, newlen);
    }
    s[newlen] = '\0';
    return s;
}

char *strip_tags_alloc(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t oi = 0, i = 0;

    while (i < len) {
        if (s[i] == '<') {
            size_t j = i + 1;
            while (j < len && s[j] != '>') {
                j++;
            }
            if (j < len) {
                i = j + 1; /* skip the whole tag */
                continue;
            }
            break; /* unterminated tag: PHP strip_tags drops the rest of the string */
        }
        out[oi++] = s[i++];
    }

    out[oi] = '\0';
    return out;
}

char *collapse_whitespace_alloc(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t oi = 0, i = 0;

    while (i < len) {
        if (isspace((unsigned char) s[i])) {
            size_t j = i;
            while (j < len && isspace((unsigned char) s[j])) {
                j++;
            }
            size_t run = j - i;
            if (run >= 3) {
                out[oi++] = ' ';
                out[oi++] = ' ';
            } else {
                for (size_t k = i; k < j; k++) {
                    out[oi++] = s[k];
                }
            }
            i = j;
        } else {
            out[oi++] = s[i++];
        }
    }

    out[oi] = '\0';
    return out;
}

char *sanitize_string_alloc(const char *input, size_t max_len)
{
    if (!input) {
        input = "";
    }

    if (!utf8_valid(input)) {
        char *empty = malloc(1);
        if (empty) {
            empty[0] = '\0';
        }
        return empty;
    }

    char *stripped = strip_tags_alloc(input);
    if (!stripped) {
        return NULL;
    }
    char *collapsed = collapse_whitespace_alloc(stripped);
    free(stripped);
    if (!collapsed) {
        return NULL;
    }
    trim_inplace(collapsed);
    char *truncated = utf8_substr_alloc(collapsed, max_len);
    free(collapsed);
    return truncated;
}

char *html_escape_alloc(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len * 6 + 1);
    if (!out) {
        return NULL;
    }
    size_t oi = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) s[i];
        const char *rep = NULL;
        switch (c) {
            case '&':  rep = "&amp;";  break;
            case '<':  rep = "&lt;";   break;
            case '>':  rep = "&gt;";   break;
            case '"':  rep = "&quot;"; break;
            case '\'': rep = "&#039;"; break;
            default: break;
        }
        if (rep) {
            size_t rl = strlen(rep);
            memcpy(out + oi, rep, rl);
            oi += rl;
        } else {
            out[oi++] = (char) c;
        }
    }

    out[oi] = '\0';
    return out;
}

char *json_escape_alloc(const char *s)
{
    size_t len = strlen(s);
    char *out = malloc(len * 6 + 1);
    if (!out) {
        return NULL;
    }
    size_t oi = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) s[i];
        switch (c) {
            case '"':  out[oi++] = '\\'; out[oi++] = '"';  break;
            case '\\': out[oi++] = '\\'; out[oi++] = '\\'; break;
            case '\n': out[oi++] = '\\'; out[oi++] = 'n';  break;
            case '\r': out[oi++] = '\\'; out[oi++] = 'r';  break;
            case '\t': out[oi++] = '\\'; out[oi++] = 't';  break;
            case '\b': out[oi++] = '\\'; out[oi++] = 'b';  break;
            case '\f': out[oi++] = '\\'; out[oi++] = 'f';  break;
            default:
                if (c < 0x20) {
                    oi += (size_t) snprintf(out + oi, 7, "\\u%04x", c);
                } else {
                    out[oi++] = (char) c;
                }
        }
    }

    out[oi] = '\0';
    return out;
}

/* ---- Network / SSRF helpers ------------------------------------------ */

/* Detects hosts that "look like" an IP address encoded to dodge a literal
 * dotted-quad/colon-hex check -- a decimal integer ("2130706433"), a 0x-
 * prefixed hex literal ("0x7f000001"), an octal-looking or short-form
 * dotted string ("017700000001", "127.1"). inet_pton() is intentionally
 * strict and accepts none of these, but some HTTP clients/resolvers still
 * normalize them to the literal address they encode, so treating "host is
 * nothing but digits and dots, or 0x+hex" as suspicious closes that gap. A
 * real hostname always contains at least one letter, so this has no false
 * positives against legitimate DNS names. */
static int host_looks_like_ip_obfuscation(const char *host)
{
    size_t len = strlen(host);
    if (len == 0) {
        return 0;
    }

    if (len > 2 && host[0] == '0' && (host[1] == 'x' || host[1] == 'X')) {
        for (size_t i = 2; i < len; i++) {
            if (!isxdigit((unsigned char) host[i])) {
                return 0;
            }
        }
        return 1;
    }

    int has_digit = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) host[i];
        if (c >= '0' && c <= '9') {
            has_digit = 1;
            continue;
        }
        if (c == '.') {
            continue;
        }
        return 0; /* contains a letter or other char: an ordinary hostname */
    }
    return has_digit;
}

int is_internal_host(const char *host)
{
    if (!host || !*host) {
        return 0;
    }

    char buf[256];
    size_t n = strlen(host);
    if (n >= sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    for (size_t i = 0; i < n; i++) {
        buf[i] = (char) tolower((unsigned char) host[i]);
    }
    buf[n] = '\0';

    /* Strip surrounding [...] from a bracketed IPv6 literal. */
    size_t blen = strlen(buf);
    if (blen >= 2 && buf[0] == '[' && buf[blen - 1] == ']') {
        memmove(buf, buf + 1, blen - 2);
        buf[blen - 2] = '\0';
    }

    if (strcmp(buf, "localhost") == 0) {
        return 1;
    }
    if (strcmp(buf, "169.254.169.254") == 0 || strcmp(buf, "metadata.google.internal") == 0) {
        return 1;
    }

    struct in_addr a4;
    if (inet_pton(AF_INET, buf, &a4) == 1) {
        uint32_t ip = ntohl(a4.s_addr);

        if ((ip & 0xFF000000u) == 0x00000000u) return 1; /* 0.0.0.0/8        */
        if ((ip & 0xFF000000u) == 0x0A000000u) return 1; /* 10.0.0.0/8       */
        if ((ip & 0xFFC00000u) == 0x64400000u) return 1; /* 100.64.0.0/10    */
        if ((ip & 0xFF000000u) == 0x7F000000u) return 1; /* 127.0.0.0/8      */
        if ((ip & 0xFFFF0000u) == 0xA9FE0000u) return 1; /* 169.254.0.0/16   */
        if ((ip & 0xFFF00000u) == 0xAC100000u) return 1; /* 172.16.0.0/12    */
        if ((ip & 0xFFFFFF00u) == 0xC0000000u) return 1; /* 192.0.0.0/24     */
        if ((ip & 0xFFFFFF00u) == 0xC0000200u) return 1; /* 192.0.2.0/24     */
        if ((ip & 0xFFFFFF00u) == 0xC0586300u) return 1; /* 192.88.99.0/24   */
        if ((ip & 0xFFFF0000u) == 0xC0A80000u) return 1; /* 192.168.0.0/16   */
        if ((ip & 0xFFFE0000u) == 0xC6120000u) return 1; /* 198.18.0.0/15    */
        if ((ip & 0xFFFFFF00u) == 0xC6336400u) return 1; /* 198.51.100.0/24  */
        if ((ip & 0xFFFFFF00u) == 0xCB007100u) return 1; /* 203.0.113.0/24   */
        if ((ip & 0xF0000000u) == 0xE0000000u) return 1; /* 224.0.0.0/4      */
        if ((ip & 0xF0000000u) == 0xF0000000u) return 1; /* 240.0.0.0/4 + bcast */
        return 0;
    }

    struct in6_addr a6;
    if (inet_pton(AF_INET6, buf, &a6) == 1) {
        if (IN6_IS_ADDR_LOOPBACK(&a6) || IN6_IS_ADDR_UNSPECIFIED(&a6) ||
            IN6_IS_ADDR_LINKLOCAL(&a6) || IN6_IS_ADDR_MULTICAST(&a6)) {
            return 1;
        }
        if ((a6.s6_addr[0] & 0xFEu) == 0xFCu) { /* fc00::/7 unique-local */
            return 1;
        }
        if (IN6_IS_ADDR_V4MAPPED(&a6) || IN6_IS_ADDR_V4COMPAT(&a6)) {
            char v4[INET_ADDRSTRLEN];
            struct in_addr ia;
            memcpy(&ia.s_addr, &a6.s6_addr[12], 4);
            inet_ntop(AF_INET, &ia, v4, sizeof(v4));
            return is_internal_host(v4);
        }
        return 0;
    }

    /* Not a literal IP address recognized by inet_pton(). Reject anything
     * that merely *looks like* an obfuscated IP literal (decimal/hex/octal/
     * short-form) rather than treating it as an ordinary hostname -- see
     * host_looks_like_ip_obfuscation(). Otherwise, treat it as a real
     * hostname: no DNS resolution is performed (a blocking lookup with no
     * timeout could stall the cron process), matching the original's
     * threat model. */
    return host_looks_like_ip_obfuscation(buf);
}

/* ---- Time helpers ------------------------------------------------- */

int parse_iso8601(const char *s, time_t *out)
{
    if (!s || strlen(s) < 19) {
        return 0;
    }

    int y, mo, d, h, mi, se;
    int matched = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se);
    if (matched != 6) {
        matched = sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se);
        if (matched != 6) {
            return 0;
        }
    }

    /* Reject syntactically-parseable but out-of-range fields (month 13, day
     * 45, ...) up front -- timegm() would otherwise silently *normalize*
     * them into a different, "valid-looking" timestamp instead of failing,
     * which could smuggle a garbage date past the caller's sanity check. 60
     * is allowed for seconds to tolerate a leap second. */
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 ||
        mi < 0 || mi > 59 || se < 0 || se > 60) {
        return 0;
    }

    int tz_offset_sec = 0;
    const char *scan = s + 10; /* skip past "YYYY-MM-DD" */

    if (strchr(scan, 'Z') == NULL) {
        const char *plus = strrchr(scan, '+');
        const char *minus = strrchr(scan, '-');
        const char *tzptr = NULL;
        int sign = 1;

        if (plus && (!minus || plus > minus)) {
            tzptr = plus;
            sign = 1;
        } else if (minus) {
            tzptr = minus;
            sign = -1;
        }

        if (tzptr) {
            int th = 0, tm = 0;
            if (sscanf(tzptr + 1, "%2d:%2d", &th, &tm) == 2 ||
                sscanf(tzptr + 1, "%2d%2d", &th, &tm) == 2) {
                tz_offset_sec = sign * (th * 3600 + tm * 60);
            }
        }
    }

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = se;

    time_t t = timegm(&tmv);
    if (t == (time_t) -1) {
        return 0;
    }
    t -= tz_offset_sec; /* UTC = local wall-clock time minus its offset */

    if (out) {
        *out = t;
    }
    return 1;
}

int parse_mysql_datetime(const char *s, time_t *out)
{
    if (!s) {
        return 0;
    }

    int y, mo, d, h, mi, se;
    if (sscanf(s, "%4d-%2d-%2d %2d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) != 6) {
        return 0;
    }

    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900;
    tmv.tm_mon = mo - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = h;
    tmv.tm_min = mi;
    tmv.tm_sec = se;

    time_t t = timegm(&tmv);
    if (t == (time_t) -1) {
        return 0;
    }
    if (out) {
        *out = t;
    }
    return 1;
}

void format_mysql_datetime(time_t t, char *out, size_t out_size)
{
    struct tm tmv;
    gmtime_r(&t, &tmv);
    strftime(out, out_size, "%Y-%m-%d %H:%M:%S", &tmv);
}
