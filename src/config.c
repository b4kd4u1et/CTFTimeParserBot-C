#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    snprintf(cfg->db.host, sizeof(cfg->db.host), "%s", "localhost");
    cfg->db.port = 3306;
    snprintf(cfg->db.charset, sizeof(cfg->db.charset), "%s", "utf8mb4");

    cfg->parser.days_ahead = 14;
    cfg->parser.events_limit = 100;
    cfg->parser.request_timeout = 10;
    cfg->parser.sleep_between_requests = 1;

    cfg->telegram.sleep_between_messages = 2;

    snprintf(cfg->log_file, sizeof(cfg->log_file), "%s", "logs/parser.log");
    snprintf(cfg->publisher_log_file, sizeof(cfg->publisher_log_file), "%s", "logs/publisher.log");
}

static char *str_trim(char *s)
{
    while (*s && isspace((unsigned char) *s)) {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char) s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
    return s;
}

/* Copies `value` into a fixed-size field, warning (not failing) if it had
 * to be truncated -- a silently-truncated password/token is a confusing
 * "Access denied" bug waiting to happen (see the audit's C2 finding). */
static void set_str(char *dst, size_t dst_size, const char *field_name, const char *value)
{
    if (strlen(value) >= dst_size) {
        fprintf(stderr,
                "config: warning: value for '%s' is %zu bytes, truncated to %zu\n",
                field_name, strlen(value), dst_size - 1);
    }
    snprintf(dst, dst_size, "%s", value);
}

static void set_field(app_config_t *cfg, const char *section, const char *key, const char *value)
{
    if (strcmp(section, "db") == 0) {
        if (strcmp(key, "host") == 0) set_str(cfg->db.host, sizeof(cfg->db.host), "db.host", value);
        else if (strcmp(key, "port") == 0) cfg->db.port = atoi(value);
        else if (strcmp(key, "name") == 0) set_str(cfg->db.name, sizeof(cfg->db.name), "db.name", value);
        else if (strcmp(key, "user") == 0) set_str(cfg->db.user, sizeof(cfg->db.user), "db.user", value);
        else if (strcmp(key, "pass") == 0) set_str(cfg->db.pass, sizeof(cfg->db.pass), "db.pass", value);
        else if (strcmp(key, "charset") == 0) set_str(cfg->db.charset, sizeof(cfg->db.charset), "db.charset", value);
    } else if (strcmp(section, "parser") == 0) {
        if (strcmp(key, "days_ahead") == 0) cfg->parser.days_ahead = atoi(value);
        else if (strcmp(key, "events_limit") == 0) cfg->parser.events_limit = atoi(value);
        else if (strcmp(key, "request_timeout") == 0) cfg->parser.request_timeout = atoi(value);
        else if (strcmp(key, "sleep_between_requests") == 0) cfg->parser.sleep_between_requests = atoi(value);
    } else if (strcmp(section, "telegram") == 0) {
        if (strcmp(key, "bot_token") == 0) set_str(cfg->telegram.bot_token, sizeof(cfg->telegram.bot_token), "telegram.bot_token", value);
        else if (strcmp(key, "chat_id") == 0) set_str(cfg->telegram.chat_id, sizeof(cfg->telegram.chat_id), "telegram.chat_id", value);
        else if (strcmp(key, "thread_id") == 0) cfg->telegram.thread_id = atol(value);
        else if (strcmp(key, "sleep_between_messages") == 0) cfg->telegram.sleep_between_messages = atoi(value);
    } else if (strcmp(section, "logging") == 0) {
        if (strcmp(key, "log_file") == 0) set_str(cfg->log_file, sizeof(cfg->log_file), "logging.log_file", value);
        else if (strcmp(key, "publisher_log_file") == 0) set_str(cfg->publisher_log_file, sizeof(cfg->publisher_log_file), "logging.publisher_log_file", value);
    }
    /* Unrecognized sections/keys are silently ignored, allowing forward-
     * compatible config files. */
}

/* Clamps out-of-range numeric fields to their documented defaults instead
 * of silently accepting garbage/negative values that would otherwise reach
 * a dangerous sink downstream -- most notably a negative sleep_* value,
 * which becomes a multi-decade sleep() once cast to unsigned (see the
 * audit's C1/C3 findings), and request_timeout <= 0, which libcurl treats
 * as "no timeout at all" rather than "fail fast" (H2). Each clamp prints a
 * warning so a config typo is visible instead of silently "working". */
static void validate_and_clamp(app_config_t *cfg)
{
    if (cfg->db.port < 1 || cfg->db.port > 65535) {
        fprintf(stderr, "config: warning: db.port %d out of range, using default 3306\n", cfg->db.port);
        cfg->db.port = 3306;
    }
    if (cfg->parser.days_ahead <= 0) {
        fprintf(stderr, "config: warning: parser.days_ahead %d invalid, using default 14\n", cfg->parser.days_ahead);
        cfg->parser.days_ahead = 14;
    }
    if (cfg->parser.events_limit <= 0) {
        fprintf(stderr, "config: warning: parser.events_limit %d invalid, using default 100\n", cfg->parser.events_limit);
        cfg->parser.events_limit = 100;
    }
    if (cfg->parser.request_timeout <= 0) {
        fprintf(stderr, "config: warning: parser.request_timeout %d invalid, using default 10\n", cfg->parser.request_timeout);
        cfg->parser.request_timeout = 10;
    }
    if (cfg->parser.sleep_between_requests < 0) {
        fprintf(stderr, "config: warning: parser.sleep_between_requests %d negative, using default 1\n", cfg->parser.sleep_between_requests);
        cfg->parser.sleep_between_requests = 1;
    }
    if (cfg->telegram.sleep_between_messages < 0) {
        fprintf(stderr, "config: warning: telegram.sleep_between_messages %d negative, using default 2\n", cfg->telegram.sleep_between_messages);
        cfg->telegram.sleep_between_messages = 2;
    }
    if (cfg->telegram.thread_id < 0) {
        fprintf(stderr, "config: warning: telegram.thread_id %ld negative, using 0\n", cfg->telegram.thread_id);
        cfg->telegram.thread_id = 0;
    }
}

int config_load(const char *path, app_config_t *cfg)
{
    set_defaults(cfg);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "config: cannot open '%s': %s\n", path, strerror(errno));
        return 0;
    }

    char line[1024];
    char section[64] = "";
    int lineno = 0;
    int ok = 1;

    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = str_trim(line);

        if (*s == '\0' || *s == ';' || *s == '#') {
            continue;
        }

        size_t len = strlen(s);
        if (s[0] == '[' && s[len - 1] == ']') {
            s[len - 1] = '\0';
            char *name = str_trim(s + 1);
            snprintf(section, sizeof(section), "%s", name);
            continue;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "config: %s:%d: malformed line (expected 'key = value')\n", path, lineno);
            ok = 0;
            break;
        }
        *eq = '\0';
        char *key = str_trim(s);
        char *value = str_trim(eq + 1);

        set_field(cfg, section, key, value);
    }

    fclose(f);

    if (!ok) {
        return 0;
    }

    if (cfg->db.name[0] == '\0') {
        fprintf(stderr, "config: %s: missing required [db] name\n", path);
        return 0;
    }

    validate_and_clamp(cfg);

    return 1;
}
