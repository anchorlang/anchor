#include "sema.h"
#include "module.h"
#include "type.h"
#include "lexer.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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
    sym->resolved_type = NULL;
    if (!table->first) {
        table->first = sym;
    } else {
        table->last->next = sym;
    }
    table->last = sym;
    table->count++;
    return sym;
}

Symbol* symbol_find(SymbolTable* table, char* name, size_t name_size) {
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
        case NODE_ENUM_DECL:
            kind = SYMBOL_ENUM;
            name = node->as.enum_decl.name;
            name_size = node->as.enum_decl.name_size;
            is_export = node->as.enum_decl.is_export;
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
    if (node->resolved_type) return (Type*)node->resolved_type;

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
        if (size == 5 && memcmp(name, "isize", 5) == 0) return type_isize(reg);
        if (size == 5 && memcmp(name, "usize", 5) == 0) return type_usize(reg);
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
    case NODE_TYPE_ARRAY: {
        Type* element = resolve_type_node(reg, errors, table, node->as.type_array.inner);
        if (!element) return NULL;
        Node* size_node = node->as.type_array.size_expr;
        if (!size_node || size_node->type != NODE_INTEGER_LITERAL) {
            errors_push(errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "array size must be an integer literal");
            return NULL;
        }
        int arr_size = 0;
        for (size_t i = 0; i < size_node->as.integer_literal.value_size; i++) {
            arr_size = arr_size * 10 + (size_node->as.integer_literal.value[i] - '0');
        }
        if (arr_size <= 0) {
            errors_push(errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "array size must be positive");
            return NULL;
        }
        return type_array(reg, element, arr_size);
    }
    case NODE_TYPE_SLICE: {
        Type* element = resolve_type_node(reg, errors, table, node->as.type_slice.inner);
        if (!element) return NULL;
        return type_slice(reg, element);
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
            // skip generic templates — they are instantiated on demand
            if (sym->node->as.struct_decl.type_params.count > 0) break;

            Type* t = type_struct(reg,
                sym->node->as.struct_decl.name,
                sym->node->as.struct_decl.name_size,
                mod,
                &sym->node->as.struct_decl.fields,
                &sym->node->as.struct_decl.methods);
            sym->node->resolved_type = t;

            // resolve field type nodes
            FieldList* fields = &sym->node->as.struct_decl.fields;
            for (size_t i = 0; i < fields->count; i++) {
                if (fields->fields[i].type_node) {
                    fields->fields[i].type_node->resolved_type =
                        resolve_type_node(reg, errors, mod->symbols, fields->fields[i].type_node);
                }
            }
            break;
        }
        case SYMBOL_ENUM: {
            Type* t = type_enum(reg,
                sym->node->as.enum_decl.name,
                sym->node->as.enum_decl.name_size,
                mod,
                &sym->node->as.enum_decl.variants);
            sym->node->resolved_type = t;
            break;
        }
        case SYMBOL_INTERFACE: {
            Type* t = type_interface(reg,
                sym->node->as.interface_decl.name,
                sym->node->as.interface_decl.name_size,
                &sym->node->as.interface_decl.method_sigs);
            sym->node->resolved_type = t;

            // resolve method signature types
            NodeList* sigs = &sym->node->as.interface_decl.method_sigs;
            for (size_t i = 0; i < sigs->count; i++) {
                Node* sig = sigs->nodes[i];
                if (sig->type != NODE_FUNC_DECL || sig->resolved_type) continue;
                if (sig->as.func_decl.type_params.count > 0) continue; // skip generic sigs
                ParamList* params = &sig->as.func_decl.params;
                int param_count = (int)params->count;
                Type** param_types = NULL;
                if (param_count > 0) {
                    param_types = arena_alloc(arena, sizeof(Type*) * param_count);
                    for (int j = 0; j < param_count; j++) {
                        param_types[j] = resolve_type_node(reg, errors, mod->symbols,
                                                            params->params[j].type_node);
                    }
                }
                Type* ret = resolve_type_node(reg, errors, mod->symbols,
                                               sig->as.func_decl.return_type);
                sig->resolved_type = type_func(reg, param_types, param_count, ret);
            }
            break;
        }
        default:
            break;
        }
    }
}

// forward declarations for CheckContext and resolve_generic_type
// (needed by resolve_func_types to handle generic types in function signatures)
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
    Type* self_type;
    int loop_depth;
    int real_loop_depth;
} CheckContext;

static Type* resolve_generic_type(CheckContext* ctx, Node* type_node);

static void resolve_func_types(Arena* arena, Errors* errors,
                                TypeRegistry* reg, Module* mod) {
    if (!mod->symbols) return;

    // minimal CheckContext for resolve_generic_type (needed for generic param/return types)
    CheckContext func_ctx = {0};
    func_ctx.arena = arena;
    func_ctx.errors = errors;
    func_ctx.reg = reg;
    func_ctx.mod = mod;

    for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_FUNC || !sym->node) continue;

        // skip generic templates — they are instantiated on demand
        if (sym->node->as.func_decl.type_params.count > 0) continue;

        ParamList* params = &sym->node->as.func_decl.params;
        int param_count = (int)params->count;
        Type** param_types = NULL;
        if (param_count > 0) {
            param_types = arena_alloc(arena, sizeof(Type*) * param_count);
            for (int i = 0; i < param_count; i++) {
                param_types[i] = resolve_generic_type(&func_ctx, params->params[i].type_node);
            }
        }

        Type* return_type = resolve_generic_type(&func_ctx, sym->node->as.func_decl.return_type);

        Type* func_t = type_func(reg, param_types, param_count, return_type);
        sym->node->resolved_type = func_t;
    }
}

// ---------------------------------------------------------------------------
// Pass 4: Expression & statement type checking
// ---------------------------------------------------------------------------

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

static void scope_add(CheckContext* ctx, SymbolKind kind, char* name, size_t name_size, Type* type, Node* node) {
    Symbol* sym = arena_alloc(ctx->arena, sizeof(Symbol));
    sym->next = NULL;
    sym->kind = kind;
    sym->name = name;
    sym->name_size = name_size;
    sym->is_export = false;
    sym->node = node;
    sym->source = NULL;
    sym->resolved_type = type;
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
    if (!sym) return NULL;
    if (sym->resolved_type) return sym->resolved_type;
    if (sym->node) return (Type*)sym->node->resolved_type;
    return NULL;
}

// unwrap &T or *T to get the struct type underneath
static Type* unwrap_to_struct(Type* type) {
    if (!type) return NULL;
    if (type->kind == TYPE_STRUCT) return type;
    if (type->kind == TYPE_REF) return unwrap_to_struct(type->as.ref_type.inner);
    if (type->kind == TYPE_PTR) return unwrap_to_struct(type->as.ptr_type.inner);
    return NULL;
}

// unwrap &T or *T to get the interface type underneath
static Type* unwrap_to_interface(Type* type) {
    if (!type) return NULL;
    if (type->kind == TYPE_INTERFACE) return type;
    if (type->kind == TYPE_REF) return unwrap_to_interface(type->as.ref_type.inner);
    if (type->kind == TYPE_PTR) return unwrap_to_interface(type->as.ptr_type.inner);
    return NULL;
}

