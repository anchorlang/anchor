#ifndef ANCC_MODULE_H
#define ANCC_MODULE_H

#include "arena.h"
#include "error.h"
#include "ast.h"

#include <stdbool.h>

typedef struct Module {
    char* name;
    char* path;
    Node* ast;
} Module;

typedef struct ModuleGraph {
    Arena* arena;
    Errors* errors;
    char* src_dir;
    Module* modules;
    int count;
    int capacity;
} ModuleGraph;

void module_graph_init(ModuleGraph* graph, Arena* arena, Errors* errors, char* src_dir);
Module* module_resolve(ModuleGraph* graph, char* module_path, size_t module_path_size);
Module* module_find(ModuleGraph* graph, char* path);

#endif
