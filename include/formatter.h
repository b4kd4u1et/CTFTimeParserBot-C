#ifndef CTF_FORMATTER_H
#define CTF_FORMATTER_H

#include <stddef.h>

#include "event.h"

/* Formats a single ctf_events row into a Telegram HTML message (the same
 * limited subset Telegram supports: <b>, <i>, <a href="...">, ...).
 * Returns a newly malloc'd string the caller must free(). */
char *formatter_event(const ctf_event_t *ev);

/* Formats a list of events (ordered by start_time ASC) into a compact
 * weekly digest. If the combined text would exceed Telegram's 4096
 * character message limit, the digest is split into multiple parts, each
 * with its own header. Returns a newly malloc'd array of `*parts_count`
 * newly malloc'd strings (both the array and each string must be freed by
 * the caller, or via formatter_free_parts()). Returns NULL with
 * *parts_count == 0 if `events` is empty. */
char **formatter_digest(const ctf_event_t *events, size_t count, int days, size_t *parts_count);

void formatter_free_parts(char **parts, size_t count);

#endif