// check if a struct satisfies an interface (has all required methods with matching signatures)
static bool check_interface_satisfaction(CheckContext* ctx, Type* struct_type, Type* iface_type) {
    if (!struct_type || !iface_type) return false;
    if (struct_type->kind != TYPE_STRUCT || iface_type->kind != TYPE_INTERFACE) return false;

    NodeList* iface_sigs = iface_type->as.interface_type.method_sigs;
    NodeList* struct_methods = struct_type->as.struct_type.methods;

    for (size_t i = 0; i < iface_sigs->count; i++) {
        Node* sig = iface_sigs->nodes[i];
        if (sig->type != NODE_FUNC_DECL) continue;

        char* sig_name = sig->as.func_decl.name;
        size_t sig_name_size = sig->as.func_decl.name_size;

        // find matching method on struct
        bool found = false;
        for (size_t j = 0; j < struct_methods->count; j++) {
            Node* m = struct_methods->nodes[j];
            if (m->type != NODE_FUNC_DECL) continue;
            if (m->as.func_decl.name_size == sig_name_size &&
                memcmp(m->as.func_decl.name, sig_name, sig_name_size) == 0) {
                // check param count matches
                if (m->as.func_decl.params.count != sig->as.func_decl.params.count) {
                    return false;
                }
                // check type param count matches (generic methods)
                if (m->as.func_decl.type_params.count != sig->as.func_decl.type_params.count) {
                    return false;
                }
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

// record a (struct, interface) implementation pair on the module
static void impl_pair_add(Module* mod, Type* struct_type, Type* iface_type, Module* struct_mod) {
    ImplPairList* list = &mod->impl_pairs;

    // check for duplicate
    for (size_t i = 0; i < list->count; i++) {
        if (list->pairs[i].struct_type == struct_type &&
            list->pairs[i].interface_type == iface_type) {
            return;
        }
    }

    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        ImplPair* new_pairs = realloc(list->pairs, new_cap * sizeof(ImplPair));
        list->pairs = new_pairs;
        list->capacity = new_cap;
    }

    list->pairs[list->count].struct_type = struct_type;
    list->pairs[list->count].interface_type = iface_type;
    list->pairs[list->count].struct_module = struct_mod;
    list->count++;
}

static bool is_lvalue(Node* node) {
    if (!node) return false;
    switch (node->type) {
    case NODE_IDENTIFIER:
    case NODE_FIELD_ACCESS:
    case NODE_SELF:
    case NODE_INDEX_EXPR:
        return true;
    case NODE_UNARY_EXPR:
        return node->as.unary_expr.op == TOKEN_STAR;
    default:
        return false;
    }
}

static Type* check_expr(CheckContext* ctx, Node* node);
static void check_stmt(CheckContext* ctx, Node* node);
static void check_body(CheckContext* ctx, NodeList* body);
static void check_func_body(CheckContext* ctx, Node* func_node);

// ---------------------------------------------------------------------------
// Monomorphization engine for generics
// ---------------------------------------------------------------------------

typedef struct TypeSubst {
    char** param_names;
    size_t* param_name_sizes;
    Type** concrete_types;
    size_t count;
} TypeSubst;

static Type* subst_lookup(TypeSubst* subst, char* name, size_t name_size) {
    for (size_t i = 0; i < subst->count; i++) {
        if (subst->param_name_sizes[i] == name_size &&
            memcmp(subst->param_names[i], name, name_size) == 0) {
            return subst->concrete_types[i];
        }
    }
    return NULL;
}

// Build mangled name: "max" + [int] -> "max__int", "Pair" + [int, float] -> "Pair__int__float"
static char* build_mangled_name(Arena* arena, char* base, size_t base_size,
                                 Type** type_args, size_t type_arg_count, size_t* out_size) {
    // estimate size
    size_t total = base_size;
    for (size_t i = 0; i < type_arg_count; i++) {
        total += 2 + strlen(type_name(type_args[i])); // "__" + type name
    }
    char* buf = arena_alloc(arena, total + 1);
    size_t pos = 0;
    memcpy(buf + pos, base, base_size);
    pos += base_size;
    for (size_t i = 0; i < type_arg_count; i++) {
        buf[pos++] = '_';
        buf[pos++] = '_';
        const char* tname = type_name(type_args[i]);
        size_t tlen = strlen(tname);
        memcpy(buf + pos, tname, tlen);
        pos += tlen;
    }
    buf[pos] = '\0';
    *out_size = pos;
    return buf;
}

// Find an existing generic instantiation for the same template + type args
static GenericInst* find_generic_inst(Module* mod, Node* template_decl,
                                       Type** type_args, size_t type_arg_count) {
    for (GenericInst* inst = mod->generic_insts.first; inst; inst = inst->next) {
        if (inst->template_decl != template_decl) continue;
        if (inst->type_arg_count != type_arg_count) continue;
        bool match = true;
        for (size_t i = 0; i < type_arg_count; i++) {
            if (!type_equals(inst->type_args[i], type_args[i])) {
                match = false;
                break;
            }
        }
        if (match) return inst;
    }
    return NULL;
}

static void generic_inst_add(Arena* arena, Module* mod, GenericInst* inst) {
    inst->next = NULL;
    if (!mod->generic_insts.first) {
        mod->generic_insts.first = inst;
    } else {
        mod->generic_insts.last->next = inst;
    }
    mod->generic_insts.last = inst;
}

// Deep-copy an AST node, substituting type parameter names with concrete types
static Node* deep_copy_node(Arena* arena, Node* src, TypeSubst* subst);

static NodeList deep_copy_node_list(Arena* arena, NodeList* src, TypeSubst* subst) {
    NodeList dst = {0};
    for (size_t i = 0; i < src->count; i++) {
        Node* copy = deep_copy_node(arena, src->nodes[i], subst);
        if (copy) {
            if (dst.count >= dst.capacity) {
                size_t new_cap = dst.capacity < 8 ? 8 : dst.capacity * 2;
                Node** new_nodes = arena_alloc(arena, new_cap * sizeof(Node*));
                if (dst.nodes) memcpy(new_nodes, dst.nodes, dst.count * sizeof(Node*));
                dst.nodes = new_nodes;
                dst.capacity = new_cap;
            }
            dst.nodes[dst.count++] = copy;
        }
    }
    return dst;
}

static FieldList deep_copy_field_list(Arena* arena, FieldList* src, TypeSubst* subst) {
    FieldList dst = {0};
    if (src->count == 0) return dst;
    dst.fields = arena_alloc(arena, src->count * sizeof(Field));
    dst.count = src->count;
    dst.capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst.fields[i] = src->fields[i];
        dst.fields[i].type_node = deep_copy_node(arena, src->fields[i].type_node, subst);
    }
    return dst;
}

static ParamList deep_copy_param_list(Arena* arena, ParamList* src, TypeSubst* subst) {
    ParamList dst = {0};
    if (src->count == 0) return dst;
    dst.params = arena_alloc(arena, src->count * sizeof(Param));
    dst.count = src->count;
    dst.capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst.params[i] = src->params[i];
        dst.params[i].type_node = deep_copy_node(arena, src->params[i].type_node, subst);
    }
    return dst;
}

static FieldInitList deep_copy_field_init_list(Arena* arena, FieldInitList* src, TypeSubst* subst) {
    FieldInitList dst = {0};
    if (src->count == 0) return dst;
    dst.inits = arena_alloc(arena, src->count * sizeof(FieldInit));
    dst.count = src->count;
    dst.capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst.inits[i] = src->inits[i];
        dst.inits[i].value = deep_copy_node(arena, src->inits[i].value, subst);
    }
    return dst;
}

static ElseIfList deep_copy_elseif_list(Arena* arena, ElseIfList* src, TypeSubst* subst) {
    ElseIfList dst = {0};
    if (src->count == 0) return dst;
    dst.branches = arena_alloc(arena, src->count * sizeof(ElseIfBranch));
    dst.count = src->count;
    dst.capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst.branches[i] = src->branches[i];
        dst.branches[i].condition = deep_copy_node(arena, src->branches[i].condition, subst);
        dst.branches[i].body = deep_copy_node_list(arena, &src->branches[i].body, subst);
    }
    return dst;
}

static MatchCaseList deep_copy_match_case_list(Arena* arena, MatchCaseList* src, TypeSubst* subst) {
    MatchCaseList dst = {0};
    if (src->count == 0) return dst;
    dst.cases = arena_alloc(arena, src->count * sizeof(MatchCase));
    dst.count = src->count;
    dst.capacity = src->count;
    for (size_t i = 0; i < src->count; i++) {
        dst.cases[i] = src->cases[i];
        dst.cases[i].values = deep_copy_node_list(arena, &src->cases[i].values, subst);
        dst.cases[i].body = deep_copy_node_list(arena, &src->cases[i].body, subst);
    }
    return dst;
}

