#include "formatter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

#define DESC_PREVIEW_LEN 300
#define TELEGRAM_MAX_LEN 4096

static const char *MONTH_ABBR[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static void format_date_str(time_t t, int with_year, char *out, size_t outsz)
{
    struct tm tmv;
    gmtime_r(&t, &tmv);
    if (with_year) {
        snprintf(out, outsz, "%02d %s %d", tmv.tm_mday, MONTH_ABBR[tmv.tm_mon], tmv.tm_year + 1900);
    } else {
        snprintf(out, outsz, "%02d %s", tmv.tm_mday, MONTH_ABBR[tmv.tm_mon]);
    }
}

/* "28 Mar — 30 Mar 2026 (UTC)" or "28 Mar 2026 (UTC)"; NULL if no start time. */
static char *format_dates_alloc(const ctf_event_t *ev)
{
    if (!ev->has_start_time) {
        return NULL;
    }
    time_t start_ts;
    if (!parse_mysql_datetime(ev->start_time, &start_ts)) {
        return NULL;
    }

    char start_str[24];
    format_date_str(start_ts, 1, start_str, sizeof(start_str));

    if (ev->has_finish_time) {
        time_t finish_ts;
        if (parse_mysql_datetime(ev->finish_time, &finish_ts) && finish_ts > start_ts) {
            char finish_str[24];
            format_date_str(finish_ts, 1, finish_str, sizeof(finish_str));
            if (strcmp(finish_str, start_str) != 0) {
                strbuf_t sb;
                strbuf_init(&sb);
                strbuf_append_fmt(&sb, "%s \xE2\x80\x94 %s (UTC)", start_str, finish_str);
                return sb.data;
            }
        }
    }

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append_fmt(&sb, "%s (UTC)", start_str);
    return sb.data;
}

/* "28 Mar — 30 Mar" or "28 Mar"; NULL if no start time. */
static char *format_compact_dates_alloc(const ctf_event_t *ev)
{
    if (!ev->has_start_time) {
        return NULL;
    }
    time_t start_ts;
    if (!parse_mysql_datetime(ev->start_time, &start_ts)) {
        return NULL;
    }

    char start_str[16];
    format_date_str(start_ts, 0, start_str, sizeof(start_str));

    if (ev->has_finish_time) {
        time_t finish_ts;
        if (parse_mysql_datetime(ev->finish_time, &finish_ts) && finish_ts > start_ts) {
            char finish_str[16];
            format_date_str(finish_ts, 0, finish_str, sizeof(finish_str));
            if (strcmp(finish_str, start_str) != 0) {
                strbuf_t sb;
                strbuf_init(&sb);
                strbuf_append_fmt(&sb, "%s \xE2\x80\x94 %s", start_str, finish_str);
                return sb.data;
            }
        }
    }

    return strdup(start_str);
}

/* "Jeopardy | Weight: 74.85", "Jeopardy", "Weight: 25.00", or NULL. */
static char *format_meta_alloc(const ctf_event_t *ev)
{
    strbuf_t sb;
    strbuf_init(&sb);
    int any = 0;

    if (ev->has_format && ev->format[0] != '\0') {
        char *esc = html_escape_alloc(ev->format);
        strbuf_append(&sb, esc);
        free(esc);
        any = 1;
    }
    if (ev->has_weight && ev->weight > 0) {
        if (any) {
            strbuf_append(&sb, " | ");
        }
        strbuf_append_fmt(&sb, "Weight: %.2f", ev->weight);
        any = 1;
    }

    if (!any) {
        strbuf_free(&sb);
        return NULL;
    }
    return sb.data;
}

/* "<a href=\"...\">Event site</a>  ·  <a href=\"...\">CTFTime</a>", or NULL. */
static char *format_links_alloc(const ctf_event_t *ev)
{
    if (!ev->has_url && !ev->has_ctftime_url) {
        return NULL;
    }

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append(&sb, "\xF0\x9F\x94\x97 ");
    int any = 0;

    if (ev->has_url) {
        char *esc = html_escape_alloc(ev->url);
        strbuf_append_fmt(&sb, "<a href=\"%s\">Event site</a>", esc);
        free(esc);
        any = 1;
    }
    if (ev->has_ctftime_url) {
        if (any) {
            strbuf_append(&sb, "  \xC2\xB7  ");
        }
        char *esc = html_escape_alloc(ev->ctftime_url);
        strbuf_append_fmt(&sb, "<a href=\"%s\">CTFTime</a>", esc);
        free(esc);
    }

    return sb.data;
}

/* Trims `text`, then truncates to at most `max_chars` UTF-8 codepoints,
 * appending an ellipsis if it was cut. */
static char *truncate_alloc(const char *text, size_t max_chars)
{
    char *copy = strdup(text);
    trim_inplace(copy);

    if (utf8_strlen(copy) <= max_chars) {
        return copy;
    }

    char *sub = utf8_substr_alloc(copy, max_chars);
    free(copy);

    strbuf_t sb;
    strbuf_init(&sb);
    strbuf_append(&sb, sub);
    strbuf_append(&sb, "\xE2\x80\xA6");
    free(sub);
    return sb.data;
}

char *formatter_event(const ctf_event_t *ev)
{
    char *lines[9];
    size_t n = 0;

    char *title_esc = html_escape_alloc(ev->title);
    {
        strbuf_t l;
        strbuf_init(&l);
        strbuf_append_fmt(&l, "\xF0\x9F\x9A\xA9 <b>%s</b>", title_esc);
        lines[n++] = l.data;
    }
    free(title_esc);
    lines[n++] = strdup("");

    char *dates = format_dates_alloc(ev);
    if (dates) {
        strbuf_t l;
        strbuf_init(&l);
        strbuf_append_fmt(&l, "\xF0\x9F\x93\x85 %s", dates);
        lines[n++] = l.data;
        free(dates);
    }

    char *meta = format_meta_alloc(ev);
    if (meta) {
        strbuf_t l;
        strbuf_init(&l);
        strbuf_append_fmt(&l, "\xF0\x9F\x8F\x86 %s", meta);
        lines[n++] = l.data;
        free(meta);
    }

    if (ev->onsite && ev->has_location && ev->location[0] != '\0') {
        char *loc_esc = html_escape_alloc(ev->location);
        strbuf_t l;
        strbuf_init(&l);
        strbuf_append_fmt(&l, "\xF0\x9F\x93\x8D %s", loc_esc);
        lines[n++] = l.data;
        free(loc_esc);
    } else {
        lines[n++] = strdup("\xF0\x9F\x8C\x90 Online");
    }

    if (ev->description && ev->description[0] != '\0') {
        char *preview = truncate_alloc(ev->description, DESC_PREVIEW_LEN);
        if (preview[0] != '\0') {
            lines[n++] = strdup("");
            lines[n++] = html_escape_alloc(preview);
        }
        free(preview);
    }

    char *links = format_links_alloc(ev);
    if (links) {
        lines[n++] = strdup("");
        lines[n++] = links;
    }

    strbuf_t out;
    strbuf_init(&out);
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            strbuf_append(&out, "\n");
        }
        strbuf_append(&out, lines[i]);
        free(lines[i]);
    }

    return out.data;
}

