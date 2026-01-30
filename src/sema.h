#ifndef ANCC_SEMA_H
#define ANCC_SEMA_H

#include "arena.h"
#include "error.h"
#include "ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Module Module;

typedef enum SymbolKind {
    SYMBOL_FUNC,
    SYMBOL_STRUCT,
    SYMBOL_INTERFACE,
    SYMBOL_CONST,
    SYMBOL_VAR,
    SYMBOL_IMPORT,
} SymbolKind;

typedef struct Symbol {
    struct Symbol* next;
    SymbolKind kind;
    char* name;
    size_t name_size;
    bool is_export;
    Node* node;
    Module* source;
} Symbol;

typedef struct SymbolTable {
    Symbol* first;
    Symbol* last;
    int count;
} SymbolTable;

typedef struct ModuleGraph ModuleGraph;

void sema_collect_symbols(Arena* arena, Errors* errors, ModuleGraph* graph);

#endif