static Node* deep_copy_node(Arena* arena, Node* src, TypeSubst* subst) {
    if (!src) return NULL;
    Node* dst = arena_alloc(arena, sizeof(Node));
    *dst = *src; // shallow copy first
    dst->resolved_type = NULL; // clear — will be re-resolved

    switch (src->type) {
    // Type nodes — this is where substitution happens
    case NODE_TYPE_SIMPLE: {
        // Check if this type name is a type parameter
        Type* concrete = subst_lookup(subst, src->as.type_simple.name, src->as.type_simple.name_size);
        if (concrete) {
            // Replace with a concrete type node
            // We keep it as NODE_TYPE_SIMPLE but change the name to the concrete type's name
            const char* cname = type_name(concrete);
            size_t clen = strlen(cname);
            char* name_copy = arena_alloc(arena, clen);
            memcpy(name_copy, cname, clen);
            dst->as.type_simple.name = name_copy;
            dst->as.type_simple.name_size = clen;
            // Pre-resolve it so resolve_type_node finds it
            dst->resolved_type = concrete;
        }
        // Deep-copy type_args so e.g. Node[T] becomes Node[int]
        dst->as.type_simple.type_args = deep_copy_node_list(arena, &src->as.type_simple.type_args, subst);
        break;
    }
    case NODE_TYPE_REFERENCE:
        dst->as.type_ref.inner = deep_copy_node(arena, src->as.type_ref.inner, subst);
        break;
    case NODE_TYPE_POINTER:
        dst->as.type_ptr.inner = deep_copy_node(arena, src->as.type_ptr.inner, subst);
        break;
    case NODE_TYPE_ARRAY:
        dst->as.type_array.inner = deep_copy_node(arena, src->as.type_array.inner, subst);
        dst->as.type_array.size_expr = deep_copy_node(arena, src->as.type_array.size_expr, subst);
        break;
    case NODE_TYPE_SLICE:
        dst->as.type_slice.inner = deep_copy_node(arena, src->as.type_slice.inner, subst);
        break;

    // Declaration nodes
    case NODE_FUNC_DECL:
        dst->as.func_decl.type_params.count = 0; // no longer generic
        dst->as.func_decl.type_params.params = NULL;
        dst->as.func_decl.params = deep_copy_param_list(arena, &src->as.func_decl.params, subst);
        dst->as.func_decl.return_type = deep_copy_node(arena, src->as.func_decl.return_type, subst);
        dst->as.func_decl.body = deep_copy_node_list(arena, &src->as.func_decl.body, subst);
        break;
    case NODE_STRUCT_DECL:
        dst->as.struct_decl.type_params.count = 0; // no longer generic
        dst->as.struct_decl.type_params.params = NULL;
        dst->as.struct_decl.fields = deep_copy_field_list(arena, &src->as.struct_decl.fields, subst);
        dst->as.struct_decl.methods = deep_copy_node_list(arena, &src->as.struct_decl.methods, subst);
        break;

    // Statement nodes
    case NODE_RETURN_STMT:
        dst->as.return_stmt.value = deep_copy_node(arena, src->as.return_stmt.value, subst);
        break;
    case NODE_IF_STMT:
        dst->as.if_stmt.condition = deep_copy_node(arena, src->as.if_stmt.condition, subst);
        dst->as.if_stmt.then_body = deep_copy_node_list(arena, &src->as.if_stmt.then_body, subst);
        dst->as.if_stmt.elseifs = deep_copy_elseif_list(arena, &src->as.if_stmt.elseifs, subst);
        dst->as.if_stmt.else_body = deep_copy_node_list(arena, &src->as.if_stmt.else_body, subst);
        break;
    case NODE_FOR_STMT:
        dst->as.for_stmt.start = deep_copy_node(arena, src->as.for_stmt.start, subst);
        dst->as.for_stmt.end = deep_copy_node(arena, src->as.for_stmt.end, subst);
        dst->as.for_stmt.step = deep_copy_node(arena, src->as.for_stmt.step, subst);
        dst->as.for_stmt.body = deep_copy_node_list(arena, &src->as.for_stmt.body, subst);
        break;
    case NODE_WHILE_STMT:
        dst->as.while_stmt.condition = deep_copy_node(arena, src->as.while_stmt.condition, subst);
        dst->as.while_stmt.body = deep_copy_node_list(arena, &src->as.while_stmt.body, subst);
        break;
    case NODE_MATCH_STMT:
        dst->as.match_stmt.subject = deep_copy_node(arena, src->as.match_stmt.subject, subst);
        dst->as.match_stmt.cases = deep_copy_match_case_list(arena, &src->as.match_stmt.cases, subst);
        dst->as.match_stmt.else_body = deep_copy_node_list(arena, &src->as.match_stmt.else_body, subst);
        break;
    case NODE_ASSIGN_STMT:
        dst->as.assign_stmt.target = deep_copy_node(arena, src->as.assign_stmt.target, subst);
        dst->as.assign_stmt.value = deep_copy_node(arena, src->as.assign_stmt.value, subst);
        break;
    case NODE_COMPOUND_ASSIGN_STMT:
        dst->as.compound_assign_stmt.target = deep_copy_node(arena, src->as.compound_assign_stmt.target, subst);
        dst->as.compound_assign_stmt.value = deep_copy_node(arena, src->as.compound_assign_stmt.value, subst);
        break;
    case NODE_EXPR_STMT:
        dst->as.expr_stmt.expr = deep_copy_node(arena, src->as.expr_stmt.expr, subst);
        break;
    case NODE_VAR_DECL:
        dst->as.var_decl.type_node = deep_copy_node(arena, src->as.var_decl.type_node, subst);
        dst->as.var_decl.value = deep_copy_node(arena, src->as.var_decl.value, subst);
        break;
    case NODE_CONST_DECL:
        dst->as.const_decl.type_node = deep_copy_node(arena, src->as.const_decl.type_node, subst);
        dst->as.const_decl.value = deep_copy_node(arena, src->as.const_decl.value, subst);
        break;

    // Expression nodes
    case NODE_BINARY_EXPR:
        dst->as.binary_expr.left = deep_copy_node(arena, src->as.binary_expr.left, subst);
        dst->as.binary_expr.right = deep_copy_node(arena, src->as.binary_expr.right, subst);
        break;
    case NODE_UNARY_EXPR:
        dst->as.unary_expr.operand = deep_copy_node(arena, src->as.unary_expr.operand, subst);
        break;
    case NODE_PAREN_EXPR:
        dst->as.paren_expr.inner = deep_copy_node(arena, src->as.paren_expr.inner, subst);
        break;
    case NODE_CALL_EXPR:
        dst->as.call_expr.callee = deep_copy_node(arena, src->as.call_expr.callee, subst);
        dst->as.call_expr.args = deep_copy_node_list(arena, &src->as.call_expr.args, subst);
        dst->as.call_expr.type_args = deep_copy_node_list(arena, &src->as.call_expr.type_args, subst);
        break;
    case NODE_FIELD_ACCESS:
        dst->as.field_access.object = deep_copy_node(arena, src->as.field_access.object, subst);
        break;
    case NODE_METHOD_CALL:
        dst->as.method_call.object = deep_copy_node(arena, src->as.method_call.object, subst);
        dst->as.method_call.args = deep_copy_node_list(arena, &src->as.method_call.args, subst);
        dst->as.method_call.type_args = deep_copy_node_list(arena, &src->as.method_call.type_args, subst);
        break;
    case NODE_STRUCT_LITERAL:
        dst->as.struct_literal.fields = deep_copy_field_init_list(arena, &src->as.struct_literal.fields, subst);
        dst->as.struct_literal.type_args = deep_copy_node_list(arena, &src->as.struct_literal.type_args, subst);
        break;
    case NODE_CAST_EXPR:
        dst->as.cast_expr.expr = deep_copy_node(arena, src->as.cast_expr.expr, subst);
        dst->as.cast_expr.target_type = deep_copy_node(arena, src->as.cast_expr.target_type, subst);
        break;
    case NODE_SIZEOF_EXPR:
        dst->as.sizeof_expr.type_node = deep_copy_node(arena, src->as.sizeof_expr.type_node, subst);
        break;
    case NODE_ARRAY_LITERAL:
        dst->as.array_literal.elements = deep_copy_node_list(arena, &src->as.array_literal.elements, subst);
        break;
    case NODE_INDEX_EXPR:
        dst->as.index_expr.object = deep_copy_node(arena, src->as.index_expr.object, subst);
        dst->as.index_expr.index = deep_copy_node(arena, src->as.index_expr.index, subst);
        break;

    // Leaf nodes — no children to copy
    case NODE_INTEGER_LITERAL:
    case NODE_FLOAT_LITERAL:
    case NODE_STRING_LITERAL:
    case NODE_BOOL_LITERAL:
    case NODE_NULL_LITERAL:
    case NODE_IDENTIFIER:
    case NODE_SELF:
    case NODE_BREAK_STMT:
    case NODE_CONTINUE_STMT:
        break;

    default:
        break;
    }

    return dst;
}

// Build a TypeSubst from template type params and concrete type args
static TypeSubst build_subst(Arena* arena, TypeParamList* params, Type** type_args, size_t type_arg_count) {
    TypeSubst subst;
    subst.count = params->count;
    subst.param_names = arena_alloc(arena, sizeof(char*) * subst.count);
    subst.param_name_sizes = arena_alloc(arena, sizeof(size_t) * subst.count);
    subst.concrete_types = arena_alloc(arena, sizeof(Type*) * subst.count);
    for (size_t i = 0; i < subst.count; i++) {
        subst.param_names[i] = params->params[i].name;
        subst.param_name_sizes[i] = params->params[i].name_size;
        subst.concrete_types[i] = (i < type_arg_count) ? type_args[i] : NULL;
    }
    return subst;
}

// forward declaration — needed because instantiate_generic_struct and resolve_generic_type are mutually recursive
static Type* resolve_generic_type(CheckContext* ctx, Node* type_node);

