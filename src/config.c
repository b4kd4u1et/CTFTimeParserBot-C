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

static void set_field(app_config_t *cfg, const char *section, const char *key, const char *value)
{
    if (strcmp(section, "db") == 0) {
        if (strcmp(key, "host") == 0) snprintf(cfg->db.host, sizeof(cfg->db.host), "%s", value);
        else if (strcmp(key, "port") == 0) cfg->db.port = atoi(value);
        else if (strcmp(key, "name") == 0) snprintf(cfg->db.name, sizeof(cfg->db.name), "%s", value);
        else if (strcmp(key, "user") == 0) snprintf(cfg->db.user, sizeof(cfg->db.user), "%s", value);
        else if (strcmp(key, "pass") == 0) snprintf(cfg->db.pass, sizeof(cfg->db.pass), "%s", value);
        else if (strcmp(key, "charset") == 0) snprintf(cfg->db.charset, sizeof(cfg->db.charset), "%s", value);
    } else if (strcmp(section, "parser") == 0) {
        if (strcmp(key, "days_ahead") == 0) cfg->parser.days_ahead = atoi(value);
        else if (strcmp(key, "events_limit") == 0) cfg->parser.events_limit = atoi(value);
        else if (strcmp(key, "request_timeout") == 0) cfg->parser.request_timeout = atoi(value);
        else if (strcmp(key, "sleep_between_requests") == 0) cfg->parser.sleep_between_requests = atoi(value);
    } else if (strcmp(section, "telegram") == 0) {
        if (strcmp(key, "bot_token") == 0) snprintf(cfg->telegram.bot_token, sizeof(cfg->telegram.bot_token), "%s", value);
        else if (strcmp(key, "chat_id") == 0) snprintf(cfg->telegram.chat_id, sizeof(cfg->telegram.chat_id), "%s", value);
        else if (strcmp(key, "thread_id") == 0) cfg->telegram.thread_id = atol(value);
        else if (strcmp(key, "sleep_between_messages") == 0) cfg->telegram.sleep_between_messages = atoi(value);
    } else if (strcmp(section, "logging") == 0) {
        if (strcmp(key, "log_file") == 0) snprintf(cfg->log_file, sizeof(cfg->log_file), "%s", value);
        else if (strcmp(key, "publisher_log_file") == 0) snprintf(cfg->publisher_log_file, sizeof(cfg->publisher_log_file), "%s", value);
    }
    /* Unrecognized sections/keys are silently ignored, allowing forward-
     * compatible config files. */
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

    return 1;
}
