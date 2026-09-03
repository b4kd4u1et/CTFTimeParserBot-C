#ifndef CTF_DB_H
#define CTF_DB_H

#include <stddef.h>

#include "config.h"
#include "event.h"

typedef struct db db_t;

/* Opens a connection using the given [db] config. Returns a heap-allocated
 * handle on success (caller must db_close()), or NULL on failure (an
 * explanatory message is written to stderr). */
db_t *db_connect(const db_config_t *cfg);
void db_close(db_t *db);

/* Returns the last error message for this connection (valid until the
 * next db_* call on the same handle). */
const char *db_error(db_t *db);

/* Inserts new event IDs into parser_buffer in a single batch query.
 * Duplicates are silently ignored (INSERT IGNORE). Returns 1 on success. */
int db_insert_buffer(db_t *db, const unsigned int *ids, size_t count);

/* Removes IDs from parser_buffer that already exist in ctf_events.
 * Returns 1 on success. */
int db_clean_buffer(db_t *db);

/* Returns all event IDs remaining in parser_buffer, ascending. Caller must
 * free(*ids_out). Returns 1 on success (count may be 0). */
int db_get_buffer_ids(db_t *db, unsigned int **ids_out, size_t *count_out);

/* Removes a single ID from parser_buffer. Returns 1 on success. */
int db_delete_from_buffer(db_t *db, unsigned int id);

/* Inserts a sanitized event into ctf_events (INSERT ... ON DUPLICATE KEY
 * UPDATE, mirroring the original's upsert semantics). Returns 1 on success. */
int db_insert_event(db_t *db, const ctf_event_t *ev);

/* Returns all safe events not yet published, ordered by start_time ASC.
 * Caller must ctf_event_array_free(*events_out, *count_out). */
int db_get_unpublished_events(db_t *db, ctf_event_t **events_out, size_t *count_out);

/* Returns safe events starting within the next `days` days, ordered by
 * start_time ASC. Caller must ctf_event_array_free(*events_out, *count_out). */
int db_get_upcoming_events(db_t *db, int days, ctf_event_t **events_out, size_t *count_out);

/* Marks an event as published (posted_at = NOW()). Returns 1 on success. */
int db_mark_as_posted(db_t *db, unsigned int id);

#endif