// Instantiate a generic struct with concrete type arguments.
// Returns the resolved Type* for the instantiation.
static Type* instantiate_generic_struct(CheckContext* ctx, Node* template_decl,
                                         Type** type_args, size_t type_arg_count) {
    TypeParamList* params = &template_decl->as.struct_decl.type_params;
    if (type_arg_count != params->count) {
        errors_push(ctx->errors, SEVERITY_ERROR, template_decl->offset,
                    template_decl->line, template_decl->column,
                    "generic struct '%.*s' expects %d type arguments, got %d",
                    (int)template_decl->as.struct_decl.name_size,
                    template_decl->as.struct_decl.name,
                    (int)params->count, (int)type_arg_count);
        return NULL;
    }

    // dedup
    GenericInst* existing = find_generic_inst(ctx->mod, template_decl, type_args, type_arg_count);
    if (existing) return existing->resolved_type;

    // build substitution and mangled name
    TypeSubst subst = build_subst(ctx->arena, params, type_args, type_arg_count);
    size_t mangled_size;
    char* mangled = build_mangled_name(ctx->arena,
        template_decl->as.struct_decl.name,
        template_decl->as.struct_decl.name_size,
        type_args, type_arg_count, &mangled_size);

    // deep-copy the template
    Node* mono = deep_copy_node(ctx->arena, template_decl, &subst);
    mono->as.struct_decl.name = mangled;
    mono->as.struct_decl.name_size = mangled_size;

    // create the struct type
    Type* t = type_struct(ctx->reg, mangled, mangled_size, ctx->mod,
                           &mono->as.struct_decl.fields, &mono->as.struct_decl.methods);
    mono->resolved_type = t;

    // register the instantiation BEFORE resolving fields to break self-referential cycles
    // (e.g. struct Node[T] { next: *Node[T] } — resolving *Node[int] re-enters instantiate_generic_struct)
    GenericInst* inst = arena_alloc(ctx->arena, sizeof(GenericInst));
    inst->next = NULL;
    inst->template_decl = template_decl;
    inst->type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
    memcpy(inst->type_args, type_args, sizeof(Type*) * type_arg_count);
    inst->type_arg_count = type_arg_count;
    inst->mangled_name = mangled;
    inst->mangled_name_size = mangled_size;
    inst->mono_decl = mono;
    inst->resolved_type = t;
    generic_inst_add(ctx->arena, ctx->mod, inst);

    // add a symbol for the monomorphized struct so codegen can find it
    symbol_add(ctx->arena, ctx->mod->symbols, SYMBOL_STRUCT, mangled, mangled_size,
               template_decl->as.struct_decl.is_export, mono);

    // resolve field types (use resolve_generic_type for fields like *Node[int])
    FieldList* fields = &mono->as.struct_decl.fields;
    for (size_t i = 0; i < fields->count; i++) {
        if (fields->fields[i].type_node) {
            Type* ft = (Type*)fields->fields[i].type_node->resolved_type;
            if (!ft) {
                ft = resolve_generic_type(ctx, fields->fields[i].type_node);
                fields->fields[i].type_node->resolved_type = ft;
            }
        }
    }

    return t;
}

// Instantiate a generic function with concrete type arguments.
// Returns the resolved Type* for the instantiation.
static Type* instantiate_generic_func(CheckContext* ctx, Node* template_decl,
                                       Type** type_args, size_t type_arg_count) {
    TypeParamList* params = &template_decl->as.func_decl.type_params;
    if (type_arg_count != params->count) {
        errors_push(ctx->errors, SEVERITY_ERROR, template_decl->offset,
                    template_decl->line, template_decl->column,
                    "generic function '%.*s' expects %d type arguments, got %d",
                    (int)template_decl->as.func_decl.name_size,
                    template_decl->as.func_decl.name,
                    (int)params->count, (int)type_arg_count);
        return NULL;
    }

    // dedup
    GenericInst* existing = find_generic_inst(ctx->mod, template_decl, type_args, type_arg_count);
    if (existing) return existing->resolved_type;

    // build substitution and mangled name
    TypeSubst subst = build_subst(ctx->arena, params, type_args, type_arg_count);
    size_t mangled_size;
    char* mangled = build_mangled_name(ctx->arena,
        template_decl->as.func_decl.name,
        template_decl->as.func_decl.name_size,
        type_args, type_arg_count, &mangled_size);

    // deep-copy the template
    Node* mono = deep_copy_node(ctx->arena, template_decl, &subst);
    mono->as.func_decl.name = mangled;
    mono->as.func_decl.name_size = mangled_size;

    // resolve function type
    ParamList* mono_params = &mono->as.func_decl.params;
    int param_count = (int)mono_params->count;
    Type** param_types = NULL;
    if (param_count > 0) {
        param_types = arena_alloc(ctx->arena, sizeof(Type*) * param_count);
        for (int i = 0; i < param_count; i++) {
            Type* pt = (Type*)mono_params->params[i].type_node->resolved_type;
            if (!pt) {
                pt = resolve_generic_type(ctx, mono_params->params[i].type_node);
            }
            param_types[i] = pt;
        }
    }
    Type* ret = NULL;
    if (mono->as.func_decl.return_type) {
        ret = (Type*)mono->as.func_decl.return_type->resolved_type;
        if (!ret) {
            ret = resolve_generic_type(ctx, mono->as.func_decl.return_type);
        }
    } else {
        ret = type_void(ctx->reg);
    }

    Type* func_t = type_func(ctx->reg, param_types, param_count, ret);
    mono->resolved_type = func_t;

    // register the instantiation
    GenericInst* inst = arena_alloc(ctx->arena, sizeof(GenericInst));
    inst->next = NULL;
    inst->template_decl = template_decl;
    inst->type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
    memcpy(inst->type_args, type_args, sizeof(Type*) * type_arg_count);
    inst->type_arg_count = type_arg_count;
    inst->mangled_name = mangled;
    inst->mangled_name_size = mangled_size;
    inst->mono_decl = mono;
    inst->resolved_type = func_t;
    generic_inst_add(ctx->arena, ctx->mod, inst);

    // add a symbol for the monomorphized function
    symbol_add(ctx->arena, ctx->mod->symbols, SYMBOL_FUNC, mangled, mangled_size,
               template_decl->as.func_decl.is_export, mono);

    // type-check the monomorphized body
    check_func_body(ctx, mono);

    return func_t;
}

// Instantiate a generic method with concrete type arguments.
// Similar to instantiate_generic_func but injects a `self` parameter and
// sets self_type during body checking. The monomorphized method becomes a
// standalone function (SYMBOL_FUNC) with mangled name struct__method__TypeArgs.
static Type* instantiate_generic_method(CheckContext* ctx, Node* template_decl,
                                         Type* struct_type, Type** type_args,
                                         size_t type_arg_count) {
    TypeParamList* params = &template_decl->as.func_decl.type_params;
    if (type_arg_count != params->count) {
        errors_push(ctx->errors, SEVERITY_ERROR, template_decl->offset,
                    template_decl->line, template_decl->column,
                    "generic method '%.*s' expects %d type arguments, got %d",
                    (int)template_decl->as.func_decl.name_size,
                    template_decl->as.func_decl.name,
                    (int)params->count, (int)type_arg_count);
        return NULL;
    }

    // dedup
    GenericInst* existing = find_generic_inst(ctx->mod, template_decl, type_args, type_arg_count);
    if (existing) return existing->resolved_type;

    // build substitution map
    TypeSubst subst = build_subst(ctx->arena, params, type_args, type_arg_count);

    // build mangled name: struct_name__method_name__TypeArgs
    // First build "struct__method" base name
    char* sname = struct_type->as.struct_type.name;
    size_t sname_size = struct_type->as.struct_type.name_size;
    char* mname = template_decl->as.func_decl.name;
    size_t mname_size = template_decl->as.func_decl.name_size;
    size_t base_size = sname_size + 2 + mname_size; // struct__method
    char* base = arena_alloc(ctx->arena, base_size + 1);
    memcpy(base, sname, sname_size);
    base[sname_size] = '_';
    base[sname_size + 1] = '_';
    memcpy(base + sname_size + 2, mname, mname_size);
    base[base_size] = '\0';

    size_t mangled_size;
    char* mangled = build_mangled_name(ctx->arena, base, base_size,
                                        type_args, type_arg_count, &mangled_size);

    // deep-copy the template with type substitutions
    Node* mono = deep_copy_node(ctx->arena, template_decl, &subst);
    mono->as.func_decl.name = mangled;
    mono->as.func_decl.name_size = mangled_size;
    mono->as.func_decl.method_of = struct_type; // mark as monomorphized method

    // Build function type: (params...) -> return_type
    // The type matches the method's declared params (no self).
    // Codegen will add self to the C signature separately.
    ParamList* mono_params = &mono->as.func_decl.params;
    int param_count = (int)mono_params->count;
    Type** param_types = NULL;
    if (param_count > 0) {
        param_types = arena_alloc(ctx->arena, sizeof(Type*) * param_count);
        for (int i = 0; i < param_count; i++) {
            Type* pt = (Type*)mono_params->params[i].type_node->resolved_type;
            if (!pt) {
                pt = resolve_generic_type(ctx, mono_params->params[i].type_node);
            }
            param_types[i] = pt;
        }
    }

    Type* ret = NULL;
    if (mono->as.func_decl.return_type) {
        ret = (Type*)mono->as.func_decl.return_type->resolved_type;
        if (!ret) {
            ret = resolve_generic_type(ctx, mono->as.func_decl.return_type);
        }
    } else {
        ret = type_void(ctx->reg);
    }

    Type* func_t = type_func(ctx->reg, param_types, param_count, ret);
    mono->resolved_type = func_t;

    // register the instantiation
    GenericInst* inst = arena_alloc(ctx->arena, sizeof(GenericInst));
    inst->next = NULL;
    inst->template_decl = template_decl;
    inst->type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
    memcpy(inst->type_args, type_args, sizeof(Type*) * type_arg_count);
    inst->type_arg_count = type_arg_count;
    inst->mangled_name = mangled;
    inst->mangled_name_size = mangled_size;
    inst->mono_decl = mono;
    inst->resolved_type = func_t;
    generic_inst_add(ctx->arena, ctx->mod, inst);

    // add a symbol for the monomorphized function
    symbol_add(ctx->arena, ctx->mod->symbols, SYMBOL_FUNC, mangled, mangled_size,
               false, mono);

    // type-check the monomorphized body with self_type set
    Type* self_ref = type_ref(ctx->reg, struct_type);
    Type* prev_self = ctx->self_type;
    ctx->self_type = self_ref;
    check_func_body(ctx, mono);
    ctx->self_type = prev_self;

    return func_t;
}

