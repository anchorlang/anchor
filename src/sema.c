#include "sema.h"
#include "module.h"
#include "type.h"
#include "lexer.h"

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

static Type* resolve_type_node(TypeRegistry* reg, Errors* errors,
                                SymbolTable* table, Node* node) {
    if (!node) return type_void(reg);

    switch (node->type) {
    case NODE_TYPE_SIMPLE: {
        char* name = node->as.type_simple.name;
        size_t size = node->as.type_simple.name_size;
        if (size == 4 && memcmp(name, "void", 4) == 0) return type_void(reg);
        if (size == 4 && memcmp(name, "bool", 4) == 0) return type_bool(reg);
        if (size == 4 && memcmp(name, "byte", 4) == 0) return type_byte(reg);
        if (size == 5 && memcmp(name, "short", 5) == 0) return type_short(reg);
        if (size == 6 && memcmp(name, "ushort", 6) == 0) return type_ushort(reg);
        if (size == 3 && memcmp(name, "int", 3) == 0) return type_int(reg);
        if (size == 4 && memcmp(name, "uint", 4) == 0) return type_uint(reg);
        if (size == 4 && memcmp(name, "long", 4) == 0) return type_long(reg);
        if (size == 5 && memcmp(name, "ulong", 5) == 0) return type_ulong(reg);
        if (size == 5 && memcmp(name, "float", 5) == 0) return type_float(reg);
        if (size == 6 && memcmp(name, "double", 6) == 0) return type_double(reg);
        if (size == 6 && memcmp(name, "string", 6) == 0) return type_string(reg);

        // look up struct/interface in symbol table
        Symbol* sym = symbol_find(table, name, size);
        if (sym && sym->node && sym->node->resolved_type) {
            return (Type*)sym->node->resolved_type;
        }
        // check if it's an import
        if (sym && sym->kind == SYMBOL_IMPORT && sym->source) {
            Symbol* src_sym = symbol_find(sym->source->symbols, name, size);
            if (src_sym && src_sym->node && src_sym->node->resolved_type) {
                return (Type*)src_sym->node->resolved_type;
            }
        }
        errors_push(errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                    "unknown type '%.*s'", (int)size, name);
        return NULL;
    }
    case NODE_TYPE_REFERENCE: {
        Type* inner = resolve_type_node(reg, errors, table, node->as.type_ref.inner);
        if (!inner) return NULL;
        return type_ref(reg, inner);
    }
    case NODE_TYPE_POINTER: {
        Type* inner = resolve_type_node(reg, errors, table, node->as.type_ptr.inner);
        if (!inner) return NULL;
        return type_ptr(reg, inner);
    }
    default:
        return NULL;
    }
}

static void resolve_module_types(Arena* arena, Errors* errors,
                                  TypeRegistry* reg, Module* mod) {
    if (!mod->symbols) return;

    for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
        if (!sym->node) continue;

        switch (sym->kind) {
        case SYMBOL_STRUCT: {
            Type* t = type_struct(reg,
                sym->node->as.struct_decl.name,
                sym->node->as.struct_decl.name_size,
                mod,
                &sym->node->as.struct_decl.fields,
                &sym->node->as.struct_decl.methods);
            sym->node->resolved_type = t;
            break;
        }
        case SYMBOL_INTERFACE: {
            Type* t = type_interface(reg,
                sym->node->as.interface_decl.name,
                sym->node->as.interface_decl.name_size,
                &sym->node->as.interface_decl.method_sigs);
            sym->node->resolved_type = t;
            break;
        }
        default:
            break;
        }
    }
}

