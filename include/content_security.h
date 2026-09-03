#ifndef CTF_CONTENT_SECURITY_H
#define CTF_CONTENT_SECURITY_H

#include "event.h"
#include "json.h"

/* Validates and sanitizes a raw CTFTime API event object into `out`.
 *
 * `forced_id` is the event ID taken from the request URL path (never from
 * the response body) as an anti-spoofing measure, and always wins over
 * whatever `raw["id"]` says.
 *
 * `fallback_ctftime_url` is used to fill out->ctftime_url only when the
 * response did not provide one (pass e.g. "https://ctftime.org/event/123").
 *
 * Returns 1 if the event is well-formed enough to store (out->is_safe
 * reports whether the content also passed the XSS/SSTI/SQLi/SSRF checks —
 * an event can be "valid" but "unsafe"). Returns 0 if the event is missing
 * required fields (no positive id, no title) and must be discarded
 * entirely, mirroring ContentSecurity::sanitize() returning null. */
int content_security_sanitize(const json_value_t *raw, unsigned int forced_id,
                               const char *fallback_ctftime_url, ctf_event_t *out);

#endif
