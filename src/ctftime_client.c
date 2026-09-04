#include "ctftime_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"

#define BASE_URL "https://ctftime.org/api/v1"

/* Guards against accidental SSRF if BASE_URL is ever changed: only
 * ctftime.org (or a subdomain, over https) is ever requested. */
static int is_allowed_url(const char *url)
{
    if (strncmp(url, "https://ctftime.org/", 20) == 0) {
        return 1;
    }
    if (strncmp(url, "https://", 8) == 0) {
        const char *host_start = url + 8;
        const char *slash = strchr(host_start, '/');
        size_t host_len = slash ? (size_t) (slash - host_start) : strlen(host_start);
        static const char suffix[] = ".ctftime.org";
        size_t sl = strlen(suffix);
        if (host_len > sl && strncmp(host_start + host_len - sl, suffix, sl) == 0) {
            return 1;
        }
    }
    return 0;
}

int ctftime_fetch_event_ids(int timeout_sec, time_t start, time_t finish, int limit, id_list_t *out)
{
    out->ids = NULL;
    out->count = 0;

    char url[256];
    snprintf(url, sizeof(url), "%s/events/?limit=%d&start=%ld&finish=%ld",
             BASE_URL, limit, (long) start, (long) finish);

    if (!is_allowed_url(url)) {
        return 0;
    }

    http_response_t resp;
    long status = 0;
    if (!http_get(url, timeout_sec, &resp, &status)) {
        return 0;
    }
    if (status != 200) {
        http_response_free(&resp);
        return 0;
    }

    json_value_t *events = json_parse(resp.data, resp.len, 16);
    http_response_free(&resp);
    if (!json_is_array(events)) {
        json_free(events);
        return 0;
    }

    size_t n = json_array_size(events);
    unsigned int *ids = n ? malloc(sizeof(unsigned int) * n) : NULL;
    if (n && !ids) {
        json_free(events);
        return 0;
    }
    size_t count = 0;

    for (size_t i = 0; i < n; i++) {
        json_value_t *ev = json_array_get(events, i);
        long id;
        if (json_get_int(ev, "id", &id) && id > 0) {
            ids[count++] = (unsigned int) id;
        }
    }

    json_free(events);
    out->ids = ids;
    out->count = count;
    return 1;
}

void id_list_free(id_list_t *l)
{
    if (!l) {
        return;
    }
    free(l->ids);
    l->ids = NULL;
    l->count = 0;
}

json_value_t *ctftime_fetch_event_detail(int timeout_sec, unsigned int id)
{
    if (id == 0) {
        return NULL;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/events/%u/", BASE_URL, id);

    if (!is_allowed_url(url)) {
        return NULL;
    }

    http_response_t resp;
    long status = 0;
    if (!http_get(url, timeout_sec, &resp, &status)) {
        return NULL;
    }
    if (status != 200) {
        http_response_free(&resp);
        return NULL;
    }

    json_value_t *data = json_parse(resp.data, resp.len, 16);
    http_response_free(&resp);

    if (!json_is_object(data) || data->u.object.count == 0) {
        json_free(data);
        return NULL;
    }

    return data;
}
