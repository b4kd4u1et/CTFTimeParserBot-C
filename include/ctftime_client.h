#ifndef CTF_CTFTIME_CLIENT_H
#define CTF_CTFTIME_CLIENT_H

#include <stddef.h>
#include <time.h>

#include "json.h"

typedef struct {
    unsigned int *ids;
    size_t count;
} id_list_t;

/* Fetches the list of event IDs starting in [start, finish] (Unix time),
 * up to `limit` results. Returns 1 on success (out->count may be 0 if
 * CTFTime has nothing scheduled) or 0 on any transport/parse failure. */
int ctftime_fetch_event_ids(int timeout_sec, time_t start, time_t finish, int limit, id_list_t *out);

void id_list_free(id_list_t *l);

/* Fetches full details for a single event. Returns a parsed JSON object
 * (caller must json_free() it) or NULL on any transport/parse/HTTP-status
 * failure. */
json_value_t *ctftime_fetch_event_detail(int timeout_sec, unsigned int id);

#endif
