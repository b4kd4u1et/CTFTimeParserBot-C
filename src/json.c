#include "json.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t len;
    size_t pos;
    int depth;
    int max_depth;
    int error;
} parser_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t *b)
{
    b->cap = 16;
    b->data = malloc(b->cap);
    b->len = 0;
    if (b->data) {
        b->data[0] = '\0';
    }
}

static void sb_push(strbuf_t *b, char c)
{
    if (!b->data) {
        return;
    }
    if (b->len + 2 > b->cap) {
        b->cap *= 2;
        b->data = realloc(b->data, b->cap);
        if (!b->data) {
            return;
        }
    }
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

static void sb_push_utf8(strbuf_t *b, unsigned int cp)
{
    if (cp < 0x80) {
        sb_push(b, (char) cp);
    } else if (cp < 0x800) {
        sb_push(b, (char) (0xC0 | (cp >> 6)));
        sb_push(b, (char) (0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sb_push(b, (char) (0xE0 | (cp >> 12)));
        sb_push(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        sb_push(b, (char) (0x80 | (cp & 0x3F)));
    } else {
        sb_push(b, (char) (0xF0 | (cp >> 18)));
        sb_push(b, (char) (0x80 | ((cp >> 12) & 0x3F)));
        sb_push(b, (char) (0x80 | ((cp >> 6) & 0x3F)));
        sb_push(b, (char) (0x80 | (cp & 0x3F)));
    }
}

static int peek(parser_t *p)
{
    return p->pos < p->len ? (unsigned char) p->s[p->pos] : -1;
}

static void skip_ws(parser_t *p)
{
    while (p->pos < p->len) {
        char c = p->s[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static json_value_t *new_value(json_type_t t)
{
    json_value_t *v = calloc(1, sizeof(json_value_t));
    if (v) {
        v->type = t;
    }
    return v;
}

static json_value_t *parse_value(parser_t *p);

static int parse_hex4(parser_t *p, unsigned int *out)
{
    unsigned int v = 0;
    for (int i = 0; i < 4; i++) {
        if (p->pos >= p->len) {
            return 0;
        }
        char c = p->s[p->pos++];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= (unsigned int) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= (unsigned int) (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= (unsigned int) (c - 'A' + 10);
        } else {
            return 0;
        }
    }
    *out = v;
    return 1;
}

static char *parse_string(parser_t *p)
{
    if (peek(p) != '"') {
        p->error = 1;
        return NULL;
    }
    p->pos++;

    strbuf_t b;
    sb_init(&b);

    for (;;) {
        if (p->pos >= p->len) {
            p->error = 1;
            free(b.data);
            return NULL;
        }
        unsigned char c = (unsigned char) p->s[p->pos++];

        if (c == '"') {
            break;
        }

        if (c == '\\') {
            if (p->pos >= p->len) {
                p->error = 1;
                free(b.data);
                return NULL;
            }
            char esc = p->s[p->pos++];
            switch (esc) {
                case '"':  sb_push(&b, '"');  break;
                case '\\': sb_push(&b, '\\'); break;
                case '/':  sb_push(&b, '/');  break;
                case 'b':  sb_push(&b, '\b'); break;
                case 'f':  sb_push(&b, '\f'); break;
                case 'n':  sb_push(&b, '\n'); break;
                case 'r':  sb_push(&b, '\r'); break;
                case 't':  sb_push(&b, '\t'); break;
                case 'u': {
                    unsigned int cp;
                    if (!parse_hex4(p, &cp)) {
                        p->error = 1;
                        free(b.data);
                        return NULL;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (p->pos + 1 < p->len && p->s[p->pos] == '\\' && p->s[p->pos + 1] == 'u') {
                            p->pos += 2;
                            unsigned int lo;
                            if (!parse_hex4(p, &lo)) {
                                p->error = 1;
                                free(b.data);
                                return NULL;
                            }
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            } else {
                                cp = 0xFFFD; /* invalid low surrogate */
                            }
                        } else {
                            cp = 0xFFFD; /* unpaired high surrogate */
                        }
                    }
                    /* A U+0000 escape is dropped rather than embedded, since
                     * the rest of this codebase treats strings as plain
                     * NUL-terminated C strings (see content_security.c). */
                    if (cp != 0) {
                        sb_push_utf8(&b, cp);
                    }
                    break;
                }
                default:
                    p->error = 1;
                    free(b.data);
                    return NULL;
            }
        } else if (c < 0x20) {
            p->error = 1; /* raw control character: not valid inside a JSON string */
            free(b.data);
            return NULL;
        } else {
            sb_push(&b, (char) c);
        }
    }

    return b.data ? b.data : strdup("");
}

static json_value_t *parse_number(parser_t *p)
{
    size_t start = p->pos;

    if (peek(p) == '-') {
        p->pos++;
    }
    while (p->pos < p->len && isdigit((unsigned char) p->s[p->pos])) {
        p->pos++;
    }
    if (p->pos < p->len && p->s[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char) p->s[p->pos])) {
            p->pos++;
        }
    }
    if (p->pos < p->len && (p->s[p->pos] == 'e' || p->s[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->s[p->pos] == '+' || p->s[p->pos] == '-')) {
            p->pos++;
        }
        while (p->pos < p->len && isdigit((unsigned char) p->s[p->pos])) {
            p->pos++;
        }
    }

    size_t numlen = p->pos - start;
    if (numlen == 0) {
        p->error = 1;
        return NULL;
    }

    char buf[64];
    if (numlen >= sizeof(buf)) {
        numlen = sizeof(buf) - 1;
    }
    memcpy(buf, p->s + start, numlen);
    buf[numlen] = '\0';

    json_value_t *v = new_value(JSON_NUMBER);
    if (v) {
        v->u.number = strtod(buf, NULL);
    }
    return v;
}

static int match_lit(parser_t *p, const char *lit)
{
    size_t l = strlen(lit);
    if (p->pos + l > p->len || memcmp(p->s + p->pos, lit, l) != 0) {
        return 0;
    }
    p->pos += l;
    return 1;
}

static json_value_t *parse_array(parser_t *p)
{
    p->pos++; /* consume '[' */
    p->depth++;
    if (p->depth > p->max_depth) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    json_value_t *v = new_value(JSON_ARRAY);
    size_t cap = 4;
    v->u.array.items = malloc(sizeof(json_value_t *) * cap);
    v->u.array.count = 0;

    skip_ws(p);
    if (peek(p) == ']') {
        p->pos++;
        p->depth--;
        return v;
    }

    for (;;) {
        skip_ws(p);
        json_value_t *item = parse_value(p);
        if (!item || p->error) {
            p->depth--;
            json_free(v);
            return NULL;
        }
        if (v->u.array.count == cap) {
            cap *= 2;
            v->u.array.items = realloc(v->u.array.items, sizeof(json_value_t *) * cap);
        }
        v->u.array.items[v->u.array.count++] = item;

        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == ']') {
            p->pos++;
            break;
        }
        p->error = 1;
        p->depth--;
        json_free(v);
        return NULL;
    }

    p->depth--;
    return v;
}

static json_value_t *parse_object(parser_t *p)
{
    p->pos++; /* consume '{' */
    p->depth++;
    if (p->depth > p->max_depth) {
        p->error = 1;
        p->depth--;
        return NULL;
    }

    json_value_t *v = new_value(JSON_OBJECT);
    size_t cap = 4;
    v->u.object.keys = malloc(sizeof(char *) * cap);
    v->u.object.values = malloc(sizeof(json_value_t *) * cap);
    v->u.object.count = 0;

    skip_ws(p);
    if (peek(p) == '}') {
        p->pos++;
        p->depth--;
        return v;
    }

    for (;;) {
        skip_ws(p);
        if (peek(p) != '"') {
            p->error = 1;
            p->depth--;
            json_free(v);
            return NULL;
        }
        char *key = parse_string(p);
        if (!key || p->error) {
            free(key);
            p->depth--;
            json_free(v);
            return NULL;
        }

        skip_ws(p);
        if (peek(p) != ':') {
            free(key);
            p->error = 1;
            p->depth--;
            json_free(v);
            return NULL;
        }
        p->pos++;
        skip_ws(p);

        json_value_t *val = parse_value(p);
        if (!val || p->error) {
            free(key);
            p->depth--;
            json_free(v);
            return NULL;
        }

        if (v->u.object.count == cap) {
            cap *= 2;
            v->u.object.keys = realloc(v->u.object.keys, sizeof(char *) * cap);
            v->u.object.values = realloc(v->u.object.values, sizeof(json_value_t *) * cap);
        }
        v->u.object.keys[v->u.object.count] = key;
        v->u.object.values[v->u.object.count] = val;
        v->u.object.count++;

        skip_ws(p);
        int c = peek(p);
        if (c == ',') {
            p->pos++;
            continue;
        }
        if (c == '}') {
            p->pos++;
            break;
        }
        p->error = 1;
        p->depth--;
        json_free(v);
        return NULL;
    }

    p->depth--;
    return v;
}

static json_value_t *parse_value(parser_t *p)
{
    skip_ws(p);
    int c = peek(p);

    if (c == '{') return parse_object(p);
    if (c == '[') return parse_array(p);
    if (c == '"') {
        char *s = parse_string(p);
        if (!s || p->error) {
            free(s);
            return NULL;
        }
        json_value_t *v = new_value(JSON_STRING);
        v->u.string = s;
        return v;
    }
    if (c == 't') {
        if (match_lit(p, "true")) {
            json_value_t *v = new_value(JSON_BOOL);
            v->u.boolean = 1;
            return v;
        }
        p->error = 1;
        return NULL;
    }
    if (c == 'f') {
        if (match_lit(p, "false")) {
            json_value_t *v = new_value(JSON_BOOL);
            v->u.boolean = 0;
            return v;
        }
        p->error = 1;
        return NULL;
    }
    if (c == 'n') {
        if (match_lit(p, "null")) {
            return new_value(JSON_NULL);
        }
        p->error = 1;
        return NULL;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return parse_number(p);
    }

    p->error = 1;
    return NULL;
}

json_value_t *json_parse(const char *text, size_t len, int max_depth)
{
    if (!text) {
        return NULL;
    }

    parser_t p;
    p.s = text;
    p.len = len;
    p.pos = 0;
    p.depth = 0;
    p.max_depth = max_depth > 0 ? max_depth : 32;
    p.error = 0;

    json_value_t *v = parse_value(&p);
    if (!v || p.error) {
        json_free(v);
        return NULL;
    }

    skip_ws(&p);
    if (p.pos != p.len) {
        json_free(v); /* trailing garbage after the top-level value */
        return NULL;
    }

    return v;
}

void json_free(json_value_t *v)
{
    if (!v) {
        return;
    }

    switch (v->type) {
        case JSON_STRING:
            free(v->u.string);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->u.array.count; i++) {
                json_free(v->u.array.items[i]);
            }
            free(v->u.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->u.object.count; i++) {
                free(v->u.object.keys[i]);
                json_free(v->u.object.values[i]);
            }
            free(v->u.object.keys);
            free(v->u.object.values);
            break;
        default:
            break;
    }

    free(v);
}

int json_is_object(const json_value_t *v) { return v && v->type == JSON_OBJECT; }
int json_is_array(const json_value_t *v)  { return v && v->type == JSON_ARRAY; }
int json_is_string(const json_value_t *v) { return v && v->type == JSON_STRING; }
int json_is_number(const json_value_t *v) { return v && v->type == JSON_NUMBER; }

json_value_t *json_object_get(const json_value_t *obj, const char *key)
{
    if (!json_is_object(obj)) {
        return NULL;
    }
    for (size_t i = 0; i < obj->u.object.count; i++) {
        if (strcmp(obj->u.object.keys[i], key) == 0) {
            return obj->u.object.values[i];
        }
    }
    return NULL;
}

size_t json_array_size(const json_value_t *v)
{
    return json_is_array(v) ? v->u.array.count : 0;
}

json_value_t *json_array_get(const json_value_t *v, size_t idx)
{
    if (!json_is_array(v) || idx >= v->u.array.count) {
        return NULL;
    }
    return v->u.array.items[idx];
}

const char *json_get_string(const json_value_t *obj, const char *key, const char *def)
{
    json_value_t *v = json_object_get(obj, key);
    return (v && v->type == JSON_STRING) ? v->u.string : def;
}

int json_get_number(const json_value_t *obj, const char *key, double *out)
{
    json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) {
        *out = v->u.number;
        return 1;
    }
    return 0;
}

int json_get_int(const json_value_t *obj, const char *key, long *out)
{
    json_value_t *v = json_object_get(obj, key);
    if (v && v->type == JSON_NUMBER) {
        *out = (long) v->u.number;
        return 1;
    }
    return 0;
}

int json_get_bool(const json_value_t *obj, const char *key, int def)
{
    json_value_t *v = json_object_get(obj, key);
    if (!v) {
        return def;
    }
    switch (v->type) {
        case JSON_BOOL:   return v->u.boolean;
        case JSON_NUMBER: return v->u.number != 0;
        case JSON_STRING: return v->u.string[0] != '\0';
        case JSON_NULL:   return 0;
        default:          return 1; /* arrays/objects are truthy, PHP-style */
    }
}
