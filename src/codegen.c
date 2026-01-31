#include "codegen.h"
#include "sema.h"
#include "type.h"
#include "lexer.h"
#include "fs.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// CodeGen context
// ---------------------------------------------------------------------------

typedef struct CodeGen {
    Arena* arena;
    Errors* errors;
    Package* pkg;
    Module* mod;
    FILE* c_file;
    FILE* h_file;
    int indent;
    bool in_method;
    char* struct_name;
    size_t struct_name_size;
} CodeGen;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void emit_indent(CodeGen* gen, FILE* f) {
    for (int i = 0; i < gen->indent; i++) {
        fprintf(f, "    ");
    }
}

static void emit_mangled(CodeGen* gen, FILE* f, char* name, size_t name_size) {
    fprintf(f, "anc__%s__%s__%.*s", gen->pkg->name, gen->mod->name,
            (int)name_size, name);
}

static void emit_method_mangled(CodeGen* gen, FILE* f,
                                 char* sname, size_t sname_size,
                                 char* mname, size_t mname_size) {
    fprintf(f, "anc__%s__%s__%.*s__%.*s", gen->pkg->name, gen->mod->name,
            (int)sname_size, sname, (int)mname_size, mname);
}

// Emit mangled interface name: anc__{pkg}__{mod}__{InterfaceName}
static void emit_iface_mangled(CodeGen* gen, FILE* f, Type* iface) {
    fprintf(f, "anc__%s__%s__%.*s", gen->pkg->name, gen->mod->name,
            (int)iface->as.interface_type.name_size, iface->as.interface_type.name);
}

static void emit_type(CodeGen* gen, FILE* f, Type* type) {
    if (!type) { fprintf(f, "void"); return; }

    switch (type->kind) {
    case TYPE_VOID:   fprintf(f, "void"); break;
    case TYPE_BOOL:   fprintf(f, "bool"); break;
    case TYPE_BYTE:   fprintf(f, "uint8_t"); break;
    case TYPE_SHORT:  fprintf(f, "int16_t"); break;
    case TYPE_USHORT: fprintf(f, "uint16_t"); break;
    case TYPE_INT:    fprintf(f, "int32_t"); break;
    case TYPE_UINT:   fprintf(f, "uint32_t"); break;
    case TYPE_LONG:   fprintf(f, "int64_t"); break;
    case TYPE_ULONG:  fprintf(f, "uint64_t"); break;
    case TYPE_ISIZE:  fprintf(f, "ptrdiff_t"); break;
    case TYPE_USIZE:  fprintf(f, "size_t"); break;
    case TYPE_FLOAT:  fprintf(f, "float"); break;
    case TYPE_DOUBLE: fprintf(f, "double"); break;
    case TYPE_STRING: fprintf(f, "anc__string"); break;
    case TYPE_STRUCT:
        emit_mangled(gen, f, type->as.struct_type.name, type->as.struct_type.name_size);
        break;
    case TYPE_INTERFACE:
        emit_iface_mangled(gen, f, type);
        fprintf(f, "__ref");
        break;
    case TYPE_FUNC:
        // shouldn't appear as a C type directly
        fprintf(f, "void*");
        break;
    case TYPE_REF:
        if (type->as.ref_type.inner && type->as.ref_type.inner->kind == TYPE_INTERFACE) {
            emit_iface_mangled(gen, f, type->as.ref_type.inner);
            fprintf(f, "__ref");
        } else {
            emit_type(gen, f, type->as.ref_type.inner);
            fprintf(f, "*");
        }
        break;
    case TYPE_PTR:
        if (type->as.ptr_type.inner && type->as.ptr_type.inner->kind == TYPE_INTERFACE) {
            emit_iface_mangled(gen, f, type->as.ptr_type.inner);
            fprintf(f, "__ref*");
        } else {
            emit_type(gen, f, type->as.ptr_type.inner);
            fprintf(f, "*");
        }
        break;
    }
}

