#include "content_security.h"

#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Sane timestamp range: 2000-01-01 to 2100-01-01 (UTC), matching the
 * original's sanity guard against garbage/overflowed API dates. */
#define TS_MIN 946684800
#define TS_MAX 4102444800

/* ---- SSTI / SQLi pattern detection (supplementary; PDO/MySQL prepared
 * statements remain the primary SQLi defence in db.c) ------------------- */

static regex_t g_ssti_re[5];
static regex_t g_sqli_re[3];
static int g_patterns_ready = 0;

static void compile_patterns(void)
{
    if (g_patterns_ready) {
        return;
    }

    static const char *ssti_patterns[5] = {
        "\\{\\{.*\\}\\}",  /* Jinja2 / Twig            */
        "\\{%.*%\\}",      /* Jinja2 / Twig tags       */
        "<%.*%>",          /* ERB / ASP                */
        "\\$\\{.*\\}",     /* Freemarker / Thymeleaf   */
        "#\\{.*\\}",       /* Groovy / Ruby            */
    };
    for (int i = 0; i < 5; i++) {
        regcomp(&g_ssti_re[i], ssti_patterns[i], REG_EXTENDED);
    }

    regcomp(&g_sqli_re[0],
            "(\\bunion\\b.*\\bselect\\b|\\bselect\\b.*\\bfrom\\b|\\bdrop\\b.*\\btable\\b)",
            REG_EXTENDED | REG_ICASE);
    regcomp(&g_sqli_re[1], "--[[:space:]]*$", REG_EXTENDED);
    regcomp(&g_sqli_re[2],
            ";[[:space:]]*(drop|insert|update|delete|truncate)[[:space:]]",
            REG_EXTENDED | REG_ICASE);

    g_patterns_ready = 1;
}

static int is_safe_string(const char *value)
{
    compile_patterns();

    for (int i = 0; i < 5; i++) {
        if (regexec(&g_ssti_re[i], value, 0, NULL, 0) == 0) {
            return 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        if (regexec(&g_sqli_re[i], value, 0, NULL, 0) == 0) {
            return 0;
        }
    }
    return 1;
}

/* ---- URL validation ---------------------------------------------------- */

static int url_scheme_and_host(const char *value, char *scheme_out, size_t scheme_cap,
                                char *host_out, size_t host_cap, int *has_userinfo)
{
    const char *sep = strstr(value, "://");
    if (!sep) {
        return 0;
    }

    size_t scheme_len = (size_t) (sep - value);
    if (scheme_len == 0 || scheme_len >= scheme_cap) {
        return 0;
    }
    for (size_t i = 0; i < scheme_len; i++) {
        unsigned char c = (unsigned char) value[i];
        if (!isalpha(c)) {
            return 0;
        }
        scheme_out[i] = (char) tolower(c);
    }
    scheme_out[scheme_len] = '\0';

    const char *rest = sep + 3;
    size_t rest_len = strlen(rest);
    size_t auth_end = rest_len;
    for (size_t i = 0; i < rest_len; i++) {
        char c = rest[i];
        if (c == '/' || c == '?' || c == '#') {
            auth_end = i;
            break;
        }
    }
    if (auth_end == 0) {
        return 0; /* empty authority, e.g. "https:///path" */
    }

    char authority[600];
    if (auth_end >= sizeof(authority)) {
        return 0;
    }
    memcpy(authority, rest, auth_end);
    authority[auth_end] = '\0';

    *has_userinfo = (strchr(authority, '@') != NULL);
    const char *hostport = authority;
    const char *at = strrchr(authority, '@');
    if (at) {
        hostport = at + 1;
    }

    const char *host_start = hostport;
    const char *host_end;
    if (*hostport == '[') {
        const char *close = strchr(hostport, ']');
        if (!close) {
            return 0;
        }
        host_start = hostport + 1;
        host_end = close;
    } else {
        const char *colon = strchr(hostport, ':');
        host_end = colon ? colon : hostport + strlen(hostport);
    }

    size_t host_len = (size_t) (host_end - host_start);
    if (host_len == 0 || host_len >= host_cap) {
        return 0;
    }
    for (size_t i = 0; i < host_len; i++) {
        host_out[i] = (char) tolower((unsigned char) host_start[i]);
    }
    host_out[host_len] = '\0';

    return 1;
}

/* Validates `value` as a safe http(s) URL. When `allow_any_domain` is 0 the
 * host must be ctftime.org (or a subdomain); when 1, any host is allowed
 * except localhost/private/reserved ranges (SSRF guard). On success copies
 * `value` verbatim into `out` and returns 1; returns 0 otherwise. */
static int sanitize_url(const char *value, int allow_any_domain, char *out, size_t out_size)
{
    if (!value || !*value) {
        return 0;
    }

    size_t len = strlen(value);
    if (len > URL_MAX) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char) value[i])) {
            return 0; /* null-byte and whitespace tricks */
        }
    }

    char scheme[16];
    char host[256];
    int has_userinfo = 0;
    if (!url_scheme_and_host(value, scheme, sizeof(scheme), host, sizeof(host), &has_userinfo)) {
        return 0;
    }
    if (strcmp(scheme, "http") != 0 && strcmp(scheme, "https") != 0) {
        return 0; /* blocks javascript:, data:, ftp:, etc. */
    }
    if (has_userinfo) {
        return 0; /* credentials in URL: SSRF / info-leak guard */
    }

    if (!allow_any_domain) {
        static const char suffix[] = ".ctftime.org";
        size_t hl = strlen(host);
        size_t sl = strlen(suffix);
        int ok = (strcmp(host, "ctftime.org") == 0) ||
                 (hl > sl && strcmp(host + hl - sl, suffix) == 0);
        if (!ok) {
            return 0;
        }
    } else if (is_internal_host(host)) {
        return 0;
    }

    if (len >= out_size) {
        return 0;
    }
    memcpy(out, value, len + 1);
    return 1;
}

