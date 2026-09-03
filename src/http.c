#include "http.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

int http_global_init(void)
{
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 1 : 0;
}

void http_global_cleanup(void)
{
    curl_global_cleanup();
}

struct write_ctx {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct write_ctx *ctx = userdata;
    size_t add = size * nmemb;

    if (ctx->len + add + 1 > ctx->cap) {
        size_t newcap = ctx->cap ? ctx->cap * 2 : 4096;
        while (newcap < ctx->len + add + 1) {
            newcap *= 2;
        }
        char *n = realloc(ctx->data, newcap);
        if (!n) {
            return 0; /* signals an error to libcurl, aborting the transfer */
        }
        ctx->data = n;
        ctx->cap = newcap;
    }

    memcpy(ctx->data + ctx->len, ptr, add);
    ctx->len += add;
    ctx->data[ctx->len] = '\0';
    return add;
}

static int do_request(const char *url, const char *json_body, int timeout_sec,
                       http_response_t *out, long *status)
{
    CURL *ch = curl_easy_init();
    if (!ch) {
        return 0;
    }

    struct write_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    struct curl_slist *headers = NULL;

    curl_easy_setopt(ch, CURLOPT_URL, url);
    curl_easy_setopt(ch, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(ch, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(ch, CURLOPT_TIMEOUT, (long) timeout_sec);
    curl_easy_setopt(ch, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(ch, CURLOPT_USERAGENT, "CTFTimeParserBot-C/1.0");
    /* No redirects: prevents SSRF/open-redirect via a 3xx response. */
    curl_easy_setopt(ch, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(ch, CURLOPT_SSL_VERIFYHOST, 2L);

    if (json_body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(ch, CURLOPT_POST, 1L);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDS, json_body);
        curl_easy_setopt(ch, CURLOPT_POSTFIELDSIZE, (long) strlen(json_body));
    } else {
        headers = curl_slist_append(headers, "Accept: application/json");
    }
    curl_easy_setopt(ch, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(ch);

    int ok = 0;
    if (res == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(ch, CURLINFO_RESPONSE_CODE, &code);
        if (status) {
            *status = code;
        }
        out->data = ctx.data ? ctx.data : strdup("");
        out->len = ctx.len;
        ok = 1;
    } else {
        free(ctx.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(ch);
    return ok;
}

int http_get(const char *url, int timeout_sec, http_response_t *out, long *status)
{
    return do_request(url, NULL, timeout_sec, out, status);
}

int http_post_json(const char *url, const char *json_body, int timeout_sec,
                    http_response_t *out, long *status)
{
    return do_request(url, json_body, timeout_sec, out, status);
}

void http_response_free(http_response_t *r)
{
    if (!r) {
        return;
    }
    free(r->data);
    r->data = NULL;
    r->len = 0;
}
