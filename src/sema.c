#include "sema.h"
#include "module.h"
#include "fs.h"

#include <string.h>
#include <stdio.h>

static Symbol* symbol_add(Arena* arena, SymbolTable* table, SymbolKind kind,
                           char* name, size_t name_size, bool is_export, Node* node) {
    Symbol* sym = arena_alloc(arena, sizeof(Symbol));
    sym->next = NULL;
    sym->kind = kind;
    sym->name = name;
    sym->name_size = name_size;
    sym->is_export = is_export;
    sym->node = node;
    sym->source = NULL;
    if (!table->first) {
        table->first = sym;
    } else {
        table->last->next = sym;
    }
    table->last = sym;
    table->count++;
    return sym;
}

static Symbol* symbol_find(SymbolTable* table, char* name, size_t name_size) {
    for (Symbol* s = table->first; s; s = s->next) {
        if (s->name_size == name_size && memcmp(s->name, name, name_size) == 0) {
            return s;
        }
    }
    return NULL;
}

static char* build_import_file_path(Arena* arena, char* src_dir, char* module_path, size_t module_path_size) {
    size_t src_dir_len = strlen(src_dir);
    size_t max_len = src_dir_len + 1 + module_path_size + 4 + 1;
    char* path = arena_alloc(arena, max_len);

    size_t pos = 0;
    memcpy(path + pos, src_dir, src_dir_len);
    pos += src_dir_len;
    path[pos++] = '/';

    for (size_t i = 0; i < module_path_size; i++) {
        path[pos++] = module_path[i] == '.' ? '/' : module_path[i];
    }

    memcpy(path + pos, ".anc", 4);
    pos += 4;
    path[pos] = '\0';
    return path;
}

static void collect_module_symbols(Arena* arena, Errors* errors, Module* mod) {
    SymbolTable* table = arena_alloc(arena, sizeof(SymbolTable));
    table->first = NULL;
    table->last = NULL;
    table->count = 0;
    mod->symbols = table;

    if (!mod->ast || mod->ast->type != NODE_PROGRAM) return;

    NodeList* decls = &mod->ast->as.program.declarations;
    for (size_t i = 0; i < decls->count; i++) {
        Node* node = decls->nodes[i];
        char* name = NULL;
        size_t name_size = 0;
        bool is_export = false;
        SymbolKind kind;

        switch (node->type) {
        case NODE_FUNC_DECL:
            kind = SYMBOL_FUNC;
            name = node->as.func_decl.name;
            name_size = node->as.func_decl.name_size;
            is_export = node->as.func_decl.is_export;
            break;
        case NODE_STRUCT_DECL:
            kind = SYMBOL_STRUCT;
            name = node->as.struct_decl.name;
            name_size = node->as.struct_decl.name_size;
            is_export = node->as.struct_decl.is_export;
            break;
        case NODE_INTERFACE_DECL:
            kind = SYMBOL_INTERFACE;
            name = node->as.interface_decl.name;
            name_size = node->as.interface_decl.name_size;
            is_export = false;
            break;
        case NODE_CONST_DECL:
            kind = SYMBOL_CONST;
            name = node->as.const_decl.name;
            name_size = node->as.const_decl.name_size;
            is_export = node->as.const_decl.is_export;
            break;
        case NODE_VAR_DECL:
            kind = SYMBOL_VAR;
            name = node->as.var_decl.name;
            name_size = node->as.var_decl.name_size;
            is_export = node->as.var_decl.is_export;
            break;
        default:
            continue;
        }

        // duplicate check
        Symbol* existing = symbol_find(table, name, name_size);
        if (existing) {
            errors_push(errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "duplicate symbol '%.*s' in module '%s'", (int)name_size, name, mod->name);
            continue;
        }

        symbol_add(arena, table, kind, name, name_size, is_export, node);
    }
}

static void resolve_module_imports(Arena* arena, Errors* errors, ModuleGraph* graph, Module* mod) {
    if (!mod->ast || mod->ast->type != NODE_PROGRAM) return;

    NodeList* decls = &mod->ast->as.program.declarations;
    for (size_t i = 0; i < decls->count; i++) {
        Node* node = decls->nodes[i];
        if (node->type != NODE_IMPORT_DECL) continue;

        char* module_path = node->as.import_decl.module_path;
        size_t module_path_size = node->as.import_decl.module_path_size;
        bool is_export = node->as.import_decl.is_export;

        // find the source module
        char* file_path = build_import_file_path(arena, graph->src_dir, module_path, module_path_size);
        Module* source = module_find(graph, file_path);
        if (!source) {
            errors_push(errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "module '%.*s' not found", (int)module_path_size, module_path);
            continue;
        }

        // resolve each imported name
        ImportNameList* names = &node->as.import_decl.names;
        for (size_t j = 0; j < names->count; j++) {
            ImportName* imp = &names->names[j];

            // check for duplicate in current module
            Symbol* dup = symbol_find(mod->symbols, imp->name, imp->name_size);
            if (dup) {
                errors_push(errors, SEVERITY_ERROR, imp->offset, imp->line, imp->column,
                            "duplicate symbol '%.*s' in module '%s'",
                            (int)imp->name_size, imp->name, mod->name);
                continue;
            }

            // find in source module
            Symbol* src_sym = symbol_find(source->symbols, imp->name, imp->name_size);
            if (!src_sym) {
                errors_push(errors, SEVERITY_ERROR, imp->offset, imp->line, imp->column,
                            "'%.*s' not found in module '%s'",
                            (int)imp->name_size, imp->name, source->name);
                continue;
            }

            if (!src_sym->is_export) {
                errors_push(errors, SEVERITY_ERROR, imp->offset, imp->line, imp->column,
                            "'%.*s' is not exported from module '%s'",
                            (int)imp->name_size, imp->name, source->name);
                continue;
            }

            Symbol* sym = symbol_add(arena, mod->symbols, SYMBOL_IMPORT,
                                      imp->name, imp->name_size, is_export, src_sym->node);
            sym->source = source;
        }
    }
}

void sema_collect_symbols(Arena* arena, Errors* errors, ModuleGraph* graph) {
    // pass 1: collect local declarations
    for (Module* m = graph->first; m; m = m->next) {
        collect_module_symbols(arena, errors, m);
    }

    // pass 2: resolve imports
    for (Module* m = graph->first; m; m = m->next) {
        resolve_module_imports(arena, errors, graph, m);
    }
}
