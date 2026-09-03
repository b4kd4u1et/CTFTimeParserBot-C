#include "db.h"

#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct db {
    MYSQL *conn;
};

db_t *db_connect(const db_config_t *cfg)
{
    MYSQL *conn = mysql_init(NULL);
    if (!conn) {
        return NULL;
    }

    unsigned int port = (unsigned int) cfg->port;
    if (!mysql_real_connect(conn, cfg->host, cfg->user, cfg->pass, cfg->name, port, NULL, 0)) {
        fprintf(stderr, "db: connection failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    const char *charset = cfg->charset[0] ? cfg->charset : "utf8mb4";
    if (mysql_set_character_set(conn, charset) != 0) {
        fprintf(stderr, "db: failed to set charset '%s': %s\n", charset, mysql_error(conn));
        mysql_close(conn);
        return NULL;
    }

    db_t *db = malloc(sizeof(*db));
    db->conn = conn;
    return db;
}

void db_close(db_t *db)
{
    if (!db) {
        return;
    }
    if (db->conn) {
        mysql_close(db->conn);
    }
    free(db);
}

const char *db_error(db_t *db)
{
    return (db && db->conn) ? mysql_error(db->conn) : "no connection";
}

int db_insert_buffer(db_t *db, const unsigned int *ids, size_t count)
{
    if (count == 0) {
        return 1;
    }

    size_t qcap = 64 + count * 4;
    char *query = malloc(qcap);
    size_t off = (size_t) snprintf(query, qcap, "INSERT IGNORE INTO `parser_buffer` (`event_id`) VALUES ");
    for (size_t i = 0; i < count; i++) {
        off += (size_t) snprintf(query + off, qcap - off, "%s(?)", i > 0 ? "," : "");
    }

    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        free(query);
        return 0;
    }
    if (mysql_stmt_prepare(stmt, query, (unsigned long) off) != 0) {
        free(query);
        mysql_stmt_close(stmt);
        return 0;
    }
    free(query);

    MYSQL_BIND *binds = calloc(count, sizeof(MYSQL_BIND));
    for (size_t i = 0; i < count; i++) {
        binds[i].buffer_type = MYSQL_TYPE_LONG;
        binds[i].buffer = (void *) &ids[i];
        binds[i].is_unsigned = 1;
    }

    int ok = (mysql_stmt_bind_param(stmt, binds) == 0) && (mysql_stmt_execute(stmt) == 0);

    free(binds);
    mysql_stmt_close(stmt);
    return ok;
}

int db_clean_buffer(db_t *db)
{
    const char *sql =
        "DELETE FROM `parser_buffer` WHERE `event_id` IN (SELECT `id` FROM `ctf_events`)";
    return mysql_query(db->conn, sql) == 0;
}

int db_get_buffer_ids(db_t *db, unsigned int **ids_out, size_t *count_out)
{
    *ids_out = NULL;
    *count_out = 0;

    if (mysql_query(db->conn, "SELECT `event_id` FROM `parser_buffer` ORDER BY `event_id` ASC") != 0) {
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(db->conn);
    if (!res) {
        return 0;
    }

    my_ulonglong n = mysql_num_rows(res);
    unsigned int *ids = n ? malloc(sizeof(unsigned int) * (size_t) n) : NULL;
    size_t i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        ids[i++] = (unsigned int) strtoul(row[0], NULL, 10);
    }
    mysql_free_result(res);

    *ids_out = ids;
    *count_out = i;
    return 1;
}

int db_delete_from_buffer(db_t *db, unsigned int id)
{
    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        return 0;
    }
    const char *sql = "DELETE FROM `parser_buffer` WHERE `event_id` = ?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND bind;
    memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &id;
    bind.is_unsigned = 1;

    int ok = (mysql_stmt_bind_param(stmt, &bind) == 0) && (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

int db_insert_event(db_t *db, const ctf_event_t *ev)
{
    static const char *sql =
        "INSERT INTO `ctf_events` "
        "(`id`, `title`, `url`, `ctftime_url`, `start_time`, `finish_time`, "
        " `format`, `weight`, `onsite`, `location`, `description`, `logo_url`, `is_safe`) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON DUPLICATE KEY UPDATE "
        "`title` = VALUES(`title`), `url` = VALUES(`url`), `ctftime_url` = VALUES(`ctftime_url`), "
        "`start_time` = VALUES(`start_time`), `finish_time` = VALUES(`finish_time`), "
        "`format` = VALUES(`format`), `weight` = VALUES(`weight`), `onsite` = VALUES(`onsite`), "
        "`location` = VALUES(`location`), `description` = VALUES(`description`), "
        "`logo_url` = VALUES(`logo_url`)";

    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        return 0;
    }
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND binds[13];
    memset(binds, 0, sizeof(binds));

    unsigned int id = ev->id;
    binds[0].buffer_type = MYSQL_TYPE_LONG;
    binds[0].buffer = &id;
    binds[0].is_unsigned = 1;

    unsigned long title_len = (unsigned long) strlen(ev->title);
    binds[1].buffer_type = MYSQL_TYPE_STRING;
    binds[1].buffer = (void *) ev->title;
    binds[1].buffer_length = title_len;
    binds[1].length = &title_len;

    my_bool url_null = !ev->has_url;
    unsigned long url_len = ev->has_url ? (unsigned long) strlen(ev->url) : 0;
    binds[2].buffer_type = MYSQL_TYPE_STRING;
    binds[2].buffer = (void *) ev->url;
    binds[2].buffer_length = url_len;
    binds[2].length = &url_len;
    binds[2].is_null = &url_null;

    my_bool ctftime_url_null = !ev->has_ctftime_url;
    unsigned long ctftime_url_len = ev->has_ctftime_url ? (unsigned long) strlen(ev->ctftime_url) : 0;
    binds[3].buffer_type = MYSQL_TYPE_STRING;
    binds[3].buffer = (void *) ev->ctftime_url;
    binds[3].buffer_length = ctftime_url_len;
    binds[3].length = &ctftime_url_len;
    binds[3].is_null = &ctftime_url_null;

    my_bool start_null = !ev->has_start_time;
    unsigned long start_len = ev->has_start_time ? (unsigned long) strlen(ev->start_time) : 0;
    binds[4].buffer_type = MYSQL_TYPE_STRING;
    binds[4].buffer = (void *) ev->start_time;
    binds[4].buffer_length = start_len;
    binds[4].length = &start_len;
    binds[4].is_null = &start_null;

    my_bool finish_null = !ev->has_finish_time;
    unsigned long finish_len = ev->has_finish_time ? (unsigned long) strlen(ev->finish_time) : 0;
    binds[5].buffer_type = MYSQL_TYPE_STRING;
    binds[5].buffer = (void *) ev->finish_time;
    binds[5].buffer_length = finish_len;
    binds[5].length = &finish_len;
    binds[5].is_null = &finish_null;

    my_bool format_null = !ev->has_format;
    unsigned long format_len = ev->has_format ? (unsigned long) strlen(ev->format) : 0;
    binds[6].buffer_type = MYSQL_TYPE_STRING;
    binds[6].buffer = (void *) ev->format;
    binds[6].buffer_length = format_len;
    binds[6].length = &format_len;
    binds[6].is_null = &format_null;

    my_bool weight_null = !ev->has_weight;
    double weight = ev->weight;
    binds[7].buffer_type = MYSQL_TYPE_DOUBLE;
    binds[7].buffer = &weight;
    binds[7].is_null = &weight_null;

    signed char onsite = ev->onsite ? 1 : 0;
    binds[8].buffer_type = MYSQL_TYPE_TINY;
    binds[8].buffer = &onsite;

    my_bool location_null = !ev->has_location;
    unsigned long location_len = ev->has_location ? (unsigned long) strlen(ev->location) : 0;
    binds[9].buffer_type = MYSQL_TYPE_STRING;
    binds[9].buffer = (void *) ev->location;
    binds[9].buffer_length = location_len;
    binds[9].length = &location_len;
    binds[9].is_null = &location_null;

    my_bool desc_null = (ev->description == NULL);
    unsigned long desc_len = ev->description ? (unsigned long) strlen(ev->description) : 0;
    binds[10].buffer_type = MYSQL_TYPE_STRING;
    binds[10].buffer = (void *) (ev->description ? ev->description : "");
    binds[10].buffer_length = desc_len;
    binds[10].length = &desc_len;
    binds[10].is_null = &desc_null;

    my_bool logo_null = !ev->has_logo_url;
    unsigned long logo_len = ev->has_logo_url ? (unsigned long) strlen(ev->logo_url) : 0;
    binds[11].buffer_type = MYSQL_TYPE_STRING;
    binds[11].buffer = (void *) ev->logo_url;
    binds[11].buffer_length = logo_len;
    binds[11].length = &logo_len;
    binds[11].is_null = &logo_null;

    signed char is_safe = ev->is_safe ? 1 : 0;
    binds[12].buffer_type = MYSQL_TYPE_TINY;
    binds[12].buffer = &is_safe;

    int ok = (mysql_stmt_bind_param(stmt, binds) == 0) && (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}

/* Maps one SELECT-list row (see the explicit column list in fetch_events())
 * into a ctf_event_t. */
static void row_to_event(MYSQL_ROW row, ctf_event_t *ev)
{
    ctf_event_init(ev);

    ev->id = row[0] ? (unsigned int) strtoul(row[0], NULL, 10) : 0;
    if (row[1]) snprintf(ev->title, sizeof(ev->title), "%s", row[1]);
    if (row[2]) { snprintf(ev->url, sizeof(ev->url), "%s", row[2]); ev->has_url = 1; }
    if (row[3]) { snprintf(ev->ctftime_url, sizeof(ev->ctftime_url), "%s", row[3]); ev->has_ctftime_url = 1; }
    if (row[4]) { snprintf(ev->start_time, sizeof(ev->start_time), "%s", row[4]); ev->has_start_time = 1; }
    if (row[5]) { snprintf(ev->finish_time, sizeof(ev->finish_time), "%s", row[5]); ev->has_finish_time = 1; }
    if (row[6]) { snprintf(ev->format, sizeof(ev->format), "%s", row[6]); ev->has_format = 1; }
    if (row[7]) { ev->weight = atof(row[7]); ev->has_weight = 1; }
    ev->onsite = row[8] ? atoi(row[8]) : 0;
    if (row[9]) { snprintf(ev->location, sizeof(ev->location), "%s", row[9]); ev->has_location = 1; }
    if (row[10]) ev->description = strdup(row[10]);
    if (row[11]) { snprintf(ev->logo_url, sizeof(ev->logo_url), "%s", row[11]); ev->has_logo_url = 1; }
    ev->is_safe = row[12] ? atoi(row[12]) : 0;
    if (row[13]) { snprintf(ev->posted_at, sizeof(ev->posted_at), "%s", row[13]); ev->has_posted_at = 1; }
    /* row[14] (created_at) is not needed by any caller and is left unmapped. */
}

#define EVENT_SELECT_COLUMNS \
    "id, title, url, ctftime_url, start_time, finish_time, format, weight, " \
    "onsite, location, description, logo_url, is_safe, posted_at, created_at "

static int fetch_events(MYSQL *conn, const char *sql, ctf_event_t **events_out, size_t *count_out)
{
    *events_out = NULL;
    *count_out = 0;

    if (mysql_query(conn, sql) != 0) {
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        return 0;
    }

    my_ulonglong n = mysql_num_rows(res);
    ctf_event_t *events = n ? malloc(sizeof(ctf_event_t) * (size_t) n) : NULL;
    size_t i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        row_to_event(row, &events[i]);
        i++;
    }
    mysql_free_result(res);

    *events_out = events;
    *count_out = i;
    return 1;
}

int db_get_unpublished_events(db_t *db, ctf_event_t **events_out, size_t *count_out)
{
    const char *sql =
        "SELECT " EVENT_SELECT_COLUMNS
        "FROM `ctf_events` WHERE `posted_at` IS NULL AND `is_safe` = 1 ORDER BY `start_time` ASC";
    return fetch_events(db->conn, sql, events_out, count_out);
}

int db_get_upcoming_events(db_t *db, int days, ctf_event_t **events_out, size_t *count_out)
{
    /* `days` always comes from our own config file (an int), never from
     * external input, so plain interpolation carries no injection risk. */
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT " EVENT_SELECT_COLUMNS
        "FROM `ctf_events` WHERE `is_safe` = 1 AND `start_time` >= NOW() "
        "AND `start_time` <= DATE_ADD(NOW(), INTERVAL %d DAY) ORDER BY `start_time` ASC",
        days);
    return fetch_events(db->conn, sql, events_out, count_out);
}

int db_mark_as_posted(db_t *db, unsigned int id)
{
    MYSQL_STMT *stmt = mysql_stmt_init(db->conn);
    if (!stmt) {
        return 0;
    }
    const char *sql = "UPDATE `ctf_events` SET `posted_at` = NOW() WHERE `id` = ?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        mysql_stmt_close(stmt);
        return 0;
    }

    MYSQL_BIND bind;
    memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &id;
    bind.is_unsigned = 1;

    int ok = (mysql_stmt_bind_param(stmt, &bind) == 0) && (mysql_stmt_execute(stmt) == 0);
    mysql_stmt_close(stmt);
    return ok;
}
