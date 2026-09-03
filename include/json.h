#ifndef CTF_JSON_H
#define CTF_JSON_H

#include <stddef.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value {
    json_type_t type;
    union {
        int boolean;
        double number;
        char *string;
        struct {
            struct json_value **items;
            size_t count;
        } array;
        struct {
            char **keys;
            struct json_value **values;
            size_t count;
        } object;
    } u;
} json_value_t;

/* Parses `len` bytes of JSON text. Object/array nesting deeper than
 * `max_depth` is rejected (a lightweight DoS guard, mirroring the depth
 * limit passed to PHP's json_decode()). Returns NULL on any parse error;
 * the caller owns the result and must free it with json_free(). */
json_value_t *json_parse(const char *text, size_t len, int max_depth);

void json_free(json_value_t *v);

/* Accessors. All are NULL/zero-value safe: passing a NULL or non-object/
 * non-array value simply behaves as "not found". */
json_value_t *json_object_get(const json_value_t *obj, const char *key);
int json_is_object(const json_value_t *v);
int json_is_array(const json_value_t *v);
int json_is_string(const json_value_t *v);
int json_is_number(const json_value_t *v);

size_t json_array_size(const json_value_t *v);
json_value_t *json_array_get(const json_value_t *v, size_t idx);

/* Returns the string value of obj[key], or `def` if missing / wrong type. */
const char *json_get_string(const json_value_t *obj, const char *key, const char *def);

/* Returns 1 and stores the numeric value of obj[key] in *out if present and
 * a number; returns 0 (and leaves *out untouched) otherwise. */
int json_get_number(const json_value_t *obj, const char *key, double *out);

/* Returns 1 and stores the integer value of obj[key] in *out if present and
 * an integral number; returns 0 otherwise. */
int json_get_int(const json_value_t *obj, const char *key, long *out);

/* Returns the boolean value of obj[key] (PHP (bool) cast semantics: numbers
 * !=0 / non-empty strings / true are truthy), or `def` if the key is absent. */
int json_get_bool(const json_value_t *obj, const char *key, int def);

#endif
