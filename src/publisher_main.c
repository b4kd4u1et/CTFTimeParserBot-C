/* publisher: MySQL -> Telegram publisher. Run via cron daily at 07:00.
 *
 * Usage: publisher [config.ini]   (defaults to "config.ini" in the CWD)
 *
 * Monday publishes one compact weekly digest of the next 14 days;
 * Tuesday-Sunday publish one full message per unpublished event.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "db.h"
#include "formatter.h"
#include "http.h"
#include "lock.h"
#include "log.h"
#include "telegram_bot.h"

static const char *LOCK_PATH = "/tmp/ctftimepublisher.lock";

int main(int argc, char **argv)
{
    const char *config_path = (argc > 1) ? argv[1] : "config.ini";

    app_config_t config;
    if (!config_load(config_path, &config)) {
        fprintf(stderr, "publisher: failed to load config from '%s'\n", config_path);
        return 1;
    }

    const char *log_file = config.publisher_log_file;

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
        const telegram_config_t *tcfg = &config.telegram;

        time_t now = time(NULL);
        struct tm tmv;
        localtime_r(&now, &tmv);
        int iso_dow = (tmv.tm_wday == 0) ? 7 : tmv.tm_wday; /* 1 = Monday ... 7 = Sunday */
        int is_monday = (iso_dow == 1);

        if (is_monday) {
            /* ---- Monday: weekly digest, next 14 days ------------------
             * posted_at is NOT touched, so these events still appear in
             * the daily updates on later days. */

            ctf_event_t *events = NULL;
            size_t count = 0;

            if (!db_get_upcoming_events(db, 14, &events, &count)) {
                log_msg(log_file, "error", "Database error: %s", db_error(db));
                exit_code = 1;
            } else if (count == 0) {
                log_msg(log_file, "info", "Weekly digest: no upcoming events in the next 14 days.");
            } else {
                size_t parts_count = 0;
                char **parts = formatter_digest(events, count, 14, &parts_count);

                for (size_t i = 0; i < parts_count; i++) {
                    if (!telegram_send_message(tcfg, parts[i])) {
                        log_msg(log_file, "warn", "Weekly digest: failed to send part %zu/%zu.",
                                i + 1, parts_count);
                    }
                    if (i < parts_count - 1) {
                        sleep(1); /* brief pause between digest parts when split */
                    }
                }

                log_msg(log_file, "info", "Weekly digest sent: %zu event(s) in %zu message(s).",
                        count, parts_count);

                formatter_free_parts(parts, parts_count);
            }

            ctf_event_array_free(events, count);
        } else {
            /* ---- Tuesday-Sunday: daily updates -------------------------
             * One full message per unpublished event; posted_at is set on
             * success. Failures are left unpublished and retried next run. */

            ctf_event_t *events = NULL;
            size_t total = 0;

            if (!db_get_unpublished_events(db, &events, &total)) {
                log_msg(log_file, "error", "Database error: %s", db_error(db));
                exit_code = 1;
            } else if (total == 0) {
                log_msg(log_file, "info", "Daily update: no unpublished events.");
            } else {
                log_msg(log_file, "info", "Daily update: %zu event(s) to publish.", total);

                size_t sent = 0, failed = 0;

                for (size_t i = 0; i < total; i++) {
                    const ctf_event_t *ev = &events[i];
                    char *text = formatter_event(ev);

                    if (telegram_send_message(tcfg, text)) {
                        if (db_mark_as_posted(db, ev->id)) {
                            log_msg(log_file, "info", "Published event #%u: \"%s\"", ev->id, ev->title);
                            sent++;
                        } else {
                            /* Sent successfully, but the DB write that records that fact
                             * failed: posted_at stays NULL, so this event will be re-sent
                             * to Telegram on the next run unless an operator intervenes.
                             * Loud on purpose -- this is the one failure mode that silently
                             * produces a duplicate notification rather than a missed one. */
                            log_msg(log_file, "error",
                                    "Event #%u (\"%s\") was sent to Telegram but marking it as "
                                    "posted failed (%s) -- it WILL be re-sent next run unless "
                                    "posted_at is set manually.",
                                    ev->id, ev->title, db_error(db));
                            sent++;
                        }
                    } else {
                        log_msg(log_file, "warn",
                                "Failed to send event #%u (\"%s\") -- will retry on next run.",
                                ev->id, ev->title);
                        failed++;
                    }

                    free(text);

                    if (tcfg->sleep_between_messages > 0 && i < total - 1) {
                        sleep((unsigned int) tcfg->sleep_between_messages);
                    }
                }

                log_msg(log_file, "info", "Daily update done. Sent: %zu | Failed: %zu", sent, failed);
            }

            ctf_event_array_free(events, total);
        }

        db_close(db);
    }

    http_global_cleanup();
    lock_release(&lock, LOCK_PATH);
    return exit_code;
}