static void resolve_func_types(Arena* arena, Errors* errors,
                                TypeRegistry* reg, Module* mod) {
    if (!mod->symbols) return;

    for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_FUNC || !sym->node) continue;

        ParamList* params = &sym->node->as.func_decl.params;
        int param_count = (int)params->count;
        Type** param_types = NULL;
        if (param_count > 0) {
            param_types = arena_alloc(arena, sizeof(Type*) * param_count);
            for (int i = 0; i < param_count; i++) {
                param_types[i] = resolve_type_node(reg, errors, mod->symbols,
                                                    params->params[i].type_node);
            }
        }

        Type* return_type = resolve_type_node(reg, errors, mod->symbols,
                                               sym->node->as.func_decl.return_type);

        Type* func_t = type_func(reg, param_types, param_count, return_type);
        sym->node->resolved_type = func_t;
    }
}

// ---------------------------------------------------------------------------
// Pass 4: Expression & statement type checking
// ---------------------------------------------------------------------------

typedef struct Scope {
    struct Scope* parent;
    SymbolTable locals;
} Scope;

typedef struct CheckContext {
    Arena* arena;
    Errors* errors;
    TypeRegistry* reg;
    Module* mod;
    Scope* scope;
    Type* return_type;
    Type* self_type;  // non-NULL inside struct methods
} CheckContext;

static Scope* scope_push(CheckContext* ctx) {
    Scope* s = arena_alloc(ctx->arena, sizeof(Scope));
    s->parent = ctx->scope;
    s->locals.first = NULL;
    s->locals.last = NULL;
    s->locals.count = 0;
    ctx->scope = s;
    return s;
}

static void scope_pop(CheckContext* ctx, Scope* prev) {
    ctx->scope = prev;
}

static void scope_add(CheckContext* ctx, char* name, size_t name_size, Type* type, Node* node) {
    Symbol* sym = arena_alloc(ctx->arena, sizeof(Symbol));
    sym->next = NULL;
    sym->kind = SYMBOL_VAR;
    sym->name = name;
    sym->name_size = name_size;
    sym->is_export = false;
    sym->node = node;
    sym->source = NULL;
    if (node) node->resolved_type = type;

    SymbolTable* t = &ctx->scope->locals;
    if (!t->first) {
        t->first = sym;
    } else {
        t->last->next = sym;
    }
    t->last = sym;
    t->count++;
}

static Symbol* scope_lookup(CheckContext* ctx, char* name, size_t name_size) {
    for (Scope* s = ctx->scope; s; s = s->parent) {
        Symbol* sym = symbol_find(&s->locals, name, name_size);
        if (sym) return sym;
    }
    // fall back to module symbols
    return symbol_find(ctx->mod->symbols, name, name_size);
}

static Type* get_symbol_type(Symbol* sym) {
    if (!sym || !sym->node) return NULL;
    return (Type*)sym->node->resolved_type;
}

// unwrap &T or *T to get the struct type underneath
static Type* unwrap_to_struct(Type* type) {
    if (!type) return NULL;
    if (type->kind == TYPE_STRUCT) return type;
    if (type->kind == TYPE_REF) return unwrap_to_struct(type->as.ref_type.inner);
    if (type->kind == TYPE_PTR) return unwrap_to_struct(type->as.ptr_type.inner);
    return NULL;
}

static Type* check_expr(CheckContext* ctx, Node* node);
static void check_stmt(CheckContext* ctx, Node* node);
static void check_body(CheckContext* ctx, NodeList* body);

