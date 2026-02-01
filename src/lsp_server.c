#include "lsp_server.h"
#include "lsp_json.h"
#include "lsp_transport.h"
#include "lsp_analysis.h"
#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MSG_BUF_SIZE (64 * 1024)

static char* heap_strdup(const char* s) {
    size_t len = strlen(s);
    char* copy = malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

/* ---- Document store ---- */

typedef struct LspDocument {
    struct LspDocument* next;
    char* uri;
    char* path;       // resolved file path
    char* content;    // current buffer (heap-allocated, replaced on change)
    size_t content_len;
} LspDocument;

typedef struct LspServer {
    Arena msg_arena;
    Arena analysis_arena;
    LspDocument* documents;
    char* root_dir;
    bool initialized;
    bool shutdown;
} LspServer;

/* ---- URI to file path conversion ---- */

static char* uri_to_path(Arena* arena, const char* uri) {
    // file:///C:/foo/bar.anc -> C:/foo/bar.anc  (Windows)
    // file:///home/foo.anc   -> /home/foo.anc   (Unix)
    const char* p = uri;
    if (strncmp(p, "file:///", 8) == 0) {
        p += 7; // keep one leading /
#ifdef _WIN32
        p += 1; // skip the leading / before drive letter on Windows
#endif
    }

    size_t len = strlen(p);
    char* path = arena_alloc(arena, len + 1);

    // Decode percent-encoded characters and normalize slashes
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] == '%' && i + 2 < len) {
            char hex[3] = { p[i+1], p[i+2], '\0' };
            path[j++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (p[i] == '/') {
            path[j++] = '/';
        } else {
            path[j++] = p[i];
        }
    }
    path[j] = '\0';
    return path;
}

/* Extract src_dir and module_name from a file path.
   e.g. "C:/project/src/utils.anc" -> src_dir="C:/project/src", stem="utils" */
static void path_split(const char* path, char* src_dir, size_t src_dir_cap,
                       char* stem, size_t stem_cap) {
    const char* last_slash = NULL;
    for (const char* p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last_slash = p;
    }

    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - path);
        if (dir_len >= src_dir_cap) dir_len = src_dir_cap - 1;
        memcpy(src_dir, path, dir_len);
        src_dir[dir_len] = '\0';
    } else {
        src_dir[0] = '.';
        src_dir[1] = '\0';
        last_slash = path - 1;
    }

    const char* filename = last_slash + 1;
    const char* dot = strrchr(filename, '.');
    size_t stem_len = dot ? (size_t)(dot - filename) : strlen(filename);
    if (stem_len >= stem_cap) stem_len = stem_cap - 1;
    memcpy(stem, filename, stem_len);
    stem[stem_len] = '\0';
}

/* ---- Document management ---- */

static LspDocument* doc_find(LspServer* server, const char* uri) {
    for (LspDocument* d = server->documents; d; d = d->next) {
        if (strcmp(d->uri, uri) == 0) return d;
    }
    return NULL;
}

static LspDocument* doc_open(LspServer* server, const char* uri, const char* text, size_t text_len) {
    LspDocument* doc = calloc(1, sizeof(LspDocument));
    doc->uri = heap_strdup(uri);
    doc->path = uri_to_path(&server->msg_arena, uri);
    // Copy path to heap so it survives arena reset
    doc->path = heap_strdup(doc->path);
    doc->content = malloc(text_len + 1);
    memcpy(doc->content, text, text_len);
    doc->content[text_len] = '\0';
    doc->content_len = text_len;
    doc->next = server->documents;
    server->documents = doc;
    return doc;
}

static void doc_update(LspDocument* doc, const char* text, size_t text_len) {
    free(doc->content);
    doc->content = malloc(text_len + 1);
    memcpy(doc->content, text, text_len);
    doc->content[text_len] = '\0';
    doc->content_len = text_len;
}

static void doc_close(LspServer* server, const char* uri) {
    LspDocument** pp = &server->documents;
    while (*pp) {
        LspDocument* d = *pp;
        if (strcmp(d->uri, uri) == 0) {
            *pp = d->next;
            free(d->uri);
            free(d->path);
            free(d->content);
            free(d);
            return;
        }
        pp = &d->next;
    }
}

