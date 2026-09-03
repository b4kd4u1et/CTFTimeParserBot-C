#ifndef CTF_CONFIG_H
#define CTF_CONFIG_H

typedef struct {
    char host[256];
    int port;
    char name[128];
    char user[128];
    char pass[128];
    char charset[32];
} db_config_t;

typedef struct {
    int days_ahead;
    int events_limit;
    int request_timeout;
    int sleep_between_requests;
} parser_config_t;

typedef struct {
    char bot_token[256];
    char chat_id[64];
    long thread_id;
    int sleep_between_messages;
} telegram_config_t;

typedef struct {
    db_config_t db;
    parser_config_t parser;
    telegram_config_t telegram;
    char log_file[512];
    char publisher_log_file[512];
} app_config_t;

/* Loads and parses an INI-style config file (see config.ini.sample) into
 * `cfg`. Returns 1 on success, 0 on failure (file missing/unreadable or a
 * malformed line); on failure an explanatory message has been written to
 * stderr. Unrecognized keys are ignored; missing keys keep their sane
 * defaults (applied before parsing). */
int config_load(const char *path, app_config_t *cfg);

#endif
