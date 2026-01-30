#include "module.h"
#include "fs.h"
#include "lexer.h"
#include "parser.h"

#include <stdio.h>
#include <string.h>

void module_graph_init(ModuleGraph* graph, Arena* arena, Errors* errors, char* src_dir) {
    graph->arena = arena;
    graph->errors = errors;
    graph->src_dir = src_dir;
    graph->first = NULL;
    graph->last = NULL;
    graph->count = 0;
}

Module* module_find(ModuleGraph* graph, char* path) {
    for (Module* m = graph->first; m; m = m->next) {
        if (strcmp(m->path, path) == 0) {
            return m;
        }
    }
    return NULL;
}

static Module* module_graph_add(ModuleGraph* graph) {
    Module* m = arena_alloc(graph->arena, sizeof(Module));
    m->next = NULL;
    m->name = NULL;
    m->path = NULL;
    m->ast = NULL;
    m->symbols = NULL;
    m->impl_pairs.pairs = NULL;
    m->impl_pairs.count = 0;
    m->impl_pairs.capacity = 0;
    if (!graph->first) {
        graph->first = m;
    } else {
        graph->last->next = m;
    }
    graph->last = m;
    graph->count++;
    return m;
}

static char* build_file_path(Arena* arena, char* src_dir, char* module_path, size_t module_path_size) {
    size_t src_dir_len = strlen(src_dir);
    // worst case: src_dir + "/" + module_path + ".anc" + null
    size_t max_len = src_dir_len + 1 + module_path_size + 4 + 1;
    char* path = arena_alloc(arena, max_len);

    size_t pos = 0;
    memcpy(path + pos, src_dir, src_dir_len);
    pos += src_dir_len;
    path[pos++] = '/';

    // copy module_path, replacing dots with slashes
    for (size_t i = 0; i < module_path_size; i++) {
        path[pos++] = module_path[i] == '.' ? '/' : module_path[i];
    }

    memcpy(path + pos, ".anc", 4);
    pos += 4;
    path[pos] = '\0';

    return path;
}

static char* extract_module_name(Arena* arena, char* module_path, size_t module_path_size) {
    // find last dot to get the final segment
    size_t start = 0;
    for (size_t i = 0; i < module_path_size; i++) {
        if (module_path[i] == '.') {
            start = i + 1;
        }
    }
    size_t name_len = module_path_size - start;
    char* name = arena_alloc(arena, name_len + 1);
    memcpy(name, module_path + start, name_len);
    name[name_len] = '\0';
    return name;
}

static void resolve_imports(ModuleGraph* graph, Node* ast) {
    if (ast->type != NODE_PROGRAM) return;
    NodeList* decls = &ast->as.program.declarations;
    for (size_t i = 0; i < decls->count; i++) {
        Node* node = decls->nodes[i];
        if (node->type == NODE_IMPORT_DECL) {
            module_resolve(graph, node->as.import_decl.module_path, node->as.import_decl.module_path_size);
        }
    }
}

Module* module_resolve(ModuleGraph* graph, char* module_path, size_t module_path_size) {
    char* file_path = build_file_path(graph->arena, graph->src_dir, module_path, module_path_size);

    // dedup: already loaded?
    Module* existing = module_find(graph, file_path);
    if (existing) return existing;

    // read file
    size_t source_size;
    char* source = file_read(graph->arena, file_path, &source_size);
    if (!source) {
        errors_push(graph->errors, SEVERITY_ERROR, 0, 0, 0, "cannot open module '%s'", file_path);
        return NULL;
    }

    // lex
    Tokens tokens;
    lexer_tokenize(graph->arena, &tokens, graph->errors, source, source_size);

    // parse
    Node* ast = parser_parse(graph->arena, &tokens, graph->errors);

    // add to graph before resolving imports (handles circular imports)
    Module* module = module_graph_add(graph);
    module->name = extract_module_name(graph->arena, module_path, module_path_size);
    module->path = file_path;
    module->ast = ast;

    // resolve imports recursively
    if (ast) {
        resolve_imports(graph, ast);
    }

    return module;
}
