/* parser: CTFTime -> MySQL pipeline. Run via cron every few hours.
 *
 * Usage: parser [config.ini]   (defaults to "config.ini" in the CWD)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "content_security.h"
#include "ctftime_client.h"
#include "db.h"
#include "http.h"
#include "json.h"
#include "lock.h"
#include "log.h"

static const char *LOCK_PATH = "/tmp/ctftimeparser.lock";

int main(int argc, char **argv)
{
    const char *config_path = (argc > 1) ? argv[1] : "config.ini";

    app_config_t config;
    if (!config_load(config_path, &config)) {
        fprintf(stderr, "parser: failed to load config from '%s'\n", config_path);
        return 1;
    }

    const char *log_file = config.log_file;

    /* Atomic lock -- see lock.c: no TOCTOU race, and the OS releases it
     * automatically if this process dies, so no stale-lock cleanup needed. */
    lockfile_t lock;
    int lr = lock_acquire(&lock, LOCK_PATH);
    if (lr == 0) {
        log_msg(log_file, "warn", "Another instance is already running. Exiting.");
        return 0;
    }
    if (lr < 0) {
        log_msg(log_file, "error", "Could not create lock file '%s'.", LOCK_PATH);
        return 1;
    }

    int exit_code = 0;
    http_global_init();

    db_t *db = db_connect(&config.db);
    if (!db) {
        log_msg(log_file, "error", "Database error: connection failed.");
        exit_code = 1;
    } else {
        const parser_config_t *cfg = &config.parser;

        /* ---- Step 1: collect event IDs into parser_buffer ------------- */

        time_t now = time(NULL);
        time_t finish = now + (time_t) cfg->days_ahead * 86400;

        char now_str[16], finish_str[16];
        struct tm tmv;
        gmtime_r(&now, &tmv);
        strftime(now_str, sizeof(now_str), "%Y-%m-%d", &tmv);
        gmtime_r(&finish, &tmv);
        strftime(finish_str, sizeof(finish_str), "%Y-%m-%d", &tmv);

        log_msg(log_file, "info", "Fetching event list [%s - %s]", now_str, finish_str);

        id_list_t ids;
        int fetched = ctftime_fetch_event_ids(cfg->request_timeout, now, finish, cfg->events_limit, &ids);

        if (!fetched || ids.count == 0) {
            log_msg(log_file, "info", "No events returned from CTFTime API.");
        } else {
            log_msg(log_file, "info", "Received %zu event IDs from API.", ids.count);
            if (!db_insert_buffer(db, ids.ids, ids.count)) {
                log_msg(log_file, "error", "Database error: %s", db_error(db));
                exit_code = 1;
            }
        }
        id_list_free(&ids);

        /* ---- Step 2: drop IDs already present in ctf_events ------------ */

        if (exit_code == 0) {
            if (!db_clean_buffer(db)) {
                log_msg(log_file, "error", "Database error: %s", db_error(db));
                exit_code = 1;
            } else {
                log_msg(log_file, "info", "Buffer cleaned (removed already-known events).");
            }
        }

        /* ---- Step 3: fetch details for remaining IDs and store them ---- */

        if (exit_code == 0) {
            unsigned int *pending = NULL;
            size_t total = 0;

            if (!db_get_buffer_ids(db, &pending, &total)) {
                log_msg(log_file, "error", "Database error: %s", db_error(db));
                exit_code = 1;
            } else {
                log_msg(log_file, "info", "%zu new event(s) to process.", total);

                size_t saved = 0, skipped = 0, unsafe = 0;

                for (size_t i = 0; i < total; i++) {
                    unsigned int event_id = pending[i];
                    log_msg(log_file, "info", "Fetching details for event #%u ...", event_id);

                    json_value_t *raw = ctftime_fetch_event_detail(cfg->request_timeout, event_id);
                    if (!raw) {
                        log_msg(log_file, "warn",
                                "Event #%u: failed to fetch details. Removing from buffer.", event_id);
                        db_delete_from_buffer(db, event_id);
                        skipped++;
                        continue;
                    }

                    /* Fallback CTFTime URL, used only when the API response omits one. */
                    char fallback[64];
                    snprintf(fallback, sizeof(fallback), "https://ctftime.org/event/%u", event_id);

                    ctf_event_t ev;
                    int ok = content_security_sanitize(raw, event_id, fallback, &ev);
                    json_free(raw);

                    if (!ok) {
                        log_msg(log_file, "warn",
                                "Event #%u: failed sanitization (invalid data). Skipping.", event_id);
                        db_delete_from_buffer(db, event_id);
                        skipped++;
                        ctf_event_free(&ev);
                        continue;
                    }

                    if (!ev.is_safe) {
                        log_msg(log_file, "warn",
                                "Event #%u: flagged as unsafe. Stored with is_safe=0.", event_id);
                        unsafe++;
                    }

                    db_insert_event(db, &ev);
                    db_delete_from_buffer(db, event_id);
                    saved++;

                    log_msg(log_file, "info", "Event #%u saved: \"%s\" (safe=%d)",
                            event_id, ev.title, ev.is_safe);

                    ctf_event_free(&ev);

                    /* Be polite to CTFTime: pause between requests, except after the last one. */
                    if (cfg->sleep_between_requests > 0 && i < total - 1) {
                        sleep((unsigned int) cfg->sleep_between_requests);
                    }
                }

                free(pending);

                log_msg(log_file, "info", "Done. Saved: %zu | Unsafe (stored): %zu | Skipped: %zu",
                        saved, unsafe, skipped);
            }
        }

        db_close(db);
    }

    http_global_cleanup();
    lock_release(&lock, LOCK_PATH);
    return exit_code;
}
