#ifndef CTF_HTTP_H
#define CTF_HTTP_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
} http_response_t;

/* One-time libcurl global init/cleanup. Call http_global_init() once at
 * process start and http_global_cleanup() once before exit. */
int http_global_init(void);
void http_global_cleanup(void);

/* Performs an HTTPS GET. On any transport error returns 0 and leaves
 * `status` / `out` untouched. On success returns 1, sets *status to the
 * HTTP status code, and fills *out with the response body (caller must
 * call http_response_free()). Redirects are never followed and full TLS
 * certificate verification is enforced (SSRF / MITM hardening). */
int http_get(const char *url, int timeout_sec, http_response_t *out, long *status);

/* Performs an HTTPS POST with a JSON body ("Content-Type: application/json").
 * Same return/verification semantics as http_get(). */
int http_post_json(const char *url, const char *json_body, int timeout_sec,
                    http_response_t *out, long *status);

void http_response_free(http_response_t *r);

#endif
