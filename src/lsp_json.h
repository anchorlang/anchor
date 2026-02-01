#ifndef ANCC_LSP_JSON_H
#define ANCC_LSP_JSON_H

#include "arena.h"
#include <stddef.h>
#include <stdbool.h>

/* ---- JSON Value (parser output) ---- */

typedef enum JsonType {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct JsonPair JsonPair;

typedef struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        struct { char* str; size_t len; } string;
        struct { struct JsonValue** items; size_t count; size_t cap; } array;
        struct { JsonPair* pairs; size_t count; size_t cap; } object;
    } as;
} JsonValue;

struct JsonPair {
    char* key;
    size_t key_len;
    JsonValue* value;
};

JsonValue* json_parse(Arena* arena, const char* input, size_t len);

JsonValue* json_get(JsonValue* obj, const char* key);
const char* json_get_string(JsonValue* obj, const char* key);
int json_get_int(JsonValue* obj, const char* key);
bool json_get_bool(JsonValue* obj, const char* key);

/* ---- JSON Writer ---- */

typedef struct JsonWriter {
    char* buf;
    size_t len;
    size_t cap;
    bool need_comma;
} JsonWriter;

void jw_init(JsonWriter* jw, char* buf, size_t cap);
char* jw_finish(JsonWriter* jw, size_t* out_len);

void jw_object_start(JsonWriter* jw);
void jw_object_end(JsonWriter* jw);
void jw_array_start(JsonWriter* jw);
void jw_array_end(JsonWriter* jw);
void jw_key(JsonWriter* jw, const char* key);
void jw_string(JsonWriter* jw, const char* str);
void jw_int(JsonWriter* jw, int value);
void jw_bool(JsonWriter* jw, bool value);
void jw_null(JsonWriter* jw);
void jw_raw(JsonWriter* jw, const char* raw, size_t len);

#endif