// Get the resolved type from a type node, falling back to resolved_type on the node
static Type* get_type(Node* node) {
    if (!node) return NULL;
    return (Type*)node->resolved_type;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static void emit_expr(CodeGen* gen, FILE* f, Node* node);
static void emit_stmt(CodeGen* gen, FILE* f, Node* node);
static void emit_body(CodeGen* gen, FILE* f, NodeList* body);

// ---------------------------------------------------------------------------
// Expression emitter
// ---------------------------------------------------------------------------

static void emit_expr(CodeGen* gen, FILE* f, Node* node) {
    if (!node) return;

    switch (node->type) {

    case NODE_INTEGER_LITERAL:
        fprintf(f, "%.*s", (int)node->as.integer_literal.value_size,
                node->as.integer_literal.value);
        break;

    case NODE_FLOAT_LITERAL:
        fprintf(f, "%.*s", (int)node->as.float_literal.value_size,
                node->as.float_literal.value);
        break;

    case NODE_STRING_LITERAL: {
        char* val = node->as.string_literal.value;
        size_t val_size = node->as.string_literal.value_size;
        size_t str_len = val_size - 2;  // subtract both quote chars
        fprintf(f, "(anc__string){ .ptr = (uint8_t*)%.*s, .len = %zu }",
                (int)val_size, val, str_len);
        break;
    }

    case NODE_BOOL_LITERAL:
        fprintf(f, node->as.bool_literal.value ? "true" : "false");
        break;

    case NODE_NULL_LITERAL:
        fprintf(f, "NULL");
        break;

    case NODE_IDENTIFIER: {
        char* name = node->as.identifier.name;
        size_t name_size = node->as.identifier.name_size;

        // check if this is a module-level symbol (needs mangling)
        Symbol* sym = symbol_find(gen->mod->symbols, name, name_size);
        if (sym && sym->kind != SYMBOL_IMPORT) {
            // module-level symbol — mangle it
            emit_mangled(gen, f, name, name_size);
        } else if (sym && sym->kind == SYMBOL_IMPORT && sym->source) {
            // imported symbol — use source module's mangling
            Module* saved = gen->mod;
            gen->mod = sym->source;
            emit_mangled(gen, f, name, name_size);
            gen->mod = saved;
        } else {
            // local variable — no mangling
            fprintf(f, "%.*s", (int)name_size, name);
        }
        break;
    }

    case NODE_SELF:
        fprintf(f, "self");
        break;

    case NODE_BINARY_EXPR: {
        TokenType op = node->as.binary_expr.op;
        const char* op_str = NULL;
        switch (op) {
        case TOKEN_PLUS:                   op_str = " + "; break;
        case TOKEN_MINUS:                  op_str = " - "; break;
        case TOKEN_STAR:                   op_str = " * "; break;
        case TOKEN_SLASH:                  op_str = " / "; break;
        case TOKEN_CARET:                  op_str = " ^ "; break;
        case TOKEN_EQUAL:                  op_str = " == "; break;
        case TOKEN_NOT_EQUAL:              op_str = " != "; break;
        case TOKEN_LESS_THAN:              op_str = " < "; break;
        case TOKEN_GREATER_THAN:           op_str = " > "; break;
        case TOKEN_LESS_THAN_OR_EQUAL:     op_str = " <= "; break;
        case TOKEN_GREATER_THAN_OR_EQUAL:  op_str = " >= "; break;
        case TOKEN_AND:                    op_str = " && "; break;
        case TOKEN_OR:                     op_str = " || "; break;
        default:                           op_str = " ? "; break;
        }
        emit_expr(gen, f, node->as.binary_expr.left);
        fprintf(f, "%s", op_str);
        emit_expr(gen, f, node->as.binary_expr.right);
        break;
    }

    case NODE_UNARY_EXPR: {
        TokenType op = node->as.unary_expr.op;
        if (op == TOKEN_MINUS) {
            fprintf(f, "-");
        } else if (op == TOKEN_AMPERSAND) {
            fprintf(f, "&");
        } else if (op == TOKEN_NOT) {
            fprintf(f, "!");
        }
        emit_expr(gen, f, node->as.unary_expr.operand);
        break;
    }

    case NODE_PAREN_EXPR:
        fprintf(f, "(");
        emit_expr(gen, f, node->as.paren_expr.inner);
        fprintf(f, ")");
        break;

    case NODE_CALL_EXPR: {
        // get the callee's function type to check for interface params
        Node* callee = node->as.call_expr.callee;
        Type* callee_type = get_type(callee);

        emit_expr(gen, f, callee);
        fprintf(f, "(");
        NodeList* args = &node->as.call_expr.args;
        for (size_t i = 0; i < args->count; i++) {
            if (i > 0) fprintf(f, ", ");

            // check if this arg needs fat pointer wrapping
            Type* arg_type = get_type(args->nodes[i]);
            Type* param_type = NULL;
            if (callee_type && callee_type->kind == TYPE_FUNC &&
                (int)i < callee_type->as.func_type.param_count) {
                param_type = callee_type->as.func_type.param_types[i];
            }

            // detect &Struct passed where &Interface expected
            Type* param_iface = NULL;
            Type* arg_struct = NULL;
            if (param_type && arg_type) {
                // unwrap ref/ptr to get inner types
                if (param_type->kind == TYPE_REF && param_type->as.ref_type.inner &&
                    param_type->as.ref_type.inner->kind == TYPE_INTERFACE) {
                    param_iface = param_type->as.ref_type.inner;
                }
                if (arg_type->kind == TYPE_REF && arg_type->as.ref_type.inner &&
                    arg_type->as.ref_type.inner->kind == TYPE_STRUCT) {
                    arg_struct = arg_type->as.ref_type.inner;
                }
            }

            if (param_iface && arg_struct) {
                // emit fat pointer: (Interface__ref){ .data = <arg_expr>, .vtable = &struct__iface__vtable }
                fprintf(f, "(");
                emit_iface_mangled(gen, f, param_iface);
                fprintf(f, "__ref){ .data = ");
                emit_expr(gen, f, args->nodes[i]);
                fprintf(f, ", .vtable = &");
                // vtable instance name: anc__{pkg}__{mod}__{Struct}__{Interface}__vtable
                // use the struct's module for mangling
                Module* saved = gen->mod;
                gen->mod = arg_struct->as.struct_type.module;
                emit_mangled(gen, f, arg_struct->as.struct_type.name,
                             arg_struct->as.struct_type.name_size);
                gen->mod = saved;
                fprintf(f, "__%.*s__vtable }",
                        (int)param_iface->as.interface_type.name_size,
                        param_iface->as.interface_type.name);
            } else {
                emit_expr(gen, f, args->nodes[i]);
            }
        }
        fprintf(f, ")");
        break;
    }

    case NODE_FIELD_ACCESS: {
        Type* obj_type = get_type(node->as.field_access.object);
        bool is_ptr = obj_type && (obj_type->kind == TYPE_REF || obj_type->kind == TYPE_PTR);

        emit_expr(gen, f, node->as.field_access.object);
        fprintf(f, "%s%.*s", is_ptr ? "->" : ".",
                (int)node->as.field_access.field_name_size,
                node->as.field_access.field_name);
        break;
    }

    case NODE_METHOD_CALL: {
        Node* object = node->as.method_call.object;
        Type* obj_type = get_type(object);
        Type* inner_type = NULL;

        if (obj_type) {
            if (obj_type->kind == TYPE_STRUCT || obj_type->kind == TYPE_INTERFACE)
                inner_type = obj_type;
            else if (obj_type->kind == TYPE_REF) inner_type = obj_type->as.ref_type.inner;
            else if (obj_type->kind == TYPE_PTR) inner_type = obj_type->as.ptr_type.inner;
        }

        if (inner_type && inner_type->kind == TYPE_INTERFACE) {
            // vtable dispatch: obj.vtable->method(obj.data, args...)
            emit_expr(gen, f, object);
            fprintf(f, ".vtable->%.*s(",
                    (int)node->as.method_call.method_name_size,
                    node->as.method_call.method_name);
            emit_expr(gen, f, object);
            fprintf(f, ".data");
            NodeList* args = &node->as.method_call.args;
            for (size_t i = 0; i < args->count; i++) {
                fprintf(f, ", ");
                emit_expr(gen, f, args->nodes[i]);
            }
            fprintf(f, ")");
        } else if (inner_type && inner_type->kind == TYPE_STRUCT) {
            emit_method_mangled(gen, f,
                inner_type->as.struct_type.name, inner_type->as.struct_type.name_size,
                node->as.method_call.method_name, node->as.method_call.method_name_size);
            fprintf(f, "(");
            // first arg is &object (or object if already a pointer)
            bool is_ptr = obj_type && (obj_type->kind == TYPE_REF || obj_type->kind == TYPE_PTR);
            if (!is_ptr) fprintf(f, "&");
            emit_expr(gen, f, object);
            NodeList* args = &node->as.method_call.args;
            for (size_t i = 0; i < args->count; i++) {
                fprintf(f, ", ");
                emit_expr(gen, f, args->nodes[i]);
            }
            fprintf(f, ")");
        } else {
            // fallback
            fprintf(f, "%.*s(", (int)node->as.method_call.method_name_size,
                    node->as.method_call.method_name);
            bool is_ptr = obj_type && (obj_type->kind == TYPE_REF || obj_type->kind == TYPE_PTR);
            if (!is_ptr) fprintf(f, "&");
            emit_expr(gen, f, object);
            NodeList* args = &node->as.method_call.args;
            for (size_t i = 0; i < args->count; i++) {
                fprintf(f, ", ");
                emit_expr(gen, f, args->nodes[i]);
            }
            fprintf(f, ")");
        }
        break;
    }

    case NODE_STRUCT_LITERAL: {
        char* name = node->as.struct_literal.struct_name;
        size_t name_size = node->as.struct_literal.struct_name_size;

        fprintf(f, "(");
        emit_mangled(gen, f, name, name_size);
        fprintf(f, "){ ");

        FieldInitList* inits = &node->as.struct_literal.fields;
        for (size_t i = 0; i < inits->count; i++) {
            if (i > 0) fprintf(f, ", ");
            fprintf(f, ".%.*s = ", (int)inits->inits[i].name_size, inits->inits[i].name);
            emit_expr(gen, f, inits->inits[i].value);
        }
        fprintf(f, " }");
        break;
    }

    default:
        fprintf(f, "/* unsupported expr %d */", node->type);
        break;
    }
}

// ---------------------------------------------------------------------------
// Statement emitter
// ---------------------------------------------------------------------------

static void emit_stmt(CodeGen* gen, FILE* f, Node* node) {
    if (!node) return;

    switch (node->type) {

    case NODE_VAR_DECL: {
        Type* var_type = get_type(node);
        emit_indent(gen, f);
        emit_type(gen, f, var_type);
        fprintf(f, " %.*s", (int)node->as.var_decl.name_size, node->as.var_decl.name);
        if (node->as.var_decl.value) {
            fprintf(f, " = ");
            emit_expr(gen, f, node->as.var_decl.value);
        }
        fprintf(f, ";\n");
        break;
    }

    case NODE_CONST_DECL: {
        Type* const_type = get_type(node);
        emit_indent(gen, f);
        fprintf(f, "const ");
        emit_type(gen, f, const_type);
        fprintf(f, " %.*s", (int)node->as.const_decl.name_size, node->as.const_decl.name);
        if (node->as.const_decl.value) {
            fprintf(f, " = ");
            emit_expr(gen, f, node->as.const_decl.value);
        }
        fprintf(f, ";\n");
        break;
    }

    case NODE_RETURN_STMT:
        emit_indent(gen, f);
        if (node->as.return_stmt.value) {
            fprintf(f, "return ");
            emit_expr(gen, f, node->as.return_stmt.value);
            fprintf(f, ";\n");
        } else {
            fprintf(f, "return;\n");
        }
        break;

    case NODE_IF_STMT: {
        emit_indent(gen, f);
        fprintf(f, "if (");
        emit_expr(gen, f, node->as.if_stmt.condition);
        fprintf(f, ") {\n");
        gen->indent++;
        emit_body(gen, f, &node->as.if_stmt.then_body);
        gen->indent--;

        ElseIfList* elseifs = &node->as.if_stmt.elseifs;
        for (size_t i = 0; i < elseifs->count; i++) {
            emit_indent(gen, f);
            fprintf(f, "} else if (");
            emit_expr(gen, f, elseifs->branches[i].condition);
            fprintf(f, ") {\n");
            gen->indent++;
            emit_body(gen, f, &elseifs->branches[i].body);
            gen->indent--;
        }

        if (node->as.if_stmt.else_body.count > 0) {
            emit_indent(gen, f);
            fprintf(f, "} else {\n");
            gen->indent++;
            emit_body(gen, f, &node->as.if_stmt.else_body);
            gen->indent--;
        }

        emit_indent(gen, f);
        fprintf(f, "}\n");
        break;
    }

    case NODE_FOR_STMT: {
        Type* iter_type = get_type(node->as.for_stmt.start);
        if (!iter_type) iter_type = get_type(node);

        emit_indent(gen, f);
        fprintf(f, "for (");
        emit_type(gen, f, iter_type);
        fprintf(f, " %.*s = ", (int)node->as.for_stmt.var_name_size, node->as.for_stmt.var_name);
        emit_expr(gen, f, node->as.for_stmt.start);
        fprintf(f, "; %.*s < ", (int)node->as.for_stmt.var_name_size, node->as.for_stmt.var_name);
        emit_expr(gen, f, node->as.for_stmt.end);
        fprintf(f, "; %.*s += ", (int)node->as.for_stmt.var_name_size, node->as.for_stmt.var_name);
        if (node->as.for_stmt.step) {
            emit_expr(gen, f, node->as.for_stmt.step);
        } else {
            fprintf(f, "1");
        }
        fprintf(f, ") {\n");
        gen->indent++;
        emit_body(gen, f, &node->as.for_stmt.body);
        gen->indent--;
        emit_indent(gen, f);
        fprintf(f, "}\n");
        break;
    }

    case NODE_WHILE_STMT:
        emit_indent(gen, f);
        fprintf(f, "while (");
        emit_expr(gen, f, node->as.while_stmt.condition);
        fprintf(f, ") {\n");
        gen->indent++;
        emit_body(gen, f, &node->as.while_stmt.body);
        gen->indent--;
        emit_indent(gen, f);
        fprintf(f, "}\n");
        break;

    case NODE_BREAK_STMT:
        emit_indent(gen, f);
        fprintf(f, "break;\n");
        break;

    case NODE_MATCH_STMT: {
        emit_indent(gen, f);
        fprintf(f, "switch (");
        emit_expr(gen, f, node->as.match_stmt.subject);
        fprintf(f, ") {\n");

        MatchCaseList* cases = &node->as.match_stmt.cases;
        for (size_t i = 0; i < cases->count; i++) {
            MatchCase* mc = &cases->cases[i];
            // emit case labels
            for (size_t j = 0; j < mc->values.count; j++) {
                emit_indent(gen, f);
                fprintf(f, "case ");
                emit_expr(gen, f, mc->values.nodes[j]);
                fprintf(f, ":\n");
            }
            // emit body
            gen->indent++;
            emit_body(gen, f, &mc->body);
            emit_indent(gen, f);
            fprintf(f, "break;\n");
            gen->indent--;
        }

        if (node->as.match_stmt.else_body.count > 0) {
            emit_indent(gen, f);
            fprintf(f, "default:\n");
            gen->indent++;
            emit_body(gen, f, &node->as.match_stmt.else_body);
            emit_indent(gen, f);
            fprintf(f, "break;\n");
            gen->indent--;
        }

        emit_indent(gen, f);
        fprintf(f, "}\n");
        break;
    }

    case NODE_ASSIGN_STMT:
        emit_indent(gen, f);
        emit_expr(gen, f, node->as.assign_stmt.target);
        fprintf(f, " = ");
        emit_expr(gen, f, node->as.assign_stmt.value);
        fprintf(f, ";\n");
        break;

    case NODE_COMPOUND_ASSIGN_STMT: {
        const char* op_str = "?=";
        switch (node->as.compound_assign_stmt.op) {
        case TOKEN_PLUS_ASSIGN:  op_str = "+="; break;
        case TOKEN_MINUS_ASSIGN: op_str = "-="; break;
        case TOKEN_STAR_ASSIGN:  op_str = "*="; break;
        case TOKEN_SLASH_ASSIGN: op_str = "/="; break;
        default: break;
        }
        emit_indent(gen, f);
        emit_expr(gen, f, node->as.compound_assign_stmt.target);
        fprintf(f, " %s ", op_str);
        emit_expr(gen, f, node->as.compound_assign_stmt.value);
        fprintf(f, ";\n");
        break;
    }

    case NODE_EXPR_STMT:
        emit_indent(gen, f);
        emit_expr(gen, f, node->as.expr_stmt.expr);
        fprintf(f, ";\n");
        break;

    default:
        emit_indent(gen, f);
        fprintf(f, "/* unsupported stmt %d */\n", node->type);
        break;
    }
}

static void emit_body(CodeGen* gen, FILE* f, NodeList* body) {
    for (size_t i = 0; i < body->count; i++) {
        emit_stmt(gen, f, body->nodes[i]);
    }
}

// ---------------------------------------------------------------------------
// Function signature emitter (shared by .h decl and .c definition)
// ---------------------------------------------------------------------------

static void emit_func_signature(CodeGen* gen, FILE* f, Node* func_node, bool is_static) {
    Type* func_type = get_type(func_node);
    if (!func_type || func_type->kind != TYPE_FUNC) return;

    if (is_static) fprintf(f, "static ");
    emit_type(gen, f, func_type->as.func_type.return_type);
    fprintf(f, " ");
    emit_mangled(gen, f, func_node->as.func_decl.name, func_node->as.func_decl.name_size);
    fprintf(f, "(");

    ParamList* params = &func_node->as.func_decl.params;
    if (params->count == 0) {
        fprintf(f, "void");
    } else {
        for (size_t i = 0; i < params->count; i++) {
            if (i > 0) fprintf(f, ", ");
            emit_type(gen, f, func_type->as.func_type.param_types[i]);
            fprintf(f, " %.*s", (int)params->params[i].name_size, params->params[i].name);
        }
    }
    fprintf(f, ")");
}

static void emit_method_signature(CodeGen* gen, FILE* f, Node* method_node,
                                    char* sname, size_t sname_size, bool is_static) {
    Type* func_type = get_type(method_node);
    if (!func_type || func_type->kind != TYPE_FUNC) return;

    if (is_static) fprintf(f, "static ");
    emit_type(gen, f, func_type->as.func_type.return_type);
    fprintf(f, " ");
    emit_method_mangled(gen, f, sname, sname_size,
                        method_node->as.func_decl.name, method_node->as.func_decl.name_size);
    fprintf(f, "(");

    // self parameter
    emit_mangled(gen, f, sname, sname_size);
    fprintf(f, "* self");

    // other parameters
    ParamList* params = &method_node->as.func_decl.params;
    for (size_t i = 0; i < params->count; i++) {
        fprintf(f, ", ");
        emit_type(gen, f, func_type->as.func_type.param_types[i]);
        fprintf(f, " %.*s", (int)params->params[i].name_size, params->params[i].name);
    }
    fprintf(f, ")");
}

// ---------------------------------------------------------------------------
// Interface vtable emission
// ---------------------------------------------------------------------------

// Emit vtable struct typedef and fat pointer ref typedef for an interface
static void emit_interface_typedefs(CodeGen* gen, FILE* f, Type* iface) {
    NodeList* sigs = iface->as.interface_type.method_sigs;

    // vtable struct
    fprintf(f, "typedef struct ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__vtable {\n");
    for (size_t i = 0; i < sigs->count; i++) {
        Node* sig = sigs->nodes[i];
        if (sig->type != NODE_FUNC_DECL) continue;
        Type* sig_type = get_type(sig);
        fprintf(f, "    ");
        // return type
        if (sig_type && sig_type->kind == TYPE_FUNC) {
            emit_type(gen, f, sig_type->as.func_type.return_type);
        } else {
            fprintf(f, "void");
        }
        fprintf(f, " (*%.*s)(void* self",
                (int)sig->as.func_decl.name_size, sig->as.func_decl.name);
        // extra params
        if (sig_type && sig_type->kind == TYPE_FUNC) {
            for (int j = 0; j < sig_type->as.func_type.param_count; j++) {
                fprintf(f, ", ");
                emit_type(gen, f, sig_type->as.func_type.param_types[j]);
                fprintf(f, " %.*s",
                        (int)sig->as.func_decl.params.params[j].name_size,
                        sig->as.func_decl.params.params[j].name);
            }
        }
        fprintf(f, ");\n");
    }
    fprintf(f, "} ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__vtable;\n\n");

    // fat pointer ref struct
    fprintf(f, "typedef struct ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__ref {\n");
    fprintf(f, "    void* data;\n");
    fprintf(f, "    ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__vtable* vtable;\n");
    fprintf(f, "} ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__ref;\n\n");
}

// Emit wrapper functions and vtable instance for a (struct, interface) pair
static void emit_vtable_instance(CodeGen* gen, FILE* f, ImplPair* pair) {
    Type* st = pair->struct_type;
    Type* iface = pair->interface_type;
    NodeList* sigs = iface->as.interface_type.method_sigs;

    // use the struct's module for mangling the wrapper and struct method names
    Module* saved = gen->mod;
    gen->mod = pair->struct_module;

    // emit wrapper functions
    for (size_t i = 0; i < sigs->count; i++) {
        Node* sig = sigs->nodes[i];
        if (sig->type != NODE_FUNC_DECL) continue;
        Type* sig_type = get_type(sig);

        fprintf(f, "static ");
        if (sig_type && sig_type->kind == TYPE_FUNC) {
            emit_type(gen, f, sig_type->as.func_type.return_type);
        } else {
            fprintf(f, "void");
        }
        fprintf(f, " ");
        emit_mangled(gen, f, st->as.struct_type.name, st->as.struct_type.name_size);
        fprintf(f, "__%.*s__wrapper(void* self",
                (int)sig->as.func_decl.name_size, sig->as.func_decl.name);
        if (sig_type && sig_type->kind == TYPE_FUNC) {
            for (int j = 0; j < sig_type->as.func_type.param_count; j++) {
                fprintf(f, ", ");
                emit_type(gen, f, sig_type->as.func_type.param_types[j]);
                fprintf(f, " %.*s",
                        (int)sig->as.func_decl.params.params[j].name_size,
                        sig->as.func_decl.params.params[j].name);
            }
        }
        fprintf(f, ") {\n");
        fprintf(f, "    return ");
        emit_method_mangled(gen, f,
            st->as.struct_type.name, st->as.struct_type.name_size,
            sig->as.func_decl.name, sig->as.func_decl.name_size);
        fprintf(f, "((");
        emit_mangled(gen, f, st->as.struct_type.name, st->as.struct_type.name_size);
        fprintf(f, "*)self");
        if (sig_type && sig_type->kind == TYPE_FUNC) {
            for (int j = 0; j < sig_type->as.func_type.param_count; j++) {
                fprintf(f, ", %.*s",
                        (int)sig->as.func_decl.params.params[j].name_size,
                        sig->as.func_decl.params.params[j].name);
            }
        }
        fprintf(f, ");\n");
        fprintf(f, "}\n\n");
    }

    // emit vtable instance
    gen->mod = saved; // use current module for interface name
    fprintf(f, "static ");
    emit_iface_mangled(gen, f, iface);
    fprintf(f, "__vtable ");
    gen->mod = pair->struct_module; // use struct module for struct name
    emit_mangled(gen, f, st->as.struct_type.name, st->as.struct_type.name_size);
    gen->mod = saved;
    fprintf(f, "__%.*s__vtable = {\n",
            (int)iface->as.interface_type.name_size, iface->as.interface_type.name);

    for (size_t i = 0; i < sigs->count; i++) {
        Node* sig = sigs->nodes[i];
        if (sig->type != NODE_FUNC_DECL) continue;
        fprintf(f, "    .%.*s = ",
                (int)sig->as.func_decl.name_size, sig->as.func_decl.name);
        gen->mod = pair->struct_module;
        emit_mangled(gen, f, st->as.struct_type.name, st->as.struct_type.name_size);
        gen->mod = saved;
        fprintf(f, "__%.*s__wrapper",
                (int)sig->as.func_decl.name_size, sig->as.func_decl.name);
        fprintf(f, ",\n");
    }

    fprintf(f, "};\n\n");
}

// ---------------------------------------------------------------------------
// .h file generation
// ---------------------------------------------------------------------------

static void emit_h_file(CodeGen* gen) {
    FILE* f = gen->h_file;

    // include guard
    fprintf(f, "#ifndef ANC__%s__%s_H\n", gen->pkg->name, gen->mod->name);
    fprintf(f, "#define ANC__%s__%s_H\n\n", gen->pkg->name, gen->mod->name);

    // standard includes
    fprintf(f, "#include <stdint.h>\n");
    fprintf(f, "#include <stdbool.h>\n");
    fprintf(f, "#include <stddef.h>\n\n");

    // anc__string fat pointer typedef (guarded to avoid redefinition across headers)
    fprintf(f, "#ifndef ANC__STRING_DEFINED\n");
    fprintf(f, "#define ANC__STRING_DEFINED\n");
    fprintf(f, "typedef struct anc__string {\n");
    fprintf(f, "    uint8_t* ptr;\n");
    fprintf(f, "    size_t len;\n");
    fprintf(f, "} anc__string;\n");
    fprintf(f, "#endif\n\n");

    // pass 1: exported struct typedefs
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_STRUCT || !sym->is_export || !sym->node) continue;

        Node* node = sym->node;
        fprintf(f, "typedef struct ");
        emit_mangled(gen, f, node->as.struct_decl.name, node->as.struct_decl.name_size);
        fprintf(f, " {\n");

        FieldList* fields = &node->as.struct_decl.fields;
        for (size_t i = 0; i < fields->count; i++) {
            Field* field = &fields->fields[i];
            Type* ft = (Type*)field->type_node->resolved_type;
            if (!ft) {
                // resolve on the fly if needed
                // just use the type_node for now
            }
            fprintf(f, "    ");
            // emit field type from type node
            emit_type(gen, f, ft);
            fprintf(f, " %.*s;\n", (int)field->name_size, field->name);
        }

        fprintf(f, "} ");
        emit_mangled(gen, f, node->as.struct_decl.name, node->as.struct_decl.name_size);
        fprintf(f, ";\n\n");

        // exported method declarations
        NodeList* methods = &node->as.struct_decl.methods;
        for (size_t i = 0; i < methods->count; i++) {
            Node* method = methods->nodes[i];
            if (method->type != NODE_FUNC_DECL) continue;
            emit_method_signature(gen, f, method,
                node->as.struct_decl.name, node->as.struct_decl.name_size, false);
            fprintf(f, ";\n");
        }
        if (methods->count > 0) fprintf(f, "\n");
    }

    // pass 2: exported extern const/var
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (!sym->is_export || !sym->node) continue;

        if (sym->kind == SYMBOL_CONST) {
            Type* t = get_type(sym->node);
            fprintf(f, "extern const ");
            emit_type(gen, f, t);
            fprintf(f, " ");
            emit_mangled(gen, f, sym->name, sym->name_size);
            fprintf(f, ";\n");
        } else if (sym->kind == SYMBOL_VAR) {
            Type* t = get_type(sym->node);
            fprintf(f, "extern ");
            emit_type(gen, f, t);
            fprintf(f, " ");
            emit_mangled(gen, f, sym->name, sym->name_size);
            fprintf(f, ";\n");
        }
    }

    // pass 3: exported function declarations
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_FUNC || !sym->is_export || !sym->node) continue;
        emit_func_signature(gen, f, sym->node, false);
        fprintf(f, ";\n");
    }

    fprintf(f, "\n#endif\n");
}