static Type* check_expr(CheckContext* ctx, Node* node) {
    if (!node) return NULL;

    Type* result = NULL;

    switch (node->type) {

    case NODE_INTEGER_LITERAL:
        result = type_int(ctx->reg);
        break;

    case NODE_FLOAT_LITERAL:
        result = type_float(ctx->reg);
        break;

    case NODE_STRING_LITERAL:
        result = type_string(ctx->reg);
        break;

    case NODE_BOOL_LITERAL:
        result = type_bool(ctx->reg);
        break;

    case NODE_NULL_LITERAL:
        result = type_ptr(ctx->reg, type_void(ctx->reg));
        break;

    case NODE_IDENTIFIER: {
        char* name = node->as.identifier.name;
        size_t name_size = node->as.identifier.name_size;
        Symbol* sym = scope_lookup(ctx, name, name_size);
        if (!sym) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "undefined variable '%.*s'", (int)name_size, name);
            break;
        }
        result = get_symbol_type(sym);
        break;
    }

    case NODE_SELF: {
        if (!ctx->self_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "'self' used outside of struct method");
            break;
        }
        result = ctx->self_type;
        break;
    }

    case NODE_BINARY_EXPR: {
        Type* left = check_expr(ctx, node->as.binary_expr.left);
        Type* right = check_expr(ctx, node->as.binary_expr.right);
        if (!left || !right) break;

        TokenType op = node->as.binary_expr.op;

        if (op == TOKEN_AND || op == TOKEN_OR) {
            if (left->kind != TYPE_BOOL) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "left operand of '%s' must be bool, got '%s'",
                            op == TOKEN_AND ? "and" : "or", type_name(left));
                break;
            }
            if (right->kind != TYPE_BOOL) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "right operand of '%s' must be bool, got '%s'",
                            op == TOKEN_AND ? "and" : "or", type_name(right));
                break;
            }
            result = type_bool(ctx->reg);
        } else if (op == TOKEN_PLUS || op == TOKEN_MINUS || op == TOKEN_STAR || op == TOKEN_SLASH) {
            if (!type_is_numeric(left)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "left operand of arithmetic must be numeric, got '%s'", type_name(left));
                break;
            }
            if (!type_is_numeric(right)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "right operand of arithmetic must be numeric, got '%s'", type_name(right));
                break;
            }
            if (!type_equals(left, right)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "type mismatch in arithmetic: '%s' vs '%s'", type_name(left), type_name(right));
                break;
            }
            result = left;
        } else if (op == TOKEN_CARET) {
            if (!type_is_integer(left) || !type_is_integer(right)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "bitwise '^' requires integer operands");
                break;
            }
            if (!type_equals(left, right)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "type mismatch in bitwise: '%s' vs '%s'", type_name(left), type_name(right));
                break;
            }
            result = left;
        } else if (op == TOKEN_EQUAL || op == TOKEN_NOT_EQUAL ||
                   op == TOKEN_LESS_THAN || op == TOKEN_GREATER_THAN ||
                   op == TOKEN_LESS_THAN_OR_EQUAL || op == TOKEN_GREATER_THAN_OR_EQUAL) {
            if (!type_equals(left, right)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "type mismatch in comparison: '%s' vs '%s'", type_name(left), type_name(right));
                break;
            }
            result = type_bool(ctx->reg);
        }
        break;
    }

    case NODE_UNARY_EXPR: {
        Type* operand = check_expr(ctx, node->as.unary_expr.operand);
        if (!operand) break;

        TokenType op = node->as.unary_expr.op;
        if (op == TOKEN_MINUS) {
            if (!type_is_numeric(operand)) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "unary '-' requires numeric operand, got '%s'", type_name(operand));
                break;
            }
            result = operand;
        } else if (op == TOKEN_NOT) {
            if (operand->kind != TYPE_BOOL) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "'not' requires bool operand, got '%s'", type_name(operand));
                break;
            }
            result = type_bool(ctx->reg);
        } else if (op == TOKEN_AMPERSAND) {
            result = type_ref(ctx->reg, operand);
        }
        break;
    }

    case NODE_PAREN_EXPR:
        result = check_expr(ctx, node->as.paren_expr.inner);
        break;

    case NODE_CALL_EXPR: {
        // resolve callee — could be identifier referring to a function
        Node* callee = node->as.call_expr.callee;
        Type* callee_type = NULL;

        if (callee && callee->type == NODE_IDENTIFIER) {
            Symbol* sym = scope_lookup(ctx, callee->as.identifier.name,
                                        callee->as.identifier.name_size);
            if (!sym) {
                errors_push(ctx->errors, SEVERITY_ERROR, callee->offset, callee->line, callee->column,
                            "undefined function '%.*s'",
                            (int)callee->as.identifier.name_size, callee->as.identifier.name);
                break;
            }
            callee_type = get_symbol_type(sym);
            callee->resolved_type = callee_type;
        } else {
            callee_type = check_expr(ctx, callee);
        }

        if (!callee_type) break;

        if (callee_type->kind != TYPE_FUNC) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot call non-function type '%s'", type_name(callee_type));
            break;
        }

        // check arg count
        NodeList* args = &node->as.call_expr.args;
        if ((int)args->count != callee_type->as.func_type.param_count) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "expected %d arguments, got %d",
                        callee_type->as.func_type.param_count, (int)args->count);
            break;
        }

        // check each arg type
        for (int i = 0; i < (int)args->count; i++) {
            Type* arg_type = check_expr(ctx, args->nodes[i]);
            if (!arg_type) continue;
            Type* param_type = callee_type->as.func_type.param_types[i];
            if (param_type && !type_equals(arg_type, param_type)) {
                // allow &Struct where &Interface is expected (duck typing check deferred)
                // allow *void (null) where any pointer is expected
                bool compatible = false;
                if (param_type->kind == TYPE_REF && arg_type->kind == TYPE_REF) {
                    // &ConcreteStruct is compatible with &Interface (for now, allow)
                    compatible = true;
                }
                if (arg_type->kind == TYPE_PTR && arg_type->as.ptr_type.inner->kind == TYPE_VOID &&
                    param_type->kind == TYPE_PTR) {
                    compatible = true;
                }
                if (!compatible) {
                    errors_push(ctx->errors, SEVERITY_ERROR, args->nodes[i]->offset,
                                args->nodes[i]->line, args->nodes[i]->column,
                                "argument %d: expected '%s', got '%s'",
                                i + 1, type_name(param_type), type_name(arg_type));
                }
            }
        }

        result = callee_type->as.func_type.return_type;
        break;
    }

    case NODE_FIELD_ACCESS: {
        Type* obj_type = check_expr(ctx, node->as.field_access.object);
        if (!obj_type) break;

        Type* struct_type = unwrap_to_struct(obj_type);
        if (!struct_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot access field on type '%s'", type_name(obj_type));
            break;
        }

        char* field_name = node->as.field_access.field_name;
        size_t field_name_size = node->as.field_access.field_name_size;
        FieldList* fields = struct_type->as.struct_type.fields;

        for (size_t i = 0; i < fields->count; i++) {
            Field* f = &fields->fields[i];
            if (f->name_size == field_name_size && memcmp(f->name, field_name, field_name_size) == 0) {
                result = resolve_type_node(ctx->reg, ctx->errors, ctx->mod->symbols, f->type_node);
                break;
            }
        }

        if (!result) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "no field '%.*s' on struct '%s'",
                        (int)field_name_size, field_name, type_name(struct_type));
        }
        break;
    }

    case NODE_METHOD_CALL: {
        Type* obj_type = check_expr(ctx, node->as.method_call.object);
        if (!obj_type) break;

        Type* struct_type = unwrap_to_struct(obj_type);
        if (!struct_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot call method on type '%s'", type_name(obj_type));
            break;
        }

        char* method_name = node->as.method_call.method_name;
        size_t method_name_size = node->as.method_call.method_name_size;
        NodeList* methods = struct_type->as.struct_type.methods;

        Node* method_node = NULL;
        for (size_t i = 0; i < methods->count; i++) {
            Node* m = methods->nodes[i];
            if (m->type == NODE_FUNC_DECL &&
                m->as.func_decl.name_size == method_name_size &&
                memcmp(m->as.func_decl.name, method_name, method_name_size) == 0) {
                method_node = m;
                break;
            }
        }

        if (!method_node) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "no method '%.*s' on struct '%s'",
                        (int)method_name_size, method_name, type_name(struct_type));
            break;
        }

        // check args (methods don't take explicit self in call)
        NodeList* args = &node->as.method_call.args;
        ParamList* params = &method_node->as.func_decl.params;
        if (args->count != params->count) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "method '%.*s' expects %d arguments, got %d",
                        (int)method_name_size, method_name,
                        (int)params->count, (int)args->count);
            break;
        }

        for (size_t i = 0; i < args->count; i++) {
            check_expr(ctx, args->nodes[i]);
        }

        // return type
        Type* ret = resolve_type_node(ctx->reg, ctx->errors, ctx->mod->symbols,
                                       method_node->as.func_decl.return_type);
        result = ret;
        break;
    }

    case NODE_STRUCT_LITERAL: {
        char* name = node->as.struct_literal.struct_name;
        size_t name_size = node->as.struct_literal.struct_name_size;
        Symbol* sym = scope_lookup(ctx, name, name_size);
        if (!sym) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "undefined struct '%.*s'", (int)name_size, name);
            break;
        }
        Type* st = get_symbol_type(sym);
        if (!st || st->kind != TYPE_STRUCT) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "'%.*s' is not a struct", (int)name_size, name);
            break;
        }

        FieldInitList* inits = &node->as.struct_literal.fields;
        FieldList* fields = st->as.struct_type.fields;

        for (size_t i = 0; i < inits->count; i++) {
            FieldInit* fi = &inits->inits[i];
            // find the field
            bool found = false;
            for (size_t j = 0; j < fields->count; j++) {
                Field* f = &fields->fields[j];
                if (f->name_size == fi->name_size && memcmp(f->name, fi->name, fi->name_size) == 0) {
                    found = true;
                    Type* val_type = check_expr(ctx, fi->value);
                    if (val_type) {
                        Type* field_type = resolve_type_node(ctx->reg, ctx->errors,
                                                              ctx->mod->symbols, f->type_node);
                        if (field_type && !type_equals(val_type, field_type)) {
                            errors_push(ctx->errors, SEVERITY_ERROR, fi->offset, fi->line, fi->column,
                                        "field '%.*s': expected '%s', got '%s'",
                                        (int)fi->name_size, fi->name,
                                        type_name(field_type), type_name(val_type));
                        }
                    }
                    break;
                }
            }
            if (!found) {
                errors_push(ctx->errors, SEVERITY_ERROR, fi->offset, fi->line, fi->column,
                            "no field '%.*s' on struct '%.*s'",
                            (int)fi->name_size, fi->name, (int)name_size, name);
            }
        }

        result = st;
        break;
    }

    default:
        break;
    }

    if (result) node->resolved_type = result;
    return result;
}

