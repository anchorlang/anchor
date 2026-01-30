#ifndef ANCC_MODULE_H
#define ANCC_MODULE_H

#include "arena.h"
#include "error.h"
#include "ast.h"

#include <stdbool.h>

typedef struct SymbolTable SymbolTable;

typedef struct Module {
    struct Module* next;
    char* name;
    char* path;
    Node* ast;
    SymbolTable* symbols;
} Module;

typedef struct ModuleGraph {
    Arena* arena;
    Errors* errors;
    char* src_dir;
    Module* first;
    Module* last;
    int count;
} ModuleGraph;

void module_graph_init(ModuleGraph* graph, Arena* arena, Errors* errors, char* src_dir);
Module* module_resolve(ModuleGraph* graph, char* module_path, size_t module_path_size);
Module* module_find(ModuleGraph* graph, char* path);

#endif