/* ---- Main entry point --------------------------------------------------- */

int content_security_sanitize(const json_value_t *raw, unsigned int forced_id,
                               const char *fallback_ctftime_url, ctf_event_t *out)
{
    ctf_event_init(out);

    if (forced_id == 0) {
        return 0;
    }

    const char *raw_title = json_get_string(raw, "title", "");
    char *title = sanitize_string_alloc(raw_title, TITLE_MAX);
    if (!title || title[0] == '\0') {
        free(title);
        return 0;
    }

    int is_safe = 1;

    const char *raw_desc = json_get_string(raw, "description", "");
    const char *raw_loc  = json_get_string(raw, "location", "");
    if (!is_safe_string(raw_title)) is_safe = 0;
    if (!is_safe_string(raw_desc))  is_safe = 0;
    if (!is_safe_string(raw_loc))   is_safe = 0;

    char *description = sanitize_string_alloc(raw_desc, DESC_MAX);
    char *format = sanitize_string_alloc(json_get_string(raw, "format", ""), FORMAT_MAX);
    char *location = sanitize_string_alloc(raw_loc, LOCATION_MAX);

    const char *raw_url  = json_get_string(raw, "url", "");
    const char *raw_logo = json_get_string(raw, "logo", "");
    const char *raw_ctftime_url = json_get_string(raw, "ctftime_url", "");
    if (raw_ctftime_url[0] == '\0') {
        raw_ctftime_url = fallback_ctftime_url ? fallback_ctftime_url : "";
    }

    char url[URL_MAX + 1];
    int has_url = sanitize_url(raw_url, 1, url, sizeof(url));
    char ctftime_url[URL_MAX + 1];
    int has_ctftime_url = sanitize_url(raw_ctftime_url, 0, ctftime_url, sizeof(ctftime_url));
    char logo_url[URL_MAX + 1];
    int has_logo_url = sanitize_url(raw_logo, 1, logo_url, sizeof(logo_url));

    if (raw_url[0] != '\0' && !has_url) {
        is_safe = 0; /* URL present in the API response but failed validation */
    }

    time_t start_ts = 0, finish_ts = 0;
    int has_start = 0, has_finish = 0;

    const char *raw_start = json_get_string(raw, "start", "");
    if (raw_start[0] != '\0' && parse_iso8601(raw_start, &start_ts) &&
        start_ts >= TS_MIN && start_ts <= TS_MAX) {
        has_start = 1;
    }

    const char *raw_finish = json_get_string(raw, "finish", "");
    if (raw_finish[0] != '\0' && parse_iso8601(raw_finish, &finish_ts) &&
        finish_ts >= TS_MIN && finish_ts <= TS_MAX) {
        has_finish = 1;
    }

    double weight = 0.0;
    int has_weight = json_get_number(raw, "weight", &weight);
    if (has_weight) {
        weight = round(weight * 100000.0) / 100000.0;
    }

    int onsite = json_get_bool(raw, "onsite", 0);

    out->id = forced_id;
    snprintf(out->title, sizeof(out->title), "%s", title);

    if (has_url) {
        snprintf(out->url, sizeof(out->url), "%s", url);
        out->has_url = 1;
    }
    if (has_ctftime_url) {
        snprintf(out->ctftime_url, sizeof(out->ctftime_url), "%s", ctftime_url);
        out->has_ctftime_url = 1;
    }
    if (has_start) {
        format_mysql_datetime(start_ts, out->start_time, sizeof(out->start_time));
        out->has_start_time = 1;
    }
    if (has_finish) {
        format_mysql_datetime(finish_ts, out->finish_time, sizeof(out->finish_time));
        out->has_finish_time = 1;
    }
    if (format && format[0] != '\0') {
        snprintf(out->format, sizeof(out->format), "%s", format);
        out->has_format = 1;
    }
    if (has_weight) {
        out->weight = weight;
        out->has_weight = 1;
    }
    out->onsite = onsite;
    if (location && location[0] != '\0') {
        snprintf(out->location, sizeof(out->location), "%s", location);
        out->has_location = 1;
    }
    if (description && description[0] != '\0') {
        out->description = strdup(description);
    }
    if (has_logo_url) {
        snprintf(out->logo_url, sizeof(out->logo_url), "%s", logo_url);
        out->has_logo_url = 1;
    }
    out->is_safe = is_safe;

    free(title);
    free(description);
    free(format);
    free(location);

    return 1;
}