static void check_stmt(CheckContext* ctx, Node* node) {
    if (!node) return;

    switch (node->type) {

    case NODE_VAR_DECL: {
        Type* declared_type = NULL;
        if (node->as.var_decl.type_node) {
            declared_type = resolve_type_node(ctx->reg, ctx->errors,
                                              ctx->mod->symbols, node->as.var_decl.type_node);
        }
        Type* init_type = NULL;
        if (node->as.var_decl.value) {
            init_type = check_expr(ctx, node->as.var_decl.value);
        }

        Type* var_type = declared_type ? declared_type : init_type;
        if (!var_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot determine type of variable '%.*s'",
                        (int)node->as.var_decl.name_size, node->as.var_decl.name);
            break;
        }

        if (declared_type && init_type && !type_equals(declared_type, init_type)) {
            // allow null (*void) assigned to any pointer type
            bool compatible = false;
            if (init_type->kind == TYPE_PTR && init_type->as.ptr_type.inner->kind == TYPE_VOID &&
                declared_type->kind == TYPE_PTR) {
                compatible = true;
            }
            if (!compatible) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "variable '%.*s': declared as '%s' but initialized with '%s'",
                            (int)node->as.var_decl.name_size, node->as.var_decl.name,
                            type_name(declared_type), type_name(init_type));
            }
        }

        scope_add(ctx, node->as.var_decl.name, node->as.var_decl.name_size, var_type, node);
        break;
    }

    case NODE_CONST_DECL: {
        Type* declared_type = NULL;
        if (node->as.const_decl.type_node) {
            declared_type = resolve_type_node(ctx->reg, ctx->errors,
                                              ctx->mod->symbols, node->as.const_decl.type_node);
        }
        Type* init_type = NULL;
        if (node->as.const_decl.value) {
            init_type = check_expr(ctx, node->as.const_decl.value);
        }

        Type* const_type = declared_type ? declared_type : init_type;
        if (!const_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot determine type of constant '%.*s'",
                        (int)node->as.const_decl.name_size, node->as.const_decl.name);
            break;
        }

        scope_add(ctx, node->as.const_decl.name, node->as.const_decl.name_size, const_type, node);
        break;
    }

    case NODE_RETURN_STMT: {
        if (node->as.return_stmt.value) {
            Type* val = check_expr(ctx, node->as.return_stmt.value);
            if (val && ctx->return_type) {
                if (!type_equals(val, ctx->return_type)) {
                    // allow integer literal in non-int integer context (e.g. return 4 in ushort func)
                    bool compatible = false;
                    if (type_is_integer(val) && type_is_integer(ctx->return_type)) {
                        compatible = true;
                    }
                    if (!compatible) {
                        errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                                    "return type mismatch: expected '%s', got '%s'",
                                    type_name(ctx->return_type), type_name(val));
                    }
                }
            }
        } else {
            if (ctx->return_type && ctx->return_type->kind != TYPE_VOID) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "return without value in function returning '%s'",
                            type_name(ctx->return_type));
            }
        }
        break;
    }

    case NODE_IF_STMT: {
        Type* cond = check_expr(ctx, node->as.if_stmt.condition);
        if (cond && cond->kind != TYPE_BOOL) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "if condition must be bool, got '%s'", type_name(cond));
        }
        check_body(ctx, &node->as.if_stmt.then_body);

        ElseIfList* elseifs = &node->as.if_stmt.elseifs;
        for (size_t i = 0; i < elseifs->count; i++) {
            Type* ei_cond = check_expr(ctx, elseifs->branches[i].condition);
            if (ei_cond && ei_cond->kind != TYPE_BOOL) {
                errors_push(ctx->errors, SEVERITY_ERROR,
                            elseifs->branches[i].offset, elseifs->branches[i].line,
                            elseifs->branches[i].column,
                            "elseif condition must be bool, got '%s'", type_name(ei_cond));
            }
            check_body(ctx, &elseifs->branches[i].body);
        }

        if (node->as.if_stmt.else_body.count > 0) {
            check_body(ctx, &node->as.if_stmt.else_body);
        }
        break;
    }

    case NODE_FOR_STMT: {
        Type* start_type = check_expr(ctx, node->as.for_stmt.start);
        Type* end_type = check_expr(ctx, node->as.for_stmt.end);
        if (node->as.for_stmt.step) {
            check_expr(ctx, node->as.for_stmt.step);
        }

        // determine iterator type from start expression
        Type* iter_type = start_type ? start_type : type_int(ctx->reg);

        Scope* prev = ctx->scope;
        scope_push(ctx);
        scope_add(ctx, node->as.for_stmt.var_name, node->as.for_stmt.var_name_size,
                  iter_type, NULL);
        check_body(ctx, &node->as.for_stmt.body);
        scope_pop(ctx, prev);
        break;
    }

    case NODE_WHILE_STMT: {
        Type* cond = check_expr(ctx, node->as.while_stmt.condition);
        if (cond && cond->kind != TYPE_BOOL) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "while condition must be bool, got '%s'", type_name(cond));
        }
        check_body(ctx, &node->as.while_stmt.body);
        break;
    }

    case NODE_BREAK_STMT:
        break;

    case NODE_MATCH_STMT: {
        Type* subject = check_expr(ctx, node->as.match_stmt.subject);
        MatchCaseList* cases = &node->as.match_stmt.cases;
        for (size_t i = 0; i < cases->count; i++) {
            NodeList* vals = &cases->cases[i].values;
            for (size_t j = 0; j < vals->count; j++) {
                Type* val_type = check_expr(ctx, vals->nodes[j]);
                if (val_type && subject && !type_equals(val_type, subject)) {
                    errors_push(ctx->errors, SEVERITY_ERROR,
                                vals->nodes[j]->offset, vals->nodes[j]->line, vals->nodes[j]->column,
                                "match case type mismatch: expected '%s', got '%s'",
                                type_name(subject), type_name(val_type));
                }
            }
            check_body(ctx, &cases->cases[i].body);
        }
        if (node->as.match_stmt.else_body.count > 0) {
            check_body(ctx, &node->as.match_stmt.else_body);
        }
        break;
    }

    case NODE_ASSIGN_STMT: {
        Type* target = check_expr(ctx, node->as.assign_stmt.target);
        Type* value = check_expr(ctx, node->as.assign_stmt.value);
        if (target && value && !type_equals(target, value)) {
            bool compatible = false;
            // allow null assigned to pointer
            if (value->kind == TYPE_PTR && value->as.ptr_type.inner->kind == TYPE_VOID &&
                target->kind == TYPE_PTR) {
                compatible = true;
            }
            // allow &T assigned to *T (taking address into pointer)
            if (value->kind == TYPE_REF && target->kind == TYPE_PTR) {
                compatible = true;
            }
            if (!compatible) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "assignment type mismatch: expected '%s', got '%s'",
                            type_name(target), type_name(value));
            }
        }
        break;
    }

    case NODE_COMPOUND_ASSIGN_STMT: {
        Type* target = check_expr(ctx, node->as.compound_assign_stmt.target);
        Type* value = check_expr(ctx, node->as.compound_assign_stmt.value);
        if (target && !type_is_numeric(target)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "compound assignment target must be numeric, got '%s'", type_name(target));
        }
        if (target && value && !type_equals(target, value)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "compound assignment type mismatch: '%s' vs '%s'",
                        type_name(target), type_name(value));
        }
        break;
    }

    case NODE_EXPR_STMT:
        check_expr(ctx, node->as.expr_stmt.expr);
        break;

    default:
        break;
    }
}

