#ifndef ANCC_MODULE_H
#define ANCC_MODULE_H

#include "arena.h"
#include "error.h"
#include "ast.h"

#include <stdbool.h>

typedef struct SymbolTable SymbolTable;
typedef struct Type Type;

typedef struct GenericInst {
    struct GenericInst* next;
    Node* template_decl;
    Type** type_args;
    size_t type_arg_count;
    char* mangled_name;
    size_t mangled_name_size;
    Node* mono_decl;
    Type* resolved_type;
} GenericInst;

typedef struct GenericInstList {
    GenericInst* first;
    GenericInst* last;
} GenericInstList;

typedef struct ImplPair {
    Type* struct_type;
    Type* interface_type;
    struct Module* struct_module;
} ImplPair;

typedef struct ImplPairList {
    ImplPair* pairs;
    size_t count;
    size_t capacity;
} ImplPairList;

typedef struct Module {
    struct Module* next;
    char* name;
    char* path;
    Node* ast;
    SymbolTable* symbols;
    ImplPairList impl_pairs;
    GenericInstList generic_insts;
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