char **formatter_digest(const ctf_event_t *events, size_t count, int days, size_t *parts_count)
{
    *parts_count = 0;
    if (count == 0) {
        return NULL;
    }

    time_t now = time(NULL);
    time_t until = now + (time_t) days * 86400;
    char now_str[24], until_str[24];
    format_date_str(now, 1, now_str, sizeof(now_str));
    format_date_str(until, 1, until_str, sizeof(until_str));

    strbuf_t header;
    strbuf_init(&header);
    strbuf_append_fmt(&header,
        "\xF0\x9F\x93\x8B <b>CTF Events \xE2\x80\x94 next %d days</b>\n<i>%s \xE2\x80\x93 %s</i>",
        days, now_str, until_str);
    const char *cont_header = "\xF0\x9F\x93\x8B <b>CTF Events (cont.)</b>";

    /* Build one compact "• <link>\n  📅 meta | meta | ..." block per event. */
    char **items = malloc(sizeof(char *) * count);
    if (!items) {
        strbuf_free(&header);
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        const ctf_event_t *ev = &events[i];

        char *title_esc = html_escape_alloc(ev->title);
        strbuf_t link;
        strbuf_init(&link);
        if (ev->has_ctftime_url) {
            char *url_esc = html_escape_alloc(ev->ctftime_url);
            strbuf_append_fmt(&link, "<a href=\"%s\">%s</a>", url_esc, title_esc);
            free(url_esc);
        } else {
            strbuf_append(&link, title_esc);
        }
        free(title_esc);

        strbuf_t meta;
        strbuf_init(&meta);
        int any_meta = 0;

        char *cdates = format_compact_dates_alloc(ev);
        if (cdates) {
            strbuf_append(&meta, cdates);
            free(cdates);
            any_meta = 1;
        }
        if (ev->has_format && ev->format[0] != '\0') {
            if (any_meta) strbuf_append(&meta, " | ");
            char *fmt_esc = html_escape_alloc(ev->format);
            strbuf_append(&meta, fmt_esc);
            free(fmt_esc);
            any_meta = 1;
        }
        if (any_meta) strbuf_append(&meta, " | ");
        if (ev->onsite && ev->has_location && ev->location[0] != '\0') {
            char *loc_esc = html_escape_alloc(ev->location);
            strbuf_append(&meta, loc_esc);
            free(loc_esc);
        } else {
            strbuf_append(&meta, "Online");
        }

        strbuf_t item;
        strbuf_init(&item);
        strbuf_append_fmt(&item, "\xE2\x80\xA2 %s\n  \xF0\x9F\x93\x85 %s", link.data, meta.data);
        strbuf_free(&link);
        strbuf_free(&meta);

        items[i] = item.data;
    }

    /* Pack items into <=4096-char Telegram message parts. */
    char **parts = malloc(sizeof(char *) * (count + 1));
    if (!parts) {
        strbuf_free(&header);
        for (size_t i = 0; i < count; i++) {
            free(items[i]);
        }
        free(items);
        return NULL;
    }
    size_t parts_n = 0;

    char *current_header = strdup(header.data);
    strbuf_free(&header);
    char *current_body = strdup("");

    for (size_t i = 0; i < count; i++) {
        const char *item = items[i];

        strbuf_t candidate;
        strbuf_init(&candidate);
        strbuf_append(&candidate, current_header);
        strbuf_append(&candidate, "\n\n");
        if (current_body[0] != '\0') {
            strbuf_append(&candidate, current_body);
            strbuf_append(&candidate, "\n\n");
        }
        strbuf_append(&candidate, item);

        if (utf8_strlen(candidate.data) > TELEGRAM_MAX_LEN && current_body[0] != '\0') {
            strbuf_t flushed;
            strbuf_init(&flushed);
            strbuf_append(&flushed, current_header);
            strbuf_append(&flushed, "\n\n");
            strbuf_append(&flushed, current_body);
            parts[parts_n++] = flushed.data;

            free(current_header);
            current_header = strdup(cont_header);
            free(current_body);
            current_body = strdup(item);
        } else {
            char *new_body;
            if (current_body[0] == '\0') {
                new_body = strdup(item);
            } else {
                strbuf_t nb;
                strbuf_init(&nb);
                strbuf_append(&nb, current_body);
                strbuf_append(&nb, "\n\n");
                strbuf_append(&nb, item);
                new_body = nb.data;
            }
            free(current_body);
            current_body = new_body;
        }

        strbuf_free(&candidate);
    }

    if (current_body[0] != '\0') {
        strbuf_t flushed;
        strbuf_init(&flushed);
        strbuf_append(&flushed, current_header);
        strbuf_append(&flushed, "\n\n");
        strbuf_append(&flushed, current_body);
        parts[parts_n++] = flushed.data;
    }

    free(current_header);
    free(current_body);
    for (size_t i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);

    *parts_count = parts_n;
    return parts;
}

void formatter_free_parts(char **parts, size_t count)
{
    if (!parts) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        free(parts[i]);
    }
    free(parts);
}