static void check_body(CheckContext* ctx, NodeList* body) {
    Scope* prev = ctx->scope;
    scope_push(ctx);
    for (size_t i = 0; i < body->count; i++) {
        check_stmt(ctx, body->nodes[i]);
    }
    scope_pop(ctx, prev);
}

static void check_func_body(CheckContext* ctx, Node* func_node) {
    if (!func_node || func_node->type != NODE_FUNC_DECL) return;

    Type* func_type = (Type*)func_node->resolved_type;
    if (!func_type || func_type->kind != TYPE_FUNC) return;

    ctx->return_type = func_type->as.func_type.return_type;

    Scope* prev = ctx->scope;
    scope_push(ctx);

    // add parameters to scope
    ParamList* params = &func_node->as.func_decl.params;
    for (size_t i = 0; i < params->count; i++) {
        Type* param_type = func_type->as.func_type.param_types[i];
        scope_add(ctx, params->params[i].name, params->params[i].name_size, param_type, NULL);
    }

    // check body statements (not wrapped in check_body to avoid double scope push)
    NodeList* body = &func_node->as.func_decl.body;
    for (size_t i = 0; i < body->count; i++) {
        check_stmt(ctx, body->nodes[i]);
    }

    scope_pop(ctx, prev);
    ctx->return_type = NULL;
}

