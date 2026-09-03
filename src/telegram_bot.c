#include "telegram_bot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "http.h"
#include "json.h"
#include "util.h"

#define TELEGRAM_TIMEOUT 10
#define MAX_RETRIES 1

static char *build_body(const telegram_config_t *cfg, const char *text)
{
    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append(&sb, "{");

    char *chat_id_esc = json_escape_alloc(cfg->chat_id);
    strbuf_append_fmt(&sb, "\"chat_id\":\"%s\",", chat_id_esc);
    free(chat_id_esc);

    strbuf_append_fmt(&sb, "\"message_thread_id\":%ld,", cfg->thread_id);

    char *text_esc = json_escape_alloc(text);
    strbuf_append_fmt(&sb, "\"text\":\"%s\",", text_esc);
    free(text_esc);

    strbuf_append(&sb, "\"parse_mode\":\"HTML\",");
    strbuf_append(&sb, "\"disable_web_page_preview\":false");
    strbuf_append(&sb, "}");

    return sb.data;
}

/* POSTs to the Telegram Bot API. The response body is parsed for both 200
 * (success) and 429 (rate limit -- needed to read `retry_after`); any
 * other status or transport failure returns NULL. */
static json_value_t *telegram_post(const telegram_config_t *cfg, const char *method, const char *body)
{
    char url[512];
    snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", cfg->bot_token, method);

    http_response_t resp;
    long status = 0;
    if (!http_post_json(url, body, TELEGRAM_TIMEOUT, &resp, &status)) {
        return NULL;
    }

    if (status != 200 && status != 429) {
        http_response_free(&resp);
        return NULL;
    }

    json_value_t *parsed = json_parse(resp.data, resp.len, 8);
    http_response_free(&resp);
    return parsed;
}

int telegram_send_message(const telegram_config_t *cfg, const char *text)
{
    char *body = build_body(cfg, text);
    int result = 0;

    for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
        json_value_t *response = telegram_post(cfg, "sendMessage", body);
        if (!response) {
            break; /* transport error: unrecoverable */
        }

        if (json_get_bool(response, "ok", 0)) {
            json_free(response);
            result = 1;
            break;
        }

        long error_code = 0;
        json_get_int(response, "error_code", &error_code);

        if (error_code == 429) {
            json_value_t *params = json_object_get(response, "parameters");
            long retry_after = 5;
            json_get_int(params, "retry_after", &retry_after);
            json_free(response);
            sleep((unsigned int) (retry_after + 1));
            continue; /* honour the rate limit window, then retry once */
        }

        json_free(response);
        break; /* any other Telegram API error: fail immediately */
    }

    free(body);
    return result;
}
