#include "lsp_json.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Parser ---- */

typedef struct {
    Arena* arena;
    const char* src;
    size_t len;
    size_t pos;
} JsonParser;

static void jp_skip_ws(JsonParser* p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            p->pos++;
        else
            break;
    }
}

static char jp_peek(JsonParser* p) {
    jp_skip_ws(p);
    return p->pos < p->len ? p->src[p->pos] : '\0';
}

static char jp_next(JsonParser* p) {
    jp_skip_ws(p);
    return p->pos < p->len ? p->src[p->pos++] : '\0';
}

static bool jp_match(JsonParser* p, const char* str) {
    size_t slen = strlen(str);
    if (p->pos + slen > p->len) return false;
    if (memcmp(p->src + p->pos, str, slen) != 0) return false;
    p->pos += slen;
    return true;
}

static JsonValue* jp_alloc(JsonParser* p, JsonType type) {
    JsonValue* v = arena_alloc(p->arena, sizeof(JsonValue));
    memset(v, 0, sizeof(JsonValue));
    v->type = type;
    return v;
}

static JsonValue* jp_parse_value(JsonParser* p);

static JsonValue* jp_parse_string(JsonParser* p) {
    if (jp_next(p) != '"') return NULL;

    size_t start = p->pos;
    // estimate: output <= input length
    size_t cap = 64;
    char* buf = arena_alloc(p->arena, cap);
    size_t len = 0;

    while (p->pos < p->len && p->src[p->pos] != '"') {
        char c = p->src[p->pos++];
        if (c == '\\' && p->pos < p->len) {
            char esc = p->src[p->pos++];
            switch (esc) {
            case '"':  c = '"'; break;
            case '\\': c = '\\'; break;
            case '/':  c = '/'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            default:   c = esc; break;
            }
        }
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char* new_buf = arena_alloc(p->arena, new_cap);
            memcpy(new_buf, buf, len);
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = c;
    }
    if (p->pos < p->len) p->pos++; // skip closing "

    buf[len] = '\0';
    JsonValue* v = jp_alloc(p, JSON_STRING);
    v->as.string.str = buf;
    v->as.string.len = len;
    return v;
}

static JsonValue* jp_parse_number(JsonParser* p) {
    const char* start = p->src + p->pos;
    char* end;
    double num = strtod(start, &end);
    p->pos += (size_t)(end - start);
    JsonValue* v = jp_alloc(p, JSON_NUMBER);
    v->as.number = num;
    return v;
}

static JsonValue* jp_parse_object(JsonParser* p) {
    jp_next(p); // skip {
    JsonValue* v = jp_alloc(p, JSON_OBJECT);
    size_t cap = 8;
    v->as.object.pairs = arena_alloc(p->arena, sizeof(JsonPair) * cap);
    v->as.object.count = 0;
    v->as.object.cap = cap;

    if (jp_peek(p) == '}') { jp_next(p); return v; }

    for (;;) {
        JsonValue* key = jp_parse_string(p);
        if (!key) return NULL;
        jp_next(p); // skip :
        JsonValue* val = jp_parse_value(p);
        if (!val) return NULL;

        if (v->as.object.count >= v->as.object.cap) {
            size_t new_cap = v->as.object.cap * 2;
            JsonPair* new_pairs = arena_alloc(p->arena, sizeof(JsonPair) * new_cap);
            memcpy(new_pairs, v->as.object.pairs, sizeof(JsonPair) * v->as.object.count);
            v->as.object.pairs = new_pairs;
            v->as.object.cap = new_cap;
        }
        JsonPair* pair = &v->as.object.pairs[v->as.object.count++];
        pair->key = key->as.string.str;
        pair->key_len = key->as.string.len;
        pair->value = val;

        char c = jp_peek(p);
        if (c == ',') { jp_next(p); continue; }
        if (c == '}') { jp_next(p); break; }
        return NULL;
    }
    return v;
}

static JsonValue* jp_parse_array(JsonParser* p) {
    jp_next(p); // skip [
    JsonValue* v = jp_alloc(p, JSON_ARRAY);
    size_t cap = 8;
    v->as.array.items = arena_alloc(p->arena, sizeof(JsonValue*) * cap);
    v->as.array.count = 0;
    v->as.array.cap = cap;

    if (jp_peek(p) == ']') { jp_next(p); return v; }

    for (;;) {
        JsonValue* item = jp_parse_value(p);
        if (!item) return NULL;

        if (v->as.array.count >= v->as.array.cap) {
            size_t new_cap = v->as.array.cap * 2;
            JsonValue** new_items = arena_alloc(p->arena, sizeof(JsonValue*) * new_cap);
            memcpy(new_items, v->as.array.items, sizeof(JsonValue*) * v->as.array.count);
            v->as.array.items = new_items;
            v->as.array.cap = new_cap;
        }
        v->as.array.items[v->as.array.count++] = item;

        char c = jp_peek(p);
        if (c == ',') { jp_next(p); continue; }
        if (c == ']') { jp_next(p); break; }
        return NULL;
    }
    return v;
}

