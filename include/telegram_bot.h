#ifndef CTF_TELEGRAM_BOT_H
#define CTF_TELEGRAM_BOT_H

#include "config.h"

/* Sends an HTML-formatted message to the configured supergroup topic via
 * the Telegram Bot API. On HTTP 429 (rate limited), sleeps for the
 * server-supplied retry_after and retries once; any other API/transport
 * error returns 0 immediately. Returns 1 on success. */
int telegram_send_message(const telegram_config_t *cfg, const char *text);

#endif