static void check_struct_methods(CheckContext* ctx, Node* struct_node) {
    if (!struct_node || struct_node->type != NODE_STRUCT_DECL) return;

    Type* struct_type = (Type*)struct_node->resolved_type;
    if (!struct_type) return;

    Type* self_ref = type_ref(ctx->reg, struct_type);
    Type* prev_self = ctx->self_type;
    ctx->self_type = self_ref;

    NodeList* methods = &struct_node->as.struct_decl.methods;
    for (size_t i = 0; i < methods->count; i++) {
        Node* method = methods->nodes[i];
        if (method->type != NODE_FUNC_DECL) continue;

        // resolve method type if not already done
        if (!method->resolved_type) {
            ParamList* params = &method->as.func_decl.params;
            int param_count = (int)params->count;
            Type** param_types = NULL;
            if (param_count > 0) {
                param_types = arena_alloc(ctx->arena, sizeof(Type*) * param_count);
                for (int j = 0; j < param_count; j++) {
                    param_types[j] = resolve_type_node(ctx->reg, ctx->errors,
                                                        ctx->mod->symbols, params->params[j].type_node);
                }
            }
            Type* ret = resolve_type_node(ctx->reg, ctx->errors,
                                           ctx->mod->symbols, method->as.func_decl.return_type);
            method->resolved_type = type_func(ctx->reg, param_types, param_count, ret);
        }

        check_func_body(ctx, method);
    }

    ctx->self_type = prev_self;
}