/* ---- Response helpers ---- */

static void send_response(Arena* arena, JsonValue* id, const char* result_body, size_t result_len) {
    char* buf = arena_alloc(arena, MSG_BUF_SIZE);
    JsonWriter jw;
    jw_init(&jw, buf, MSG_BUF_SIZE);

    jw_object_start(&jw);
    jw_key(&jw, "jsonrpc"); jw_string(&jw, "2.0");

    jw_key(&jw, "id");
    if (id && id->type == JSON_NUMBER) jw_int(&jw, (int)id->as.number);
    else if (id && id->type == JSON_STRING) jw_string(&jw, id->as.string.str);
    else jw_null(&jw);

    jw_key(&jw, "result");
    if (result_body) jw_raw(&jw, result_body, result_len);
    else jw_null(&jw);

    jw_object_end(&jw);

    size_t len;
    jw_finish(&jw, &len);
    lsp_transport_write(buf, len);
}

static void send_notification(Arena* arena, const char* method,
                              const char* params_body, size_t params_len) {
    char* buf = arena_alloc(arena, MSG_BUF_SIZE);
    JsonWriter jw;
    jw_init(&jw, buf, MSG_BUF_SIZE);

    jw_object_start(&jw);
    jw_key(&jw, "jsonrpc"); jw_string(&jw, "2.0");
    jw_key(&jw, "method"); jw_string(&jw, method);
    jw_key(&jw, "params");
    jw_raw(&jw, params_body, params_len);
    jw_object_end(&jw);

    size_t len;
    jw_finish(&jw, &len);
    lsp_transport_write(buf, len);
}

/* ---- Analysis + publish diagnostics ---- */

static void analyze_and_publish(LspServer* server, LspDocument* doc) {
    arena_reset(&server->analysis_arena);

    char src_dir[1024];
    char stem[256];
    path_split(doc->path, src_dir, sizeof(src_dir), stem, sizeof(stem));

    LspAnalysisResult result = lsp_analyze(
        &server->analysis_arena, src_dir,
        doc->path, doc->content, doc->content_len,
        stem, strlen(stem));

    // Build diagnostics JSON array
    char* diag_buf = arena_alloc(&server->msg_arena, MSG_BUF_SIZE);
    JsonWriter djw;
    jw_init(&djw, diag_buf, MSG_BUF_SIZE);
    lsp_errors_to_diagnostics(&djw, &result.errors);
    size_t diag_len;
    jw_finish(&djw, &diag_len);

    // Build publishDiagnostics params
    char* params_buf = arena_alloc(&server->msg_arena, MSG_BUF_SIZE);
    JsonWriter pjw;
    jw_init(&pjw, params_buf, MSG_BUF_SIZE);
    jw_object_start(&pjw);
    jw_key(&pjw, "uri"); jw_string(&pjw, doc->uri);
    jw_key(&pjw, "diagnostics"); jw_raw(&pjw, diag_buf, diag_len);
    jw_object_end(&pjw);
    size_t params_len;
    jw_finish(&pjw, &params_len);

    send_notification(&server->msg_arena, "textDocument/publishDiagnostics",
                      params_buf, params_len);
}

/* ---- Handlers ---- */

static void handle_initialize(LspServer* server, JsonValue* id, JsonValue* params) {
    (void)params;

    char* buf = arena_alloc(&server->msg_arena, MSG_BUF_SIZE);
    JsonWriter jw;
    jw_init(&jw, buf, MSG_BUF_SIZE);

    jw_object_start(&jw);
        jw_key(&jw, "capabilities");
        jw_object_start(&jw);
            jw_key(&jw, "textDocumentSync"); jw_int(&jw, 1); // full sync
            jw_key(&jw, "hoverProvider"); jw_bool(&jw, true);
            jw_key(&jw, "definitionProvider"); jw_bool(&jw, true);
        jw_object_end(&jw);

        jw_key(&jw, "serverInfo");
        jw_object_start(&jw);
            jw_key(&jw, "name"); jw_string(&jw, "ancc");
            jw_key(&jw, "version"); jw_string(&jw, "0.1.0");
        jw_object_end(&jw);
    jw_object_end(&jw);

    size_t len;
    jw_finish(&jw, &len);
    send_response(&server->msg_arena, id, buf, len);

    server->initialized = true;
}