// ---------------------------------------------------------------------------
// .c file generation
// ---------------------------------------------------------------------------

static void emit_c_file(CodeGen* gen) {
    FILE* f = gen->c_file;

    // include own header
    fprintf(f, "#include \"anc__%s__%s.h\"\n", gen->pkg->name, gen->mod->name);

    // include headers for imported modules
    if (gen->mod->ast && gen->mod->ast->type == NODE_PROGRAM) {
        NodeList* decls = &gen->mod->ast->as.program.declarations;
        for (size_t i = 0; i < decls->count; i++) {
            Node* node = decls->nodes[i];
            if (node->type != NODE_IMPORT_DECL) continue;

            // find the source module for this import
            ImportNameList* names = &node->as.import_decl.names;
            if (names->count > 0) {
                Symbol* sym = symbol_find(gen->mod->symbols, names->names[0].name,
                                           names->names[0].name_size);
                if (sym && sym->kind == SYMBOL_IMPORT && sym->source) {
                    fprintf(f, "#include \"anc__%s__%s.h\"\n",
                            gen->pkg->name, sym->source->name);
                }
            }
        }
    }

    // standard includes
    fprintf(f, "\n#include <stdint.h>\n");
    fprintf(f, "#include <stdbool.h>\n");
    fprintf(f, "#include <stddef.h>\n\n");

    // pass 1: non-exported struct typedefs
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_STRUCT || sym->is_export || !sym->node) continue;

        Node* node = sym->node;
        fprintf(f, "typedef struct ");
        emit_mangled(gen, f, node->as.struct_decl.name, node->as.struct_decl.name_size);
        fprintf(f, " {\n");

        FieldList* fields = &node->as.struct_decl.fields;
        for (size_t i = 0; i < fields->count; i++) {
            Field* field = &fields->fields[i];
            Type* ft = (Type*)field->type_node->resolved_type;
            fprintf(f, "    ");
            emit_type(gen, f, ft);
            fprintf(f, " %.*s;\n", (int)field->name_size, field->name);
        }

        fprintf(f, "} ");
        emit_mangled(gen, f, node->as.struct_decl.name, node->as.struct_decl.name_size);
        fprintf(f, ";\n\n");
    }

    // interface vtable and fat pointer typedefs
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_INTERFACE || !sym->node) continue;
        Type* iface_type = get_type(sym->node);
        if (!iface_type || iface_type->kind != TYPE_INTERFACE) continue;
        emit_interface_typedefs(gen, f, iface_type);
    }

    // pass 2: static forward declarations for non-exported functions
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_FUNC || sym->is_export || !sym->node) continue;
        emit_func_signature(gen, f, sym->node, true);
        fprintf(f, ";\n");
    }
    // forward declarations for non-exported struct methods
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_STRUCT || !sym->node) continue;
        Node* snode = sym->node;
        bool is_exported_struct = sym->is_export;
        NodeList* methods = &snode->as.struct_decl.methods;
        for (size_t i = 0; i < methods->count; i++) {
            Node* method = methods->nodes[i];
            if (method->type != NODE_FUNC_DECL) continue;
            if (!is_exported_struct) {
                // non-exported struct: methods are static
                emit_method_signature(gen, f, method,
                    snode->as.struct_decl.name, snode->as.struct_decl.name_size, true);
                fprintf(f, ";\n");
            }
        }
    }
    fprintf(f, "\n");

    // vtable wrapper functions and instances
    ImplPairList* impl_pairs = &gen->mod->impl_pairs;
    for (size_t i = 0; i < impl_pairs->count; i++) {
        emit_vtable_instance(gen, f, &impl_pairs->pairs[i]);
    }

    // pass 3: const/var definitions
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (!sym->node) continue;

        if (sym->kind == SYMBOL_CONST) {
            Type* t = get_type(sym->node);
            if (sym->is_export) {
                fprintf(f, "const ");
            } else {
                fprintf(f, "static const ");
            }
            emit_type(gen, f, t);
            fprintf(f, " ");
            emit_mangled(gen, f, sym->name, sym->name_size);
            if (sym->node->as.const_decl.value) {
                fprintf(f, " = ");
                emit_expr(gen, f, sym->node->as.const_decl.value);
            }
            fprintf(f, ";\n");
        } else if (sym->kind == SYMBOL_VAR) {
            Type* t = get_type(sym->node);
            if (!sym->is_export) {
                fprintf(f, "static ");
            }
            emit_type(gen, f, t);
            fprintf(f, " ");
            emit_mangled(gen, f, sym->name, sym->name_size);
            if (sym->node->as.var_decl.value) {
                fprintf(f, " = ");
                emit_expr(gen, f, sym->node->as.var_decl.value);
            }
            fprintf(f, ";\n");
        }
    }
    fprintf(f, "\n");

    // pass 4: function definitions
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_FUNC || !sym->node) continue;

        bool is_static = !sym->is_export;
        emit_func_signature(gen, f, sym->node, is_static);
        fprintf(f, " {\n");
        gen->indent = 1;
        emit_body(gen, f, &sym->node->as.func_decl.body);
        gen->indent = 0;
        fprintf(f, "}\n\n");
    }

    // pass 5: struct method definitions
    for (Symbol* sym = gen->mod->symbols->first; sym; sym = sym->next) {
        if (sym->kind != SYMBOL_STRUCT || !sym->node) continue;

        Node* snode = sym->node;
        bool is_static = !sym->is_export;
        NodeList* methods = &snode->as.struct_decl.methods;

        for (size_t i = 0; i < methods->count; i++) {
            Node* method = methods->nodes[i];
            if (method->type != NODE_FUNC_DECL) continue;

            emit_method_signature(gen, f, method,
                snode->as.struct_decl.name, snode->as.struct_decl.name_size, is_static);
            fprintf(f, " {\n");
            gen->indent = 1;
            emit_body(gen, f, &method->as.func_decl.body);
            gen->indent = 0;
            fprintf(f, "}\n\n");
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool codegen(Arena* arena, Errors* errors, Package* pkg, ModuleGraph* graph, Module* entry, char* output_dir) {
    dir_ensure(output_dir);

    for (Module* mod = graph->first; mod; mod = mod->next) {
        if (!mod->symbols) continue;

        // build file paths
        char h_path[1024];
        char c_path[1024];
        snprintf(h_path, sizeof(h_path), "%s/anc__%s__%s.h", output_dir, pkg->name, mod->name);
        snprintf(c_path, sizeof(c_path), "%s/anc__%s__%s.c", output_dir, pkg->name, mod->name);

        FILE* h_file = fopen(h_path, "w");
        FILE* c_file = fopen(c_path, "w");

        if (!h_file || !c_file) {
            errors_push(errors, SEVERITY_ERROR, 0, 0, 0,
                        "failed to create output file for module '%s'", mod->name);
            if (h_file) fclose(h_file);
            if (c_file) fclose(c_file);
            return false;
        }

        CodeGen gen;
        gen.arena = arena;
        gen.errors = errors;
        gen.pkg = pkg;
        gen.mod = mod;
        gen.h_file = h_file;
        gen.c_file = c_file;
        gen.indent = 0;
        gen.in_method = false;
        gen.struct_name = NULL;
        gen.struct_name_size = 0;

        // resolve field types (they may not have resolved_type set yet)
        for (Symbol* sym = mod->symbols->first; sym; sym = sym->next) {
            if (sym->kind != SYMBOL_STRUCT || !sym->node) continue;
            FieldList* fields = &sym->node->as.struct_decl.fields;
            for (size_t i = 0; i < fields->count; i++) {
                if (fields->fields[i].type_node && !fields->fields[i].type_node->resolved_type) {
                    // We need the TypeRegistry here, but we don't have it.
                    // Field types should already be resolved from sema.
                    // If not, we'll handle it in emit_type via the type node.
                }
            }
        }

        emit_h_file(&gen);
        emit_c_file(&gen);

        // emit C main() wrapper in entry module
        if (mod == entry) {
            Symbol* main_sym = symbol_find(mod->symbols, "main", 4);
            if (!main_sym || main_sym->kind != SYMBOL_FUNC) {
                errors_push(errors, SEVERITY_ERROR, 0, 0, 0,
                            "entry module '%s' has no 'main' function", mod->name);
            } else {
                Type* func_type = (Type*)main_sym->node->resolved_type;
                Type* ret = (func_type && func_type->kind == TYPE_FUNC)
                            ? func_type->as.func_type.return_type : NULL;
                bool returns_int = ret && type_is_integer(ret);

                fprintf(c_file, "\nint main(void) {\n");
                if (returns_int) {
                    fprintf(c_file, "    return ");
                    emit_mangled(&gen, c_file, "main", 4);
                    fprintf(c_file, "();\n");
                } else {
                    fprintf(c_file, "    ");
                    emit_mangled(&gen, c_file, "main", 4);
                    fprintf(c_file, "();\n");
                    fprintf(c_file, "    return 0;\n");
                }
                fprintf(c_file, "}\n");
            }
        }

        fclose(h_file);
        fclose(c_file);
    }

    return true;
}
