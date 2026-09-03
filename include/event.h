#ifndef CTF_EVENT_H
#define CTF_EVENT_H

#include <stddef.h>

/* Max *character* (codepoint) counts -- these mirror the VARCHAR column
 * widths in schema.sql, which MySQL also enforces per-character. */
#define TITLE_MAX    255
#define URL_MAX      512
#define FORMAT_MAX   64
#define LOCATION_MAX 255
#define DESC_MAX     65535

/* Byte capacity of the corresponding fixed buffers below. A UTF-8
 * codepoint can take up to 4 bytes, so a field holding up to N *characters*
 * needs up to 4N bytes of storage; URLs are validated ASCII-only so they
 * need no such headroom. */
#define TITLE_BUF    (TITLE_MAX * 4 + 1)
#define FORMAT_BUF   (FORMAT_MAX * 4 + 1)
#define LOCATION_BUF (LOCATION_MAX * 4 + 1)

/* Mirrors one row of the `ctf_events` table. `description` is heap-allocated
 * since it can be up to 64KB * 4 bytes (TEXT column). */
typedef struct {
    unsigned int id;

    char title[TITLE_BUF];

    char url[URL_MAX + 1];
    int has_url;

    char ctftime_url[URL_MAX + 1];
    int has_ctftime_url;

    char start_time[20]; /* "YYYY-MM-DD HH:MM:SS" */
    int has_start_time;

    char finish_time[20];
    int has_finish_time;

    char format[FORMAT_BUF];
    int has_format;

    double weight;
    int has_weight;

    int onsite;

    char location[LOCATION_BUF];
    int has_location;

    char *description; /* malloc'd, NULL if absent */

    char logo_url[URL_MAX + 1];
    int has_logo_url;

    int is_safe;

    char posted_at[20];
    int has_posted_at;
} ctf_event_t;

void ctf_event_init(ctf_event_t *ev);
void ctf_event_free(ctf_event_t *ev);

/* Frees an array of `count` events (including each event's owned memory)
 * and the array itself. */
void ctf_event_array_free(ctf_event_t *events, size_t count);

#endif