static void handle_shutdown(LspServer* server, JsonValue* id) {
    server->shutdown = true;
    send_response(&server->msg_arena, id, NULL, 0);
}

static void handle_did_open(LspServer* server, JsonValue* params) {
    JsonValue* td = json_get(params, "textDocument");
    if (!td) return;

    const char* uri = json_get_string(td, "uri");
    JsonValue* text_val = json_get(td, "text");
    if (!uri || !text_val || text_val->type != JSON_STRING) return;

    LspDocument* doc = doc_open(server, uri,
                                text_val->as.string.str,
                                text_val->as.string.len);
    analyze_and_publish(server, doc);
}

static void handle_did_change(LspServer* server, JsonValue* params) {
    JsonValue* td = json_get(params, "textDocument");
    if (!td) return;

    const char* uri = json_get_string(td, "uri");
    if (!uri) return;

    LspDocument* doc = doc_find(server, uri);
    if (!doc) return;

    JsonValue* changes = json_get(params, "contentChanges");
    if (!changes || changes->type != JSON_ARRAY || changes->as.array.count == 0) return;

    // Full sync: take the last content change (entire document)
    JsonValue* last = changes->as.array.items[changes->as.array.count - 1];
    JsonValue* text_val = json_get(last, "text");
    if (!text_val || text_val->type != JSON_STRING) return;

    doc_update(doc, text_val->as.string.str, text_val->as.string.len);
    analyze_and_publish(server, doc);
}

static void handle_did_close(LspServer* server, JsonValue* params) {
    JsonValue* td = json_get(params, "textDocument");
    if (!td) return;
    const char* uri = json_get_string(td, "uri");
    if (!uri) return;

    // Publish empty diagnostics to clear them
    char* params_buf = arena_alloc(&server->msg_arena, MSG_BUF_SIZE);
    JsonWriter pjw;
    jw_init(&pjw, params_buf, MSG_BUF_SIZE);
    jw_object_start(&pjw);
    jw_key(&pjw, "uri"); jw_string(&pjw, uri);
    jw_key(&pjw, "diagnostics"); jw_array_start(&pjw); jw_array_end(&pjw);
    jw_object_end(&pjw);
    size_t params_len;
    jw_finish(&pjw, &params_len);
    send_notification(&server->msg_arena, "textDocument/publishDiagnostics",
                      params_buf, params_len);

    doc_close(server, uri);
}

/* ---- Main loop ---- */

void lsp_server_run(char* dir) {
    lsp_transport_init();

    LspServer server;
    memset(&server, 0, sizeof(server));
    arena_init(&server.msg_arena, 1 * 1024 * 1024);
    arena_init(&server.analysis_arena, 16 * 1024 * 1024);
    server.root_dir = dir;

    for (;;) {
        arena_reset(&server.msg_arena);

        size_t msg_len;
        char* msg = lsp_transport_read(&msg_len);
        if (!msg) break;

        JsonValue* root = json_parse(&server.msg_arena, msg, msg_len);
        free(msg);

        if (!root) continue;

        const char* method = json_get_string(root, "method");
        JsonValue* id = json_get(root, "id");
        JsonValue* params = json_get(root, "params");

        if (!method) continue;

        if (strcmp(method, "initialize") == 0) {
            handle_initialize(&server, id, params);
        } else if (strcmp(method, "initialized") == 0) {
            // no-op
        } else if (strcmp(method, "shutdown") == 0) {
            handle_shutdown(&server, id);
        } else if (strcmp(method, "exit") == 0) {
            break;
        } else if (strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(&server, params);
        } else if (strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(&server, params);
        } else if (strcmp(method, "textDocument/didClose") == 0) {
            handle_did_close(&server, params);
        }
        // Unknown methods are silently ignored (per LSP spec for notifications)
    }

    // Cleanup
    while (server.documents) {
        LspDocument* d = server.documents;
        server.documents = d->next;
        free(d->uri);
        free(d->path);
        free(d->content);
        free(d);
    }
    arena_free(&server.msg_arena);
    arena_free(&server.analysis_arena);

    exit(server.shutdown ? 0 : 1);
}