static void check_module_bodies(Arena* arena, Errors* errors, TypeRegistry* reg, Module* mod) {
    if (!mod->symbols) return;

    CheckContext ctx;
    ctx.arena = arena;
    ctx.errors = errors;
    ctx.reg = reg;
    ctx.mod = mod;
    ctx.scope = NULL;
    ctx.return_type = NULL;
    ctx.self_type = NULL;

    for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
        if (!sym->node) continue;

        switch (sym->kind) {
        case SYMBOL_FUNC:
            check_func_body(&ctx, sym->node);
            break;
        case SYMBOL_STRUCT:
            check_struct_methods(&ctx, sym->node);
            break;
        case SYMBOL_VAR:
        case SYMBOL_CONST:
            // check top-level initializer expressions
            if (sym->kind == SYMBOL_VAR && sym->node->as.var_decl.value) {
                Type* init = check_expr(&ctx, sym->node->as.var_decl.value);
                if (init) {
                    if (!sym->node->resolved_type) {
                        sym->node->resolved_type = init;
                    }
                }
            }
            if (sym->kind == SYMBOL_CONST && sym->node->as.const_decl.value) {
                Type* init = check_expr(&ctx, sym->node->as.const_decl.value);
                if (init) {
                    if (!sym->node->resolved_type) {
                        sym->node->resolved_type = init;
                    }
                }
            }
            break;
        default:
            break;
        }
    }
}

void sema_analyze(Arena* arena, Errors* errors, ModuleGraph* graph) {
    // pass 1: collect local declarations
    for (Module* m = graph->first; m; m = m->next) {
        collect_module_symbols(arena, errors, m);
    }

    // pass 2: resolve imports
    for (Module* m = graph->first; m; m = m->next) {
        resolve_module_imports(arena, errors, graph, m);
    }

    // pass 3: resolve types
    TypeRegistry reg;
    type_registry_init(&reg, arena);

    // 3a: structs and interfaces first (so they can be referenced by functions)
    for (Module* m = graph->first; m; m = m->next) {
        resolve_module_types(arena, errors, &reg, m);
    }

    // 3b: function signatures (may reference struct/interface types)
    for (Module* m = graph->first; m; m = m->next) {
        resolve_func_types(arena, errors, &reg, m);
    }

    // pass 4: check function bodies and expressions
    for (Module* m = graph->first; m; m = m->next) {
        check_module_bodies(arena, errors, &reg, m);
    }
}