// Infer type arguments for a generic function from call arguments.
// Returns an arena-allocated array of Type*, or NULL on failure.
static Type** infer_type_args(CheckContext* ctx, Node* template_decl,
                               NodeList* call_args, size_t* out_count) {
    TypeParamList* type_params = &template_decl->as.func_decl.type_params;
    size_t param_count = type_params->count;
    *out_count = param_count;

    Type** inferred = arena_alloc(ctx->arena, sizeof(Type*) * param_count);
    for (size_t i = 0; i < param_count; i++) inferred[i] = NULL;

    // match each function param's type node against the argument's resolved type
    ParamList* func_params = &template_decl->as.func_decl.params;
    for (size_t i = 0; i < func_params->count && i < call_args->count; i++) {
        Type* arg_type = (Type*)call_args->nodes[i]->resolved_type;
        if (!arg_type) continue;
        Node* param_type_node = func_params->params[i].type_node;
        if (!param_type_node) continue;

        // if param type is a simple name matching a type param, bind it
        if (param_type_node->type == NODE_TYPE_SIMPLE) {
            for (size_t j = 0; j < param_count; j++) {
                if (type_params->params[j].name_size == param_type_node->as.type_simple.name_size &&
                    memcmp(type_params->params[j].name, param_type_node->as.type_simple.name,
                           param_type_node->as.type_simple.name_size) == 0) {
                    if (!inferred[j]) {
                        inferred[j] = arg_type;
                    }
                    break;
                }
            }
        }
        // if param type is *T, and arg is *X, infer T=X
        if (param_type_node->type == NODE_TYPE_POINTER && arg_type->kind == TYPE_PTR) {
            Node* inner = param_type_node->as.type_ptr.inner;
            if (inner && inner->type == NODE_TYPE_SIMPLE) {
                for (size_t j = 0; j < param_count; j++) {
                    if (type_params->params[j].name_size == inner->as.type_simple.name_size &&
                        memcmp(type_params->params[j].name, inner->as.type_simple.name,
                               inner->as.type_simple.name_size) == 0) {
                        if (!inferred[j]) inferred[j] = arg_type->as.ptr_type.inner;
                        break;
                    }
                }
            }
        }
        // if param type is &T, and arg is &X, infer T=X
        if (param_type_node->type == NODE_TYPE_REFERENCE && arg_type->kind == TYPE_REF) {
            Node* inner = param_type_node->as.type_ref.inner;
            if (inner && inner->type == NODE_TYPE_SIMPLE) {
                for (size_t j = 0; j < param_count; j++) {
                    if (type_params->params[j].name_size == inner->as.type_simple.name_size &&
                        memcmp(type_params->params[j].name, inner->as.type_simple.name,
                               inner->as.type_simple.name_size) == 0) {
                        if (!inferred[j]) inferred[j] = arg_type->as.ref_type.inner;
                        break;
                    }
                }
            }
        }
    }

    // check all params were inferred
    for (size_t i = 0; i < param_count; i++) {
        if (!inferred[i]) {
            errors_push(ctx->errors, SEVERITY_ERROR,
                        template_decl->offset, template_decl->line, template_decl->column,
                        "cannot infer type parameter '%.*s'",
                        (int)type_params->params[i].name_size, type_params->params[i].name);
            return NULL;
        }
    }
    return inferred;
}

// resolve_type_node wrapper that handles generic type args (e.g. List[int], *List[int])
static Type* resolve_generic_type(CheckContext* ctx, Node* type_node) {
    if (!type_node) return type_void(ctx->reg);

    switch (type_node->type) {
    case NODE_TYPE_SIMPLE:
        if (type_node->as.type_simple.type_args.count > 0) {
            char* name = type_node->as.type_simple.name;
            size_t name_size = type_node->as.type_simple.name_size;
            Symbol* sym = symbol_find(ctx->mod->symbols, name, name_size);
            if (!sym || sym->kind != SYMBOL_STRUCT || !sym->node ||
                sym->node->as.struct_decl.type_params.count == 0) {
                errors_push(ctx->errors, SEVERITY_ERROR, type_node->offset,
                            type_node->line, type_node->column,
                            "'%.*s' is not a generic struct", (int)name_size, name);
                return NULL;
            }
            NodeList* targs = &type_node->as.type_simple.type_args;
            size_t type_arg_count = targs->count;
            Type** type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
            for (size_t i = 0; i < type_arg_count; i++) {
                type_args[i] = resolve_generic_type(ctx, targs->nodes[i]);
                if (!type_args[i]) return NULL;
            }
            Type* t = instantiate_generic_struct(ctx, sym->node, type_args, type_arg_count);
            type_node->resolved_type = t;
            return t;
        }
        break;
    case NODE_TYPE_POINTER: {
        Type* inner = resolve_generic_type(ctx, type_node->as.type_ptr.inner);
        if (!inner) return NULL;
        return type_ptr(ctx->reg, inner);
    }
    case NODE_TYPE_REFERENCE: {
        Type* inner = resolve_generic_type(ctx, type_node->as.type_ref.inner);
        if (!inner) return NULL;
        return type_ref(ctx->reg, inner);
    }
    case NODE_TYPE_ARRAY: {
        Type* element = resolve_generic_type(ctx, type_node->as.type_array.inner);
        if (!element) return NULL;
        Node* size_node = type_node->as.type_array.size_expr;
        if (!size_node || size_node->type != NODE_INTEGER_LITERAL) {
            errors_push(ctx->errors, SEVERITY_ERROR, type_node->offset,
                        type_node->line, type_node->column,
                        "array size must be an integer literal");
            return NULL;
        }
        int arr_size = 0;
        for (size_t i = 0; i < size_node->as.integer_literal.value_size; i++) {
            arr_size = arr_size * 10 + (size_node->as.integer_literal.value[i] - '0');
        }
        return type_array(ctx->reg, element, arr_size);
    }
    case NODE_TYPE_SLICE: {
        Type* element = resolve_generic_type(ctx, type_node->as.type_slice.inner);
        if (!element) return NULL;
        return type_slice(ctx->reg, element);
    }
    default:
        break;
    }
    return resolve_type_node(ctx->reg, ctx->errors, ctx->mod->symbols, type_node);
}