static JsonValue* jp_parse_value(JsonParser* p) {
    char c = jp_peek(p);
    if (c == '"') return jp_parse_string(p);
    if (c == '{') return jp_parse_object(p);
    if (c == '[') return jp_parse_array(p);
    if (c == '-' || (c >= '0' && c <= '9')) return jp_parse_number(p);
    if (jp_match(p, "true"))  { JsonValue* v = jp_alloc(p, JSON_BOOL); v->as.boolean = true; return v; }
    if (jp_match(p, "false")) { JsonValue* v = jp_alloc(p, JSON_BOOL); v->as.boolean = false; return v; }
    if (jp_match(p, "null"))  { return jp_alloc(p, JSON_NULL); }
    return NULL;
}

JsonValue* json_parse(Arena* arena, const char* input, size_t len) {
    JsonParser p = { .arena = arena, .src = input, .len = len, .pos = 0 };
    return jp_parse_value(&p);
}

/* ---- Accessors ---- */

JsonValue* json_get(JsonValue* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    size_t klen = strlen(key);
    for (size_t i = 0; i < obj->as.object.count; i++) {
        JsonPair* p = &obj->as.object.pairs[i];
        if (p->key_len == klen && memcmp(p->key, key, klen) == 0)
            return p->value;
    }
    return NULL;
}

const char* json_get_string(JsonValue* obj, const char* key) {
    JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_STRING) return NULL;
    return v->as.string.str;
}

int json_get_int(JsonValue* obj, const char* key) {
    JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return 0;
    return (int)v->as.number;
}

bool json_get_bool(JsonValue* obj, const char* key) {
    JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_BOOL) return false;
    return v->as.boolean;
}

/* ---- Writer ---- */

static void jw_ensure(JsonWriter* jw, size_t need) {
    while (jw->len + need >= jw->cap) {
        jw->cap *= 2;
    }
    // caller must provide enough initial buffer; we cannot realloc arena memory.
    // In practice, LSP messages are small enough for the initial buffer.
}

static void jw_put(JsonWriter* jw, const char* s, size_t n) {
    if (jw->len + n < jw->cap) {
        memcpy(jw->buf + jw->len, s, n);
        jw->len += n;
    }
}

static void jw_putc(JsonWriter* jw, char c) {
    if (jw->len + 1 < jw->cap) {
        jw->buf[jw->len++] = c;
    }
}

static void jw_comma(JsonWriter* jw) {
    if (jw->need_comma) jw_putc(jw, ',');
    jw->need_comma = false;
}

void jw_init(JsonWriter* jw, char* buf, size_t cap) {
    jw->buf = buf;
    jw->len = 0;
    jw->cap = cap;
    jw->need_comma = false;
}

char* jw_finish(JsonWriter* jw, size_t* out_len) {
    jw->buf[jw->len] = '\0';
    if (out_len) *out_len = jw->len;
    return jw->buf;
}

void jw_object_start(JsonWriter* jw) {
    jw_comma(jw);
    jw_putc(jw, '{');
    jw->need_comma = false;
}

void jw_object_end(JsonWriter* jw) {
    jw_putc(jw, '}');
    jw->need_comma = true;
}

void jw_array_start(JsonWriter* jw) {
    jw_comma(jw);
    jw_putc(jw, '[');
    jw->need_comma = false;
}

void jw_array_end(JsonWriter* jw) {
    jw_putc(jw, ']');
    jw->need_comma = true;
}

void jw_key(JsonWriter* jw, const char* key) {
    jw_comma(jw);
    jw_putc(jw, '"');
    jw_put(jw, key, strlen(key));
    jw_putc(jw, '"');
    jw_putc(jw, ':');
    jw->need_comma = false;
}

void jw_string(JsonWriter* jw, const char* str) {
    jw_comma(jw);
    jw_putc(jw, '"');
    for (const char* p = str; *p; p++) {
        switch (*p) {
        case '"':  jw_put(jw, "\\\"", 2); break;
        case '\\': jw_put(jw, "\\\\", 2); break;
        case '\n': jw_put(jw, "\\n", 2); break;
        case '\r': jw_put(jw, "\\r", 2); break;
        case '\t': jw_put(jw, "\\t", 2); break;
        default:   jw_putc(jw, *p); break;
        }
    }
    jw_putc(jw, '"');
    jw->need_comma = true;
}

void jw_int(JsonWriter* jw, int value) {
    jw_comma(jw);
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%d", value);
    jw_put(jw, tmp, (size_t)n);
    jw->need_comma = true;
}

void jw_bool(JsonWriter* jw, bool value) {
    jw_comma(jw);
    if (value) jw_put(jw, "true", 4);
    else       jw_put(jw, "false", 5);
    jw->need_comma = true;
}

void jw_null(JsonWriter* jw) {
    jw_comma(jw);
    jw_put(jw, "null", 4);
    jw->need_comma = true;
}

void jw_raw(JsonWriter* jw, const char* raw, size_t len) {
    jw_comma(jw);
    jw_put(jw, raw, len);
    jw->need_comma = true;
}