static Type* check_expr(CheckContext* ctx, Node* node) {
    if (!node) return NULL;

    Type* result = NULL;

    switch (node->type) {

    case NODE_INTEGER_LITERAL:
        result = type_int(ctx->reg);
        break;

    case NODE_FLOAT_LITERAL: {
        // 3.14f → float, 6.28 → double
        char* val = node->as.float_literal.value;
        size_t val_size = node->as.float_literal.value_size;
        if (val_size > 0 && (val[val_size - 1] == 'f' || val[val_size - 1] == 'F')) {
            result = type_float(ctx->reg);
        } else {
            result = type_double(ctx->reg);
        }
        break;
    }

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
        } else if (op == TOKEN_STAR) {
            if (operand->kind == TYPE_PTR) {
                result = operand->as.ptr_type.inner;
            } else if (operand->kind == TYPE_REF) {
                result = operand->as.ref_type.inner;
            } else {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "cannot dereference type '%s' (expected pointer or reference)", type_name(operand));
            }
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

            // Handle empty struct literal: StructName() parsed as call expr
            if (sym->kind == SYMBOL_STRUCT && node->as.call_expr.args.count == 0) {
                Type* st = get_symbol_type(sym);
                if (st && st->kind == TYPE_STRUCT) {
                    result = st;
                    // Rewrite node to struct literal for codegen
                    node->type = NODE_STRUCT_LITERAL;
                    node->as.struct_literal.struct_name = callee->as.identifier.name;
                    node->as.struct_literal.struct_name_size = callee->as.identifier.name_size;
                    memset(&node->as.struct_literal.fields, 0, sizeof(FieldInitList));
                    memset(&node->as.struct_literal.type_args, 0, sizeof(NodeList));
                    break;
                }
            }

            // Handle generic function calls
            if (sym->kind == SYMBOL_FUNC && sym->node &&
                sym->node->as.func_decl.type_params.count > 0) {
                NodeList* explicit_type_args = &node->as.call_expr.type_args;
                NodeList* call_args = &node->as.call_expr.args;

                // Type-check arguments first (needed for inference)
                for (size_t i = 0; i < call_args->count; i++) {
                    check_expr(ctx, call_args->nodes[i]);
                }

                Type** type_args = NULL;
                size_t type_arg_count = 0;

                if (explicit_type_args->count > 0) {
                    // Explicit type args: max[int](1, 2)
                    type_arg_count = explicit_type_args->count;
                    type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
                    for (size_t i = 0; i < type_arg_count; i++) {
                        type_args[i] = resolve_type_node(ctx->reg, ctx->errors,
                                                          ctx->mod->symbols,
                                                          explicit_type_args->nodes[i]);
                    }
                } else {
                    // Infer type args from arguments: max(1, 2) -> T=int
                    type_args = infer_type_args(ctx, sym->node, call_args, &type_arg_count);
                }

                if (!type_args) break;

                callee_type = instantiate_generic_func(ctx, sym->node, type_args, type_arg_count);
                if (!callee_type) break;

                // Update callee to point to the monomorphized function
                GenericInst* inst = find_generic_inst(ctx->mod, sym->node, type_args, type_arg_count);
                if (inst) {
                    callee->as.identifier.name = inst->mangled_name;
                    callee->as.identifier.name_size = inst->mangled_name_size;
                }
                callee->resolved_type = callee_type;
                // fall through to arg type-checking (for interface satisfaction, etc.)
            } else {
                callee_type = get_symbol_type(sym);
                callee->resolved_type = callee_type;
            }
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
                bool compatible = false;
                // allow &Struct where &Interface is expected (with satisfaction check)
                Type* param_iface = unwrap_to_interface(param_type);
                Type* arg_struct = unwrap_to_struct(arg_type);
                if (param_iface && arg_struct) {
                    if (check_interface_satisfaction(ctx, arg_struct, param_iface)) {
                        compatible = true;
                        impl_pair_add(ctx->mod, arg_struct, param_iface,
                                      arg_struct->as.struct_type.module);
                    } else {
                        errors_push(ctx->errors, SEVERITY_ERROR, args->nodes[i]->offset,
                                    args->nodes[i]->line, args->nodes[i]->column,
                                    "struct '%s' does not satisfy interface '%s'",
                                    type_name(arg_struct), type_name(param_iface));
                        compatible = true; // already reported
                    }
                }
                // allow *void (null) where any pointer is expected
                if (arg_type->kind == TYPE_PTR && arg_type->as.ptr_type.inner->kind == TYPE_VOID &&
                    param_type->kind == TYPE_PTR) {
                    compatible = true;
                }
                // allow any *T or &T where *void is expected
                if (param_type->kind == TYPE_PTR && param_type->as.ptr_type.inner->kind == TYPE_VOID &&
                    (arg_type->kind == TYPE_PTR || arg_type->kind == TYPE_REF)) {
                    compatible = true;
                }
                // allow &T where *T is expected (references are always valid)
                if (arg_type->kind == TYPE_REF && param_type->kind == TYPE_PTR &&
                    type_equals(arg_type->as.ref_type.inner, param_type->as.ptr_type.inner)) {
                    compatible = true;
                }
                // allow integer literal coercion to any integer type
                if (type_is_integer(param_type) && type_is_integer(arg_type) &&
                    args->nodes[i]->type == NODE_INTEGER_LITERAL) {
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
        // check for enum variant access: EnumName.Variant
        Node* fa_obj = node->as.field_access.object;
        if (fa_obj && fa_obj->type == NODE_IDENTIFIER) {
            Symbol* sym = scope_lookup(ctx, fa_obj->as.identifier.name,
                                        fa_obj->as.identifier.name_size);
            if (sym && sym->kind == SYMBOL_ENUM) {
                Type* enum_type = get_symbol_type(sym);
                if (enum_type && enum_type->kind == TYPE_ENUM) {
                    char* vname = node->as.field_access.field_name;
                    size_t vname_size = node->as.field_access.field_name_size;
                    EnumVariantList* variants = enum_type->as.enum_type.variants;
                    bool found = false;
                    for (size_t i = 0; i < variants->count; i++) {
                        if (variants->variants[i].name_size == vname_size &&
                            memcmp(variants->variants[i].name, vname, vname_size) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                                    "no variant '%.*s' on enum '%.*s'",
                                    (int)vname_size, vname,
                                    (int)enum_type->as.enum_type.name_size, enum_type->as.enum_type.name);
                    }
                    fa_obj->resolved_type = enum_type;
                    result = enum_type;
                    break;
                }
            }
        }

        Type* obj_type = check_expr(ctx, node->as.field_access.object);
        if (!obj_type) break;

        char* field_name = node->as.field_access.field_name;
        size_t field_name_size = node->as.field_access.field_name_size;

        // string fat pointer fields: .ptr and .len
        if (obj_type->kind == TYPE_STRING) {
            if (field_name_size == 3 && memcmp(field_name, "ptr", 3) == 0) {
                result = type_ptr(ctx->reg, type_byte(ctx->reg));
            } else if (field_name_size == 3 && memcmp(field_name, "len", 3) == 0) {
                result = type_usize(ctx->reg);
            } else {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "no field '%.*s' on type 'string'",
                            (int)field_name_size, field_name);
            }
            break;
        }

        // array fields: .len and .ptr
        if (obj_type->kind == TYPE_ARRAY) {
            if (field_name_size == 3 && memcmp(field_name, "len", 3) == 0) {
                result = type_usize(ctx->reg);
            } else if (field_name_size == 3 && memcmp(field_name, "ptr", 3) == 0) {
                result = type_ptr(ctx->reg, obj_type->as.array_type.element);
            } else {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "no field '%.*s' on array type",
                            (int)field_name_size, field_name);
            }
            break;
        }

        // slice fields: .len and .ptr
        if (obj_type->kind == TYPE_SLICE) {
            if (field_name_size == 3 && memcmp(field_name, "len", 3) == 0) {
                result = type_usize(ctx->reg);
            } else if (field_name_size == 3 && memcmp(field_name, "ptr", 3) == 0) {
                result = type_ptr(ctx->reg, obj_type->as.slice_type.element);
            } else {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "no field '%.*s' on slice type",
                            (int)field_name_size, field_name);
            }
            break;
        }

        Type* struct_type = unwrap_to_struct(obj_type);
        if (!struct_type) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot access field on type '%s'", type_name(obj_type));
            break;
        }

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

        char* method_name = node->as.method_call.method_name;
        size_t method_name_size = node->as.method_call.method_name_size;

        // try struct first, then interface
        Type* struct_type = unwrap_to_struct(obj_type);
        Type* iface_type = struct_type ? NULL : unwrap_to_interface(obj_type);

        Node* method_node = NULL;

        if (struct_type) {
            NodeList* methods = struct_type->as.struct_type.methods;
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
        } else if (iface_type) {
            NodeList* sigs = iface_type->as.interface_type.method_sigs;
            for (size_t i = 0; i < sigs->count; i++) {
                Node* m = sigs->nodes[i];
                if (m->type == NODE_FUNC_DECL &&
                    m->as.func_decl.name_size == method_name_size &&
                    memcmp(m->as.func_decl.name, method_name, method_name_size) == 0) {
                    method_node = m;
                    break;
                }
            }
            if (!method_node) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "no method '%.*s' on interface '%s'",
                            (int)method_name_size, method_name, type_name(iface_type));
                break;
            }
        } else {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot call method on type '%s'", type_name(obj_type));
            break;
        }

        // Handle generic method: monomorphize and rewrite as function call
        if (method_node->as.func_decl.type_params.count > 0 && struct_type) {
            NodeList* explicit_type_args = &node->as.method_call.type_args;
            NodeList* call_args = &node->as.method_call.args;

            // Type-check arguments first (needed for inference)
            for (size_t i = 0; i < call_args->count; i++) {
                check_expr(ctx, call_args->nodes[i]);
            }

            Type** type_args = NULL;
            size_t type_arg_count = 0;

            if (explicit_type_args->count > 0) {
                type_arg_count = explicit_type_args->count;
                type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
                for (size_t i = 0; i < type_arg_count; i++) {
                    type_args[i] = resolve_type_node(ctx->reg, ctx->errors,
                                                      ctx->mod->symbols,
                                                      explicit_type_args->nodes[i]);
                }
            } else {
                type_args = infer_type_args(ctx, method_node, call_args, &type_arg_count);
            }

            if (!type_args) break;

            // Check arg count against method params
            ParamList* params = &method_node->as.func_decl.params;
            if (call_args->count != params->count) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "method '%.*s' expects %d arguments, got %d",
                            (int)method_name_size, method_name,
                            (int)params->count, (int)call_args->count);
                break;
            }

            Type* mono_type = instantiate_generic_method(ctx, method_node,
                                                          struct_type, type_args, type_arg_count);
            if (!mono_type) break;

            // Find the instantiation to get the mangled name
            GenericInst* inst = find_generic_inst(ctx->mod, method_node, type_args, type_arg_count);
            if (inst) {
                node->as.method_call.method_name = inst->mangled_name;
                node->as.method_call.method_name_size = inst->mangled_name_size;
            }
            node->as.method_call.is_mono = true;

            result = mono_type->as.func_type.return_type;
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

        // Handle generic struct instantiation: Pair[int, float](...)
        Type* st = NULL;
        NodeList* type_args_list = &node->as.struct_literal.type_args;
        if (type_args_list->count > 0 && sym->kind == SYMBOL_STRUCT && sym->node &&
            sym->node->as.struct_decl.type_params.count > 0) {
            size_t type_arg_count = type_args_list->count;
            Type** type_args = arena_alloc(ctx->arena, sizeof(Type*) * type_arg_count);
            for (size_t i = 0; i < type_arg_count; i++) {
                type_args[i] = resolve_type_node(ctx->reg, ctx->errors,
                                                  ctx->mod->symbols,
                                                  type_args_list->nodes[i]);
                if (!type_args[i]) { st = NULL; break; }
            }
            if (type_args[0]) {
                st = instantiate_generic_struct(ctx, sym->node, type_args, type_arg_count);
            }
            if (!st) break;
            // Update the node's struct_name to the mangled name so codegen works
            GenericInst* inst = find_generic_inst(ctx->mod, sym->node, type_args, type_arg_count);
            if (inst) {
                node->as.struct_literal.struct_name = inst->mangled_name;
                node->as.struct_literal.struct_name_size = inst->mangled_name_size;
            }
        } else {
            st = get_symbol_type(sym);
        }

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
                            // allow pointer coercions
                            bool compatible = false;
                            if (val_type->kind == TYPE_PTR && val_type->as.ptr_type.inner->kind == TYPE_VOID &&
                                field_type->kind == TYPE_PTR) {
                                compatible = true;
                            }
                            if (field_type->kind == TYPE_PTR && field_type->as.ptr_type.inner->kind == TYPE_VOID &&
                                (val_type->kind == TYPE_PTR || val_type->kind == TYPE_REF)) {
                                compatible = true;
                            }
                            if (!compatible) {
                                errors_push(ctx->errors, SEVERITY_ERROR, fi->offset, fi->line, fi->column,
                                            "field '%.*s': expected '%s', got '%s'",
                                            (int)fi->name_size, fi->name,
                                            type_name(field_type), type_name(val_type));
                            }
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

    case NODE_CAST_EXPR: {
        Type* from = check_expr(ctx, node->as.cast_expr.expr);
        Type* to = resolve_type_node(ctx->reg, ctx->errors,
                                      ctx->mod->symbols,
                                      node->as.cast_expr.target_type);
        node->as.cast_expr.target_type->resolved_type = to;
        if (from && to) {
            bool allowed = false;
            if (type_is_numeric(from) && type_is_numeric(to)) allowed = true;
            if (from->kind == TYPE_REF && to->kind == TYPE_REF) allowed = true;
            if (from->kind == TYPE_PTR && to->kind == TYPE_PTR) allowed = true;
            if (from->kind == TYPE_ENUM && type_is_integer(to)) allowed = true;
            if (type_is_integer(from) && to->kind == TYPE_ENUM) allowed = true;
            if (!allowed) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "cannot cast '%s' to '%s'", type_name(from), type_name(to));
            }
        }
        result = to;
        break;
    }

    case NODE_SIZEOF_EXPR: {
        Type* t = resolve_generic_type(ctx, node->as.sizeof_expr.type_node);
        if (!t) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "unknown type in sizeof");
        } else {
            node->as.sizeof_expr.type_node->resolved_type = t;
        }
        result = type_usize(ctx->reg);
        break;
    }

    case NODE_ARRAY_LITERAL: {
        NodeList* elems = &node->as.array_literal.elements;
        if (elems->count == 0) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "array literal cannot be empty");
            break;
        }
        Type* elem_type = check_expr(ctx, elems->nodes[0]);
        if (!elem_type) break;
        for (size_t i = 1; i < elems->count; i++) {
            Type* t = check_expr(ctx, elems->nodes[i]);
            if (t && !type_equals(t, elem_type)) {
                errors_push(ctx->errors, SEVERITY_ERROR, elems->nodes[i]->offset,
                            elems->nodes[i]->line, elems->nodes[i]->column,
                            "array element type mismatch: expected '%s', got '%s'",
                            type_name(elem_type), type_name(t));
            }
        }
        result = type_array(ctx->reg, elem_type, (int)elems->count);
        break;
    }

    case NODE_INDEX_EXPR: {
        Type* obj_type = check_expr(ctx, node->as.index_expr.object);
        Type* idx_type = check_expr(ctx, node->as.index_expr.index);
        if (!obj_type) break;
        if (idx_type && !type_is_integer(idx_type)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->as.index_expr.index->offset,
                        node->as.index_expr.index->line, node->as.index_expr.index->column,
                        "index must be an integer type, got '%s'", type_name(idx_type));
        }
        if (obj_type->kind == TYPE_ARRAY) {
            result = obj_type->as.array_type.element;
        } else if (obj_type->kind == TYPE_SLICE) {
            result = obj_type->as.slice_type.element;
        } else {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot index type '%s'", type_name(obj_type));
        }
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
            declared_type = resolve_generic_type(ctx, node->as.var_decl.type_node);
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
            bool compatible = false;
            // allow null (*void) assigned to any pointer type
            if (init_type->kind == TYPE_PTR && init_type->as.ptr_type.inner->kind == TYPE_VOID &&
                declared_type->kind == TYPE_PTR) {
                compatible = true;
            }
            // allow any *T or &T assigned to *void
            if (declared_type->kind == TYPE_PTR && declared_type->as.ptr_type.inner->kind == TYPE_VOID &&
                (init_type->kind == TYPE_PTR || init_type->kind == TYPE_REF)) {
                compatible = true;
            }
            // allow integer conversions (C99-style)
            if (type_is_integer(declared_type) && type_is_integer(init_type)) {
                // integer literals can be assigned to any integer type
                if (node->as.var_decl.value &&
                    node->as.var_decl.value->type == NODE_INTEGER_LITERAL) {
                    compatible = true;
                }
                // widening conversions (smaller -> larger rank) always allowed
                else if (type_integer_convertible(init_type, declared_type)) {
                    compatible = true;
                }
            }
            // allow &Struct where &Interface is expected (with satisfaction check)
            Type* decl_iface = unwrap_to_interface(declared_type);
            Type* init_struct = unwrap_to_struct(init_type);
            if (decl_iface && init_struct) {
                if (check_interface_satisfaction(ctx, init_struct, decl_iface)) {
                    compatible = true;
                    impl_pair_add(ctx->mod, init_struct, decl_iface,
                                  init_struct->as.struct_type.module);
                } else {
                    errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                                "struct '%s' does not satisfy interface '%s'",
                                type_name(init_struct), type_name(decl_iface));
                    compatible = true; // already reported
                }
            }
            // allow &T -> *T (references are always valid, safe to use as pointer)
            if (declared_type->kind == TYPE_PTR && init_type->kind == TYPE_REF) {
                if (type_equals(declared_type->as.ptr_type.inner, init_type->as.ref_type.inner)) {
                    compatible = true;
                }
            }
            // allow array-to-slice conversion: T[N] -> T[]
            if (declared_type->kind == TYPE_SLICE && init_type->kind == TYPE_ARRAY) {
                if (type_equals(declared_type->as.slice_type.element,
                                init_type->as.array_type.element)) {
                    compatible = true;
                }
            }
            if (!compatible) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "variable '%.*s': declared as '%s' but initialized with '%s'",
                            (int)node->as.var_decl.name_size, node->as.var_decl.name,
                            type_name(declared_type), type_name(init_type));
            }
        }

        if (ctx->scope) {
            Symbol* dup = symbol_find(&ctx->scope->locals, node->as.var_decl.name, node->as.var_decl.name_size);
            if (dup) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "duplicate variable '%.*s' in this scope",
                            (int)node->as.var_decl.name_size, node->as.var_decl.name);
                break;
            }
        }
        scope_add(ctx, SYMBOL_VAR, node->as.var_decl.name, node->as.var_decl.name_size, var_type, node);
        break;
    }

    case NODE_CONST_DECL: {
        Type* declared_type = NULL;
        if (node->as.const_decl.type_node) {
            declared_type = resolve_generic_type(ctx, node->as.const_decl.type_node);
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

        if (ctx->scope) {
            Symbol* dup = symbol_find(&ctx->scope->locals, node->as.const_decl.name, node->as.const_decl.name_size);
            if (dup) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "duplicate variable '%.*s' in this scope",
                            (int)node->as.const_decl.name_size, node->as.const_decl.name);
                break;
            }
        }
        scope_add(ctx, SYMBOL_CONST, node->as.const_decl.name, node->as.const_decl.name_size, const_type, node);
        break;
    }

    case NODE_RETURN_STMT: {
        if (node->as.return_stmt.value) {
            Type* val = check_expr(ctx, node->as.return_stmt.value);
            if (ctx->return_type && ctx->return_type->kind == TYPE_VOID) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "return with value in void function");
            } else if (val && ctx->return_type) {
                if (!type_equals(val, ctx->return_type)) {
                    // allow integer conversions (C99-style)
                    bool compatible = false;
                    if (type_is_integer(val) && type_is_integer(ctx->return_type)) {
                        // integer literals can convert to any integer type
                        if (node->as.return_stmt.value->type == NODE_INTEGER_LITERAL) {
                            compatible = true;
                        }
                        // widening conversions always allowed
                        else if (type_integer_convertible(val, ctx->return_type)) {
                            compatible = true;
                        }
                    }
                    // allow *void coercions in returns
                    if (val->kind == TYPE_PTR && val->as.ptr_type.inner->kind == TYPE_VOID &&
                        ctx->return_type->kind == TYPE_PTR) {
                        compatible = true;
                    }
                    if (ctx->return_type->kind == TYPE_PTR && ctx->return_type->as.ptr_type.inner->kind == TYPE_VOID &&
                        (val->kind == TYPE_PTR || val->kind == TYPE_REF)) {
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
        if (cond && cond->kind != TYPE_BOOL && cond->kind != TYPE_PTR) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "if condition must be bool or pointer, got '%s'", type_name(cond));
        }
        check_body(ctx, &node->as.if_stmt.then_body);

        ElseIfList* elseifs = &node->as.if_stmt.elseifs;
        for (size_t i = 0; i < elseifs->count; i++) {
            Type* ei_cond = check_expr(ctx, elseifs->branches[i].condition);
            if (ei_cond && ei_cond->kind != TYPE_BOOL && ei_cond->kind != TYPE_PTR) {
                errors_push(ctx->errors, SEVERITY_ERROR,
                            elseifs->branches[i].offset, elseifs->branches[i].line,
                            elseifs->branches[i].column,
                            "elseif condition must be bool or pointer, got '%s'", type_name(ei_cond));
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
        Type* step_type = NULL;
        if (node->as.for_stmt.step) {
            step_type = check_expr(ctx, node->as.for_stmt.step);
        }

        if (start_type && !type_is_integer(start_type)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->as.for_stmt.start->offset,
                        node->as.for_stmt.start->line, node->as.for_stmt.start->column,
                        "for-loop start must be an integer type, got '%s'", type_name(start_type));
        }
        if (end_type && !type_is_integer(end_type)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->as.for_stmt.end->offset,
                        node->as.for_stmt.end->line, node->as.for_stmt.end->column,
                        "for-loop end must be an integer type, got '%s'", type_name(end_type));
        }
        if (step_type && !type_is_integer(step_type)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->as.for_stmt.step->offset,
                        node->as.for_stmt.step->line, node->as.for_stmt.step->column,
                        "for-loop step must be an integer type, got '%s'", type_name(step_type));
        }

        // determine iterator type from start expression
        Type* iter_type = start_type ? start_type : type_int(ctx->reg);

        Scope* prev = ctx->scope;
        scope_push(ctx);
        scope_add(ctx, SYMBOL_VAR, node->as.for_stmt.var_name, node->as.for_stmt.var_name_size,
                  iter_type, NULL);
        ctx->loop_depth++;
        ctx->real_loop_depth++;
        check_body(ctx, &node->as.for_stmt.body);
        ctx->real_loop_depth--;
        ctx->loop_depth--;
        scope_pop(ctx, prev);
        break;
    }

    case NODE_WHILE_STMT: {
        Type* cond = check_expr(ctx, node->as.while_stmt.condition);
        if (cond && cond->kind != TYPE_BOOL && cond->kind != TYPE_PTR) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "while condition must be bool or pointer, got '%s'", type_name(cond));
        }
        ctx->loop_depth++;
        ctx->real_loop_depth++;
        check_body(ctx, &node->as.while_stmt.body);
        ctx->real_loop_depth--;
        ctx->loop_depth--;
        break;
    }

    case NODE_BREAK_STMT:
        if (ctx->loop_depth == 0) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "'break' used outside of loop or match");
        }
        break;

    case NODE_CONTINUE_STMT:
        if (ctx->real_loop_depth == 0) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "'continue' used outside of loop");
        }
        break;

    case NODE_MATCH_STMT: {
        Type* subject = check_expr(ctx, node->as.match_stmt.subject);
        MatchCaseList* cases = &node->as.match_stmt.cases;
        ctx->loop_depth++;
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

        // check for duplicate case values (O(n^2) — case counts are small)
        for (size_t i = 0; i < cases->count; i++) {
            NodeList* vals_i = &cases->cases[i].values;
            for (size_t vi = 0; vi < vals_i->count; vi++) {
                Node* a = vals_i->nodes[vi];
                // compare against all subsequent case values
                for (size_t j = i; j < cases->count; j++) {
                    NodeList* vals_j = &cases->cases[j].values;
                    size_t vj_start = (j == i) ? vi + 1 : 0;
                    for (size_t vj = vj_start; vj < vals_j->count; vj++) {
                        Node* b = vals_j->nodes[vj];
                        if (a->type != b->type) continue;
                        bool dup = false;
                        if (a->type == NODE_INTEGER_LITERAL &&
                            a->as.integer_literal.value_size == b->as.integer_literal.value_size &&
                            memcmp(a->as.integer_literal.value, b->as.integer_literal.value,
                                   a->as.integer_literal.value_size) == 0) {
                            dup = true;
                        }
                        if (a->type == NODE_BOOL_LITERAL &&
                            a->as.bool_literal.value == b->as.bool_literal.value) {
                            dup = true;
                        }
                        if (a->type == NODE_STRING_LITERAL &&
                            a->as.string_literal.value_size == b->as.string_literal.value_size &&
                            memcmp(a->as.string_literal.value, b->as.string_literal.value,
                                   a->as.string_literal.value_size) == 0) {
                            dup = true;
                        }
                        if (dup) {
                            errors_push(ctx->errors, SEVERITY_ERROR,
                                        b->offset, b->line, b->column,
                                        "duplicate match case value");
                        }
                    }
                }
            }
        }

        if (node->as.match_stmt.else_body.count > 0) {
            check_body(ctx, &node->as.match_stmt.else_body);
        }
        ctx->loop_depth--;
        break;
    }

    case NODE_ASSIGN_STMT: {
        if (!is_lvalue(node->as.assign_stmt.target)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot assign to this expression");
        }
        if (node->as.assign_stmt.target->type == NODE_IDENTIFIER) {
            Symbol* sym = scope_lookup(ctx,
                node->as.assign_stmt.target->as.identifier.name,
                node->as.assign_stmt.target->as.identifier.name_size);
            if (sym && sym->kind == SYMBOL_CONST) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "cannot assign to constant '%.*s'",
                            (int)node->as.assign_stmt.target->as.identifier.name_size,
                            node->as.assign_stmt.target->as.identifier.name);
            }
        }
        Type* target = check_expr(ctx, node->as.assign_stmt.target);
        Type* value = check_expr(ctx, node->as.assign_stmt.value);
        if (target && value && !type_equals(target, value)) {
            bool compatible = false;
            // allow null assigned to pointer
            if (value->kind == TYPE_PTR && value->as.ptr_type.inner->kind == TYPE_VOID &&
                target->kind == TYPE_PTR) {
                compatible = true;
            }
            // allow any *T or &T assigned to *void
            if (target->kind == TYPE_PTR && target->as.ptr_type.inner->kind == TYPE_VOID &&
                (value->kind == TYPE_PTR || value->kind == TYPE_REF)) {
                compatible = true;
            }
            // allow &T assigned to *T (taking address into pointer)
            if (value->kind == TYPE_REF && target->kind == TYPE_PTR) {
                compatible = true;
            }
            // allow integer widening conversions and literal assignments
            if (type_is_integer(target) && type_is_integer(value)) {
                if (node->as.assign_stmt.value->type == NODE_INTEGER_LITERAL) {
                    compatible = true;
                } else if (type_integer_convertible(value, target)) {
                    compatible = true;
                }
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
        if (!is_lvalue(node->as.compound_assign_stmt.target)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "cannot assign to this expression");
        }
        if (node->as.compound_assign_stmt.target->type == NODE_IDENTIFIER) {
            Symbol* sym = scope_lookup(ctx,
                node->as.compound_assign_stmt.target->as.identifier.name,
                node->as.compound_assign_stmt.target->as.identifier.name_size);
            if (sym && sym->kind == SYMBOL_CONST) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "cannot assign to constant '%.*s'",
                            (int)node->as.compound_assign_stmt.target->as.identifier.name_size,
                            node->as.compound_assign_stmt.target->as.identifier.name);
            }
        }
        Type* target = check_expr(ctx, node->as.compound_assign_stmt.target);
        Type* value = check_expr(ctx, node->as.compound_assign_stmt.value);
        if (target && !type_is_numeric(target)) {
            errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                        "compound assignment target must be numeric, got '%s'", type_name(target));
        }
        if (target && value && !type_equals(target, value)) {
            bool compatible = false;
            if (type_is_integer(target) && type_is_integer(value)) {
                if (node->as.compound_assign_stmt.value->type == NODE_INTEGER_LITERAL) {
                    compatible = true;
                } else if (type_integer_convertible(value, target)) {
                    compatible = true;
                }
            }
            if (!compatible) {
                errors_push(ctx->errors, SEVERITY_ERROR, node->offset, node->line, node->column,
                            "compound assignment type mismatch: '%s' vs '%s'",
                            type_name(target), type_name(value));
            }
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
        scope_add(ctx, SYMBOL_VAR, params->params[i].name, params->params[i].name_size, param_type, NULL);
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
        if (method->as.func_decl.type_params.count > 0) continue; // skip generic methods

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
    ctx.loop_depth = 0;
    ctx.real_loop_depth = 0;

    for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
        if (!sym->node) continue;

        switch (sym->kind) {
        case SYMBOL_FUNC:
            if (sym->node->as.func_decl.type_params.count > 0) break;
            if (sym->node->as.func_decl.is_extern) break;
            check_func_body(&ctx, sym->node);
            break;
        case SYMBOL_STRUCT:
            if (sym->node->as.struct_decl.type_params.count > 0) break;
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
