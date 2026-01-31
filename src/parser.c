#include "parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Parser state
// ---------------------------------------------------------------------------

typedef struct Parser {
    Arena* arena;
    Errors* errors;
    Tokens* tokens;
    size_t pos;
    bool had_error;
    bool panic_mode;
} Parser;

// ---------------------------------------------------------------------------
// Token navigation helpers
// ---------------------------------------------------------------------------

static Token* peek(Parser* p) {
    return &p->tokens->tokens[p->pos];
}

static Token* advance(Parser* p) {
    Token* tok = &p->tokens->tokens[p->pos];
    if (tok->type != TOKEN_END_OF_FILE) {
        p->pos++;
    }
    return tok;
}

static bool check(Parser* p, TokenType type) {
    return peek(p)->type == type;
}

static bool match(Parser* p, TokenType type) {
    if (peek(p)->type == type) {
        advance(p);
        return true;
    }
    return false;
}

static Token* expect(Parser* p, TokenType expected, const char* message) {
    if (peek(p)->type == expected) {
        return advance(p);
    }
    Token* tok = peek(p);
    errors_push(p->errors, SEVERITY_ERROR, tok->offset, tok->line, tok->column, "%s", message);
    p->had_error = true;
    p->panic_mode = true;
    return NULL;
}

static void skip_newlines(Parser* p) {
    while (peek(p)->type == TOKEN_NEWLINE) {
        advance(p);
    }
}

static bool expect_newline(Parser* p) {
    if (peek(p)->type == TOKEN_NEWLINE || peek(p)->type == TOKEN_END_OF_FILE) {
        skip_newlines(p);
        return true;
    }
    Token* tok = peek(p);
    errors_push(p->errors, SEVERITY_ERROR, tok->offset, tok->line, tok->column,
                "Expected newline.");
    p->had_error = true;
    return false;
}

// ---------------------------------------------------------------------------
// Node allocation
// ---------------------------------------------------------------------------

static Node* make_node(Parser* p, NodeType type, Token* tok) {
    Node* node = arena_alloc(p->arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->type = type;
    if (tok) {
        node->offset = tok->offset;
        node->line = tok->line;
        node->column = tok->column;
    }
    return node;
}

// ---------------------------------------------------------------------------
// List helpers (arena-allocated, 2x doubling)
// ---------------------------------------------------------------------------

static void node_list_push(Arena* arena, NodeList* list, Node* node) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        Node** new_nodes = arena_alloc(arena, new_cap * sizeof(Node*));
        if (list->nodes) {
            memcpy(new_nodes, list->nodes, list->count * sizeof(Node*));
        }
        list->nodes = new_nodes;
        list->capacity = new_cap;
    }
    list->nodes[list->count++] = node;
}

static void param_list_push(Arena* arena, ParamList* list, Param param) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        Param* new_params = arena_alloc(arena, new_cap * sizeof(Param));
        if (list->params) {
            memcpy(new_params, list->params, list->count * sizeof(Param));
        }
        list->params = new_params;
        list->capacity = new_cap;
    }
    list->params[list->count++] = param;
}

static void field_list_push(Arena* arena, FieldList* list, Field field) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        Field* new_fields = arena_alloc(arena, new_cap * sizeof(Field));
        if (list->fields) {
            memcpy(new_fields, list->fields, list->count * sizeof(Field));
        }
        list->fields = new_fields;
        list->capacity = new_cap;
    }
    list->fields[list->count++] = field;
}

static void enum_variant_list_push(Arena* arena, EnumVariantList* list, EnumVariant variant) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        EnumVariant* new_variants = arena_alloc(arena, new_cap * sizeof(EnumVariant));
        if (list->variants) {
            memcpy(new_variants, list->variants, list->count * sizeof(EnumVariant));
        }
        list->variants = new_variants;
        list->capacity = new_cap;
    }
    list->variants[list->count++] = variant;
}

static void field_init_list_push(Arena* arena, FieldInitList* list, FieldInit init) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        FieldInit* new_inits = arena_alloc(arena, new_cap * sizeof(FieldInit));
        if (list->inits) {
            memcpy(new_inits, list->inits, list->count * sizeof(FieldInit));
        }
        list->inits = new_inits;
        list->capacity = new_cap;
    }
    list->inits[list->count++] = init;
}

static void elseif_list_push(Arena* arena, ElseIfList* list, ElseIfBranch branch) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        ElseIfBranch* new_branches = arena_alloc(arena, new_cap * sizeof(ElseIfBranch));
        if (list->branches) {
            memcpy(new_branches, list->branches, list->count * sizeof(ElseIfBranch));
        }
        list->branches = new_branches;
        list->capacity = new_cap;
    }
    list->branches[list->count++] = branch;
}

static void type_param_list_push(Arena* arena, TypeParamList* list, TypeParam param) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        TypeParam* new_params = arena_alloc(arena, new_cap * sizeof(TypeParam));
        if (list->params) {
            memcpy(new_params, list->params, list->count * sizeof(TypeParam));
        }
        list->params = new_params;
        list->capacity = new_cap;
    }
    list->params[list->count++] = param;
}

static void import_name_list_push(Arena* arena, ImportNameList* list, ImportName name) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        ImportName* new_names = arena_alloc(arena, new_cap * sizeof(ImportName));
        if (list->names) {
            memcpy(new_names, list->names, list->count * sizeof(ImportName));
        }
        list->names = new_names;
        list->capacity = new_cap;
    }
    list->names[list->count++] = name;
}

static void match_case_list_push(Arena* arena, MatchCaseList* list, MatchCase mc) {
    if (list->count >= list->capacity) {
        size_t new_cap = list->capacity < 8 ? 8 : list->capacity * 2;
        MatchCase* new_cases = arena_alloc(arena, new_cap * sizeof(MatchCase));
        if (list->cases) {
            memcpy(new_cases, list->cases, list->count * sizeof(MatchCase));
        }
        list->cases = new_cases;
        list->capacity = new_cap;
    }
    list->cases[list->count++] = mc;
}

// ---------------------------------------------------------------------------
// Error recovery
// ---------------------------------------------------------------------------

static void synchronize(Parser* p) {
    p->panic_mode = false;
    while (!check(p, TOKEN_END_OF_FILE)) {
        TokenType t = peek(p)->type;
        if (t == TOKEN_FUNC || t == TOKEN_STRUCT || t == TOKEN_INTERFACE ||
            t == TOKEN_ENUM || t == TOKEN_CONST || t == TOKEN_VAR || t == TOKEN_EXPORT ||
            t == TOKEN_END || t == TOKEN_RETURN || t == TOKEN_IF ||
            t == TOKEN_FOR || t == TOKEN_WHILE || t == TOKEN_BREAK || t == TOKEN_CONTINUE ||
            t == TOKEN_MATCH || t == TOKEN_CASE || t == TOKEN_ELSE ||
            t == TOKEN_ELSEIF) {
            return;
        }
        advance(p);
    }
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static Node* parse_expression(Parser* p);
static Node* parse_type(Parser* p);
static NodeList parse_body(Parser* p);
static Node* parse_statement(Parser* p);
static Node* parse_var_decl(Parser* p, bool is_export);
static Node* parse_const_decl(Parser* p, bool is_export);
static Node* parse_func_decl(Parser* p, bool is_export);

// ---------------------------------------------------------------------------
// Generic parameter/argument parsers
// ---------------------------------------------------------------------------

// Parses [T, K, V] at declaration sites (type parameter names)
static TypeParamList parse_type_params(Parser* p) {
    TypeParamList params = {0};
    advance(p); // consume '['
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected type parameter name.");
    if (name_tok) {
        TypeParam param = {0};
        param.name = name_tok->value;
        param.name_size = name_tok->size;
        type_param_list_push(p->arena, &params, param);

        while (match(p, TOKEN_COMMA)) {
            name_tok = expect(p, TOKEN_IDENTIFIER, "Expected type parameter name.");
            if (!name_tok) break;
            TypeParam next = {0};
            next.name = name_tok->value;
            next.name_size = name_tok->size;
            type_param_list_push(p->arena, &params, next);
        }
    }
    expect(p, TOKEN_RIGHT_BRACKET, "Expected ']' after type parameters.");
    return params;
}

// Parses [int, float] at usage sites (type arguments as type nodes)
static NodeList parse_type_args(Parser* p) {
    NodeList args = {0};
    advance(p); // consume '['
    Node* type_node = parse_type(p);
    if (type_node) node_list_push(p->arena, &args, type_node);
    while (match(p, TOKEN_COMMA)) {
        type_node = parse_type(p);
        if (type_node) node_list_push(p->arena, &args, type_node);
    }
    expect(p, TOKEN_RIGHT_BRACKET, "Expected ']' after type arguments.");
    return args;
}

// ---------------------------------------------------------------------------
// Type parser
// ---------------------------------------------------------------------------

static Node* parse_type(Parser* p) {
    if (check(p, TOKEN_AMPERSAND)) {
        Token* tok = advance(p);
        Node* inner = parse_type(p);
        Node* node = make_node(p, NODE_TYPE_REFERENCE, tok);
        node->as.type_ref.inner = inner;
        return node;
    }
    if (check(p, TOKEN_STAR)) {
        Token* tok = advance(p);
        Node* inner = parse_type(p);
        Node* node = make_node(p, NODE_TYPE_POINTER, tok);
        node->as.type_ptr.inner = inner;
        return node;
    }
    if (check(p, TOKEN_IDENTIFIER)) {
        Token* tok = advance(p);
        Node* node = make_node(p, NODE_TYPE_SIMPLE, tok);
        node->as.type_simple.name = tok->value;
        node->as.type_simple.name_size = tok->size;
        memset(&node->as.type_simple.type_args, 0, sizeof(NodeList));

        // check for array/slice/generic suffix
        if (check(p, TOKEN_LEFT_BRACKET)) {
            // Peek inside the bracket to disambiguate:
            //   T[]      -> slice
            //   T[N]     -> array (N is integer literal)
            //   T[U,...] -> generic type args
            size_t saved = p->pos;
            Token* after = &p->tokens->tokens[saved + 1];
            if (after->type == TOKEN_RIGHT_BRACKET) {
                // T[] -> slice
                Token* bracket_tok = advance(p); // consume '['
                advance(p); // consume ']'
                Node* slice_node = make_node(p, NODE_TYPE_SLICE, bracket_tok);
                slice_node->as.type_slice.inner = node;
                return slice_node;
            } else if (after->type == TOKEN_INTEGER_LITERAL) {
                // T[N] -> array
                Token* bracket_tok = advance(p); // consume '['
                Node* size_expr = parse_expression(p);
                expect(p, TOKEN_RIGHT_BRACKET, "Expected ']' after array size.");
                Node* array_node = make_node(p, NODE_TYPE_ARRAY, bracket_tok);
                array_node->as.type_array.inner = node;
                array_node->as.type_array.size_expr = size_expr;
                return array_node;
            } else {
                // Generic type arguments: List[int], Pair[int, float]
                node->as.type_simple.type_args = parse_type_args(p);
            }
        }

        return node;
    }
    Token* tok = peek(p);
    errors_push(p->errors, SEVERITY_ERROR, tok->offset, tok->line, tok->column,
                "Expected type.");
    p->had_error = true;
    p->panic_mode = true;
    return NULL;
}

// ---------------------------------------------------------------------------
// Expression parser — precedence climbing
// ---------------------------------------------------------------------------

// Level 7: primary
static Node* parse_struct_literal(Parser* p, Token* name_tok) {
    advance(p); // consume '('
    Node* node = make_node(p, NODE_STRUCT_LITERAL, name_tok);
    node->as.struct_literal.struct_name = name_tok->value;
    node->as.struct_literal.struct_name_size = name_tok->size;
    memset(&node->as.struct_literal.fields, 0, sizeof(FieldInitList));

    if (!check(p, TOKEN_RIGHT_PAREN)) {
        Token* field_tok = expect(p, TOKEN_IDENTIFIER, "Expected field name in struct literal.");
        if (!field_tok) return node;
        expect(p, TOKEN_ASSIGN, "Expected '=' after field name.");
        Node* value = parse_expression(p);
        FieldInit init = {0};
        init.name = field_tok->value;
        init.name_size = field_tok->size;
        init.value = value;
        init.offset = field_tok->offset;
        init.line = field_tok->line;
        init.column = field_tok->column;
        field_init_list_push(p->arena, &node->as.struct_literal.fields, init);

        while (match(p, TOKEN_COMMA)) {
            field_tok = expect(p, TOKEN_IDENTIFIER, "Expected field name.");
            if (!field_tok) break;
            expect(p, TOKEN_ASSIGN, "Expected '=' after field name.");
            value = parse_expression(p);
            FieldInit next = {0};
            next.name = field_tok->value;
            next.name_size = field_tok->size;
            next.value = value;
            next.offset = field_tok->offset;
            next.line = field_tok->line;
            next.column = field_tok->column;
            field_init_list_push(p->arena, &node->as.struct_literal.fields, next);
        }
    }
    expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after struct literal.");
    return node;
}

static Node* parse_argument_list(Parser* p, NodeList* args) {
    (void)args;
    return NULL; // unused, args passed by pointer below
}

static void parse_args_into(Parser* p, NodeList* args) {
    if (!check(p, TOKEN_RIGHT_PAREN)) {
        Node* arg = parse_expression(p);
        if (arg) node_list_push(p->arena, args, arg);
        while (match(p, TOKEN_COMMA)) {
            arg = parse_expression(p);
            if (arg) node_list_push(p->arena, args, arg);
        }
    }
}

static bool is_struct_literal_lookahead(Parser* p) {
    // After IDENTIFIER and seeing LEFT_PAREN at pos,
    // check if pos+1 is IDENTIFIER and pos+2 is ASSIGN
    size_t pos = p->pos;
    if (pos + 2 < p->tokens->count) {
        Token* t1 = &p->tokens->tokens[pos + 1];
        Token* t2 = &p->tokens->tokens[pos + 2];
        if (t1->type == TOKEN_IDENTIFIER && t2->type == TOKEN_ASSIGN) {
            return true;
        }
        // Also handle empty struct literal: Name()
        // That's actually a function call, not a struct literal
    }
    return false;
}

static Node* parse_primary(Parser* p) {
    Token* tok = peek(p);

    switch (tok->type) {
    case TOKEN_INTEGER_LITERAL: {
        advance(p);
        Node* node = make_node(p, NODE_INTEGER_LITERAL, tok);
        node->as.integer_literal.value = tok->value;
        node->as.integer_literal.value_size = tok->size;
        return node;
    }
    case TOKEN_FLOAT_LITERAL: {
        advance(p);
        Node* node = make_node(p, NODE_FLOAT_LITERAL, tok);
        node->as.float_literal.value = tok->value;
        node->as.float_literal.value_size = tok->size;
        return node;
    }
    case TOKEN_STRING_LITERAL: {
        advance(p);
        Node* node = make_node(p, NODE_STRING_LITERAL, tok);
        node->as.string_literal.value = tok->value;
        node->as.string_literal.value_size = tok->size;
        return node;
    }
    case TOKEN_TRUE: {
        advance(p);
        Node* node = make_node(p, NODE_BOOL_LITERAL, tok);
        node->as.bool_literal.value = true;
        return node;
    }
    case TOKEN_FALSE: {
        advance(p);
        Node* node = make_node(p, NODE_BOOL_LITERAL, tok);
        node->as.bool_literal.value = false;
        return node;
    }
    case TOKEN_NULL: {
        advance(p);
        return make_node(p, NODE_NULL_LITERAL, tok);
    }
    case TOKEN_SELF: {
        advance(p);
        return make_node(p, NODE_SELF, tok);
    }
    case TOKEN_IDENTIFIER: {
        Token* name_tok = advance(p);

        // Check for generic type args: Name[int, float](...)
        NodeList type_args = {0};
        if (check(p, TOKEN_LEFT_BRACKET)) {
            // Lookahead: scan to matching ']', check if '(' follows
            size_t scan = p->pos;
            int depth = 0;
            bool is_generic = false;
            for (; scan < p->tokens->count; scan++) {
                TokenType tt = p->tokens->tokens[scan].type;
                if (tt == TOKEN_LEFT_BRACKET) depth++;
                else if (tt == TOKEN_RIGHT_BRACKET) {
                    depth--;
                    if (depth == 0) {
                        if (scan + 1 < p->tokens->count &&
                            p->tokens->tokens[scan + 1].type == TOKEN_LEFT_PAREN)
                            is_generic = true;
                        break;
                    }
                }
                else if (tt == TOKEN_NEWLINE || tt == TOKEN_END_OF_FILE) break;
            }
            if (is_generic) {
                type_args = parse_type_args(p);
            }
        }

        if (check(p, TOKEN_LEFT_PAREN)) {
            if (is_struct_literal_lookahead(p)) {
                Node* slit = parse_struct_literal(p, name_tok);
                slit->as.struct_literal.type_args = type_args;
                return slit;
            }
            // function call
            advance(p); // consume '('
            Node* callee = make_node(p, NODE_IDENTIFIER, name_tok);
            callee->as.identifier.name = name_tok->value;
            callee->as.identifier.name_size = name_tok->size;
            Node* node = make_node(p, NODE_CALL_EXPR, name_tok);
            node->as.call_expr.callee = callee;
            node->as.call_expr.type_args = type_args;
            memset(&node->as.call_expr.args, 0, sizeof(NodeList));
            parse_args_into(p, &node->as.call_expr.args);
            expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after arguments.");
            return node;
        }
        Node* node = make_node(p, NODE_IDENTIFIER, name_tok);
        node->as.identifier.name = name_tok->value;
        node->as.identifier.name_size = name_tok->size;
        return node;
    }
    case TOKEN_LEFT_PAREN: {
        advance(p);
        Node* inner = parse_expression(p);
        expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after expression.");
        Node* node = make_node(p, NODE_PAREN_EXPR, tok);
        node->as.paren_expr.inner = inner;
        return node;
    }
    case TOKEN_LEFT_BRACKET: {
        Token* bracket_tok = advance(p);
        Node* node = make_node(p, NODE_ARRAY_LITERAL, bracket_tok);
        memset(&node->as.array_literal.elements, 0, sizeof(NodeList));
        if (!check(p, TOKEN_RIGHT_BRACKET)) {
            Node* elem = parse_expression(p);
            if (elem) node_list_push(p->arena, &node->as.array_literal.elements, elem);
            while (match(p, TOKEN_COMMA)) {
                elem = parse_expression(p);
                if (elem) node_list_push(p->arena, &node->as.array_literal.elements, elem);
            }
        }
        expect(p, TOKEN_RIGHT_BRACKET, "Expected ']' after array literal.");
        return node;
    }
    default:
        errors_push(p->errors, SEVERITY_ERROR, tok->offset, tok->line, tok->column,
                     "Unexpected token in expression.");
        p->had_error = true;
        p->panic_mode = true;
        return NULL;
    }
}

// Level 6: postfix (field access, method call)
static Node* parse_postfix(Parser* p) {
    Node* node = parse_primary(p);
    if (!node) return NULL;

    while (check(p, TOKEN_DOT) || check(p, TOKEN_LEFT_BRACKET)) {
        if (check(p, TOKEN_LEFT_BRACKET)) {
            Token* bracket_tok = advance(p);
            Node* index = parse_expression(p);
            expect(p, TOKEN_RIGHT_BRACKET, "Expected ']' after index.");
            Node* idx_node = make_node(p, NODE_INDEX_EXPR, bracket_tok);
            idx_node->as.index_expr.object = node;
            idx_node->as.index_expr.index = index;
            node = idx_node;
            continue;
        }

        Token* dot_tok = advance(p);
        Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected field name after '.'.");
        if (!name_tok) return node;

        if (check(p, TOKEN_LEFT_PAREN)) {
            // method call
            advance(p); // consume '('
            Node* call = make_node(p, NODE_METHOD_CALL, dot_tok);
            call->as.method_call.object = node;
            call->as.method_call.method_name = name_tok->value;
            call->as.method_call.method_name_size = name_tok->size;
            memset(&call->as.method_call.args, 0, sizeof(NodeList));
            parse_args_into(p, &call->as.method_call.args);
            expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after method arguments.");
            node = call;
        } else {
            // field access
            Node* access = make_node(p, NODE_FIELD_ACCESS, dot_tok);
            access->as.field_access.object = node;
            access->as.field_access.field_name = name_tok->value;
            access->as.field_access.field_name_size = name_tok->size;
            node = access;
        }
    }
    return node;
}

// Level 5: unary (-, &)
static Node* parse_unary(Parser* p) {
    if (check(p, TOKEN_MINUS)) {
        Token* tok = advance(p);
        Node* operand = parse_unary(p);
        Node* node = make_node(p, NODE_UNARY_EXPR, tok);
        node->as.unary_expr.op = TOKEN_MINUS;
        node->as.unary_expr.operand = operand;
        return node;
    }
    if (check(p, TOKEN_AMPERSAND)) {
        Token* tok = advance(p);
        Node* operand = parse_unary(p);
        Node* node = make_node(p, NODE_UNARY_EXPR, tok);
        node->as.unary_expr.op = TOKEN_AMPERSAND;
        node->as.unary_expr.operand = operand;
        return node;
    }
    if (check(p, TOKEN_STAR)) {
        Token* tok = advance(p);
        Node* operand = parse_unary(p);
        Node* node = make_node(p, NODE_UNARY_EXPR, tok);
        node->as.unary_expr.op = TOKEN_STAR;
        node->as.unary_expr.operand = operand;
        return node;
    }
    if (check(p, TOKEN_NOT)) {
        Token* tok = advance(p);
        Node* operand = parse_unary(p);
        Node* node = make_node(p, NODE_UNARY_EXPR, tok);
        node->as.unary_expr.op = TOKEN_NOT;
        node->as.unary_expr.operand = operand;
        return node;
    }
    return parse_postfix(p);
}

// Level 4.5: cast (as)
static Node* parse_cast(Parser* p) {
    Node* node = parse_unary(p);
    while (check(p, TOKEN_AS)) {
        Token* tok = advance(p);
        Node* type_node = parse_type(p);
        Node* cast = make_node(p, NODE_CAST_EXPR, tok);
        cast->as.cast_expr.expr = node;
        cast->as.cast_expr.target_type = type_node;
        node = cast;
    }
    return node;
}

// Level 4: bitwise (^)
static Node* parse_bitwise(Parser* p) {
    Node* left = parse_cast(p);
    while (check(p, TOKEN_CARET)) {
        Token* op_tok = advance(p);
        Node* right = parse_cast(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

// Level 3: multiplication (*, /)
static Node* parse_multiplication(Parser* p) {
    Node* left = parse_bitwise(p);
    while (check(p, TOKEN_STAR) || check(p, TOKEN_SLASH)) {
        Token* op_tok = advance(p);
        Node* right = parse_bitwise(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

// Level 2: addition (+, -)
static Node* parse_addition(Parser* p) {
    Node* left = parse_multiplication(p);
    while (check(p, TOKEN_PLUS) || check(p, TOKEN_MINUS)) {
        Token* op_tok = advance(p);
        Node* right = parse_multiplication(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

// Level 1: comparison (==, !=, <, >, <=, >=)
static Node* parse_comparison(Parser* p) {
    Node* left = parse_addition(p);
    while (check(p, TOKEN_EQUAL) || check(p, TOKEN_NOT_EQUAL) ||
           check(p, TOKEN_LESS_THAN) || check(p, TOKEN_GREATER_THAN) ||
           check(p, TOKEN_LESS_THAN_OR_EQUAL) || check(p, TOKEN_GREATER_THAN_OR_EQUAL)) {
        Token* op_tok = advance(p);
        Node* right = parse_addition(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

// Level 0b: logical and
static Node* parse_and(Parser* p) {
    Node* left = parse_comparison(p);
    while (check(p, TOKEN_AND)) {
        Token* op_tok = advance(p);
        Node* right = parse_comparison(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

// Level 0a: logical or (lowest precedence)
static Node* parse_or(Parser* p) {
    Node* left = parse_and(p);
    while (check(p, TOKEN_OR)) {
        Token* op_tok = advance(p);
        Node* right = parse_and(p);
        Node* node = make_node(p, NODE_BINARY_EXPR, op_tok);
        node->as.binary_expr.op = op_tok->type;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static Node* parse_expression(Parser* p) {
    return parse_or(p);
}

// ---------------------------------------------------------------------------
// Statement parsers
// ---------------------------------------------------------------------------

static Node* parse_return_stmt(Parser* p) {
    Token* tok = advance(p); // consume RETURN
    Node* node = make_node(p, NODE_RETURN_STMT, tok);
    node->as.return_stmt.value = NULL;

    if (!check(p, TOKEN_NEWLINE) && !check(p, TOKEN_END_OF_FILE) && !check(p, TOKEN_END)) {
        node->as.return_stmt.value = parse_expression(p);
    }
    return node;
}

static Node* parse_if_stmt(Parser* p) {
    Token* tok = advance(p); // consume IF
    Node* condition = parse_expression(p);
    expect_newline(p);

    NodeList then_body = parse_body(p);

    Node* node = make_node(p, NODE_IF_STMT, tok);
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_body = then_body;
    memset(&node->as.if_stmt.elseifs, 0, sizeof(ElseIfList));
    memset(&node->as.if_stmt.else_body, 0, sizeof(NodeList));

    while (check(p, TOKEN_ELSEIF)) {
        Token* ei_tok = advance(p);
        Node* ei_cond = parse_expression(p);
        expect_newline(p);
        NodeList ei_body = parse_body(p);
        ElseIfBranch branch = {0};
        branch.condition = ei_cond;
        branch.body = ei_body;
        branch.offset = ei_tok->offset;
        branch.line = ei_tok->line;
        branch.column = ei_tok->column;
        elseif_list_push(p->arena, &node->as.if_stmt.elseifs, branch);
    }

    if (match(p, TOKEN_ELSE)) {
        expect_newline(p);
        node->as.if_stmt.else_body = parse_body(p);
    }

    expect(p, TOKEN_END, "Expected 'end' to close if statement.");
    return node;
}

static Node* parse_for_stmt(Parser* p) {
    Token* tok = advance(p); // consume FOR
    Token* var_tok = expect(p, TOKEN_IDENTIFIER, "Expected loop variable after 'for'.");
    if (!var_tok) return make_node(p, NODE_FOR_STMT, tok);

    expect(p, TOKEN_IN, "Expected 'in' after loop variable.");
    Node* start = parse_expression(p);
    expect(p, TOKEN_UNTIL, "Expected 'until' in for loop.");
    Node* end_expr = parse_expression(p);

    Node* step = NULL;
    if (match(p, TOKEN_STEP)) {
        step = parse_expression(p);
    }

    expect_newline(p);
    NodeList body = parse_body(p);
    expect(p, TOKEN_END, "Expected 'end' to close for loop.");

    Node* node = make_node(p, NODE_FOR_STMT, tok);
    node->as.for_stmt.var_name = var_tok->value;
    node->as.for_stmt.var_name_size = var_tok->size;
    node->as.for_stmt.start = start;
    node->as.for_stmt.end = end_expr;
    node->as.for_stmt.step = step;
    node->as.for_stmt.body = body;
    return node;
}

static Node* parse_while_stmt(Parser* p) {
    Token* tok = advance(p); // consume WHILE
    Node* condition = parse_expression(p);
    expect_newline(p);
    NodeList body = parse_body(p);
    expect(p, TOKEN_END, "Expected 'end' to close while loop.");

    Node* node = make_node(p, NODE_WHILE_STMT, tok);
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
    return node;
}

static Node* parse_break_stmt(Parser* p) {
    Token* tok = advance(p); // consume BREAK
    return make_node(p, NODE_BREAK_STMT, tok);
}

static Node* parse_continue_stmt(Parser* p) {
    Token* tok = advance(p); // consume CONTINUE
    return make_node(p, NODE_CONTINUE_STMT, tok);
}

static Node* parse_match_stmt(Parser* p) {
    Token* tok = advance(p); // consume MATCH
    Node* subject = parse_expression(p);
    expect_newline(p);
    skip_newlines(p);

    Node* node = make_node(p, NODE_MATCH_STMT, tok);
    node->as.match_stmt.subject = subject;
    memset(&node->as.match_stmt.cases, 0, sizeof(MatchCaseList));
    memset(&node->as.match_stmt.else_body, 0, sizeof(NodeList));

    while (!check(p, TOKEN_END) && !check(p, TOKEN_ELSE) && !check(p, TOKEN_END_OF_FILE)) {
        skip_newlines(p);
        if (check(p, TOKEN_CASE)) {
            Token* case_tok = advance(p);
            MatchCase mc = {0};
            mc.offset = case_tok->offset;
            mc.line = case_tok->line;
            mc.column = case_tok->column;

            // parse comma-separated values
            Node* val = parse_expression(p);
            if (val) node_list_push(p->arena, &mc.values, val);
            while (match(p, TOKEN_COMMA)) {
                val = parse_expression(p);
                if (val) node_list_push(p->arena, &mc.values, val);
            }
            expect_newline(p);
            mc.body = parse_body(p);
            match_case_list_push(p->arena, &node->as.match_stmt.cases, mc);
        } else if (check(p, TOKEN_END) || check(p, TOKEN_ELSE)) {
            break;
        } else {
            errors_push(p->errors, SEVERITY_ERROR, peek(p)->offset, peek(p)->line, peek(p)->column,
                         "Expected 'case', 'else', or 'end' in match statement.");
            p->had_error = true;
            synchronize(p);
        }
    }

    if (match(p, TOKEN_ELSE)) {
        expect_newline(p);
        node->as.match_stmt.else_body = parse_body(p);
    }

    expect(p, TOKEN_END, "Expected 'end' to close match statement.");
    return node;
}

static Node* parse_assignment_or_expr_stmt(Parser* p) {
    Node* expr = parse_expression(p);
    if (!expr) return NULL;

    if (check(p, TOKEN_ASSIGN)) {
        Token* tok = advance(p);
        Node* value = parse_expression(p);
        Node* node = make_node(p, NODE_ASSIGN_STMT, tok);
        node->as.assign_stmt.target = expr;
        node->as.assign_stmt.value = value;
        return node;
    }

    if (check(p, TOKEN_PLUS_ASSIGN) || check(p, TOKEN_MINUS_ASSIGN) ||
        check(p, TOKEN_STAR_ASSIGN) || check(p, TOKEN_SLASH_ASSIGN)) {
        Token* op_tok = advance(p);
        Node* value = parse_expression(p);
        Node* node = make_node(p, NODE_COMPOUND_ASSIGN_STMT, op_tok);
        node->as.compound_assign_stmt.op = op_tok->type;
        node->as.compound_assign_stmt.target = expr;
        node->as.compound_assign_stmt.value = value;
        return node;
    }

    Node* node = make_node(p, NODE_EXPR_STMT, &p->tokens->tokens[expr->line > 0 ? 0 : 0]);
    node->offset = expr->offset;
    node->line = expr->line;
    node->column = expr->column;
    node->as.expr_stmt.expr = expr;
    return node;
}

static Node* parse_statement(Parser* p) {
    switch (peek(p)->type) {
    case TOKEN_VAR:    return parse_var_decl(p, false);
    case TOKEN_CONST:  return parse_const_decl(p, false);
    case TOKEN_RETURN: return parse_return_stmt(p);
    case TOKEN_IF:     return parse_if_stmt(p);
    case TOKEN_FOR:    return parse_for_stmt(p);
    case TOKEN_WHILE:  return parse_while_stmt(p);
    case TOKEN_BREAK:    return parse_break_stmt(p);
    case TOKEN_CONTINUE: return parse_continue_stmt(p);
    case TOKEN_MATCH:    return parse_match_stmt(p);
    default:           return parse_assignment_or_expr_stmt(p);
    }
}

static NodeList parse_body(Parser* p) {
    NodeList stmts = {0};
    skip_newlines(p);

    while (!check(p, TOKEN_END) && !check(p, TOKEN_ELSE) &&
           !check(p, TOKEN_ELSEIF) && !check(p, TOKEN_CASE) &&
           !check(p, TOKEN_END_OF_FILE)) {
        Node* stmt = parse_statement(p);
        if (stmt) {
            node_list_push(p->arena, &stmts, stmt);
        }
        if (p->panic_mode) {
            synchronize(p);
            continue;
        }
        expect_newline(p);
        skip_newlines(p);
    }
    return stmts;
}

// ---------------------------------------------------------------------------
// Declaration parsers
// ---------------------------------------------------------------------------

static Node* parse_const_decl(Parser* p, bool is_export) {
    Token* tok = advance(p); // consume CONST
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected name after 'const'.");
    if (!name_tok) return NULL;

    Node* type_node = NULL;
    if (match(p, TOKEN_COLON)) {
        type_node = parse_type(p);
    }

    expect(p, TOKEN_ASSIGN, "Expected '=' in const declaration.");
    Node* value = parse_expression(p);

    Node* node = make_node(p, NODE_CONST_DECL, tok);
    node->as.const_decl.is_export = is_export;
    node->as.const_decl.name = name_tok->value;
    node->as.const_decl.name_size = name_tok->size;
    node->as.const_decl.type_node = type_node;
    node->as.const_decl.value = value;
    return node;
}

static Node* parse_var_decl(Parser* p, bool is_export) {
    Token* tok = advance(p); // consume VAR
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected name after 'var'.");
    if (!name_tok) return NULL;

    Node* type_node = NULL;
    if (match(p, TOKEN_COLON)) {
        type_node = parse_type(p);
    }

    expect(p, TOKEN_ASSIGN, "Expected '=' in var declaration.");
    Node* value = parse_expression(p);

    Node* node = make_node(p, NODE_VAR_DECL, tok);
    node->as.var_decl.is_export = is_export;
    node->as.var_decl.name = name_tok->value;
    node->as.var_decl.name_size = name_tok->size;
    node->as.var_decl.type_node = type_node;
    node->as.var_decl.value = value;
    return node;
}

static ParamList parse_param_list(Parser* p) {
    ParamList params = {0};
    if (check(p, TOKEN_RIGHT_PAREN)) return params;

    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected parameter name.");
    if (!name_tok) return params;
    expect(p, TOKEN_COLON, "Expected ':' after parameter name.");
    Node* type_node = parse_type(p);

    Param param = {0};
    param.name = name_tok->value;
    param.name_size = name_tok->size;
    param.type_node = type_node;
    param.offset = name_tok->offset;
    param.line = name_tok->line;
    param.column = name_tok->column;
    param_list_push(p->arena, &params, param);

    while (match(p, TOKEN_COMMA)) {
        name_tok = expect(p, TOKEN_IDENTIFIER, "Expected parameter name.");
        if (!name_tok) break;
        expect(p, TOKEN_COLON, "Expected ':' after parameter name.");
        type_node = parse_type(p);

        Param next = {0};
        next.name = name_tok->value;
        next.name_size = name_tok->size;
        next.type_node = type_node;
        next.offset = name_tok->offset;
        next.line = name_tok->line;
        next.column = name_tok->column;
        param_list_push(p->arena, &params, next);
    }
    return params;
}

static Node* parse_func_decl(Parser* p, bool is_export) {
    Token* tok = advance(p); // consume FUNC
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected function name.");
    if (!name_tok) return NULL;

    TypeParamList type_params = {0};
    if (check(p, TOKEN_LEFT_BRACKET)) {
        type_params = parse_type_params(p);
    }

    expect(p, TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    ParamList params = parse_param_list(p);
    expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

    Node* return_type = NULL;
    if (match(p, TOKEN_COLON)) {
        return_type = parse_type(p);
    }

    expect_newline(p);
    NodeList body = parse_body(p);
    expect(p, TOKEN_END, "Expected 'end' to close function.");

    Node* node = make_node(p, NODE_FUNC_DECL, tok);
    node->as.func_decl.is_export = is_export;
    node->as.func_decl.name = name_tok->value;
    node->as.func_decl.name_size = name_tok->size;
    node->as.func_decl.type_params = type_params;
    node->as.func_decl.params = params;
    node->as.func_decl.return_type = return_type;
    node->as.func_decl.body = body;
    return node;
}

static Node* parse_func_signature(Parser* p) {
    Token* tok = advance(p); // consume FUNC
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected function name.");
    if (!name_tok) return NULL;

    expect(p, TOKEN_LEFT_PAREN, "Expected '(' after function name.");
    ParamList params = parse_param_list(p);
    expect(p, TOKEN_RIGHT_PAREN, "Expected ')' after parameters.");

    Node* return_type = NULL;
    if (match(p, TOKEN_COLON)) {
        return_type = parse_type(p);
    }

    Node* node = make_node(p, NODE_FUNC_DECL, tok);
    node->as.func_decl.is_export = false;
    node->as.func_decl.name = name_tok->value;
    node->as.func_decl.name_size = name_tok->size;
    node->as.func_decl.params = params;
    node->as.func_decl.return_type = return_type;
    memset(&node->as.func_decl.body, 0, sizeof(NodeList));
    return node;
}

static Node* parse_struct_decl(Parser* p, bool is_export) {
    Token* tok = advance(p); // consume STRUCT
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected struct name.");
    if (!name_tok) return NULL;

    TypeParamList type_params = {0};
    if (check(p, TOKEN_LEFT_BRACKET)) {
        type_params = parse_type_params(p);
    }

    expect_newline(p);
    skip_newlines(p);

    Node* node = make_node(p, NODE_STRUCT_DECL, tok);
    node->as.struct_decl.is_export = is_export;
    node->as.struct_decl.name = name_tok->value;
    node->as.struct_decl.name_size = name_tok->size;
    node->as.struct_decl.type_params = type_params;
    memset(&node->as.struct_decl.fields, 0, sizeof(FieldList));
    memset(&node->as.struct_decl.methods, 0, sizeof(NodeList));

    while (!check(p, TOKEN_END) && !check(p, TOKEN_END_OF_FILE)) {
        skip_newlines(p);
        if (check(p, TOKEN_END)) break;

        if (check(p, TOKEN_FUNC)) {
            Node* method = parse_func_decl(p, false);
            if (method) {
                node_list_push(p->arena, &node->as.struct_decl.methods, method);
            }
        } else if (check(p, TOKEN_IDENTIFIER)) {
            Token* field_tok = advance(p);
            expect(p, TOKEN_COLON, "Expected ':' after field name.");
            Node* type_node = parse_type(p);

            Field field = {0};
            field.name = field_tok->value;
            field.name_size = field_tok->size;
            field.type_node = type_node;
            field.offset = field_tok->offset;
            field.line = field_tok->line;
            field.column = field_tok->column;
            field_list_push(p->arena, &node->as.struct_decl.fields, field);
            expect_newline(p);
        } else {
            errors_push(p->errors, SEVERITY_ERROR, peek(p)->offset, peek(p)->line, peek(p)->column,
                         "Expected field or method in struct.");
            p->had_error = true;
            synchronize(p);
        }
        skip_newlines(p);
    }

    expect(p, TOKEN_END, "Expected 'end' to close struct.");
    return node;
}

static Node* parse_interface_decl(Parser* p) {
    Token* tok = advance(p); // consume INTERFACE
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected interface name.");
    if (!name_tok) return NULL;

    expect_newline(p);
    skip_newlines(p);

    Node* node = make_node(p, NODE_INTERFACE_DECL, tok);
    node->as.interface_decl.name = name_tok->value;
    node->as.interface_decl.name_size = name_tok->size;
    memset(&node->as.interface_decl.method_sigs, 0, sizeof(NodeList));

    while (!check(p, TOKEN_END) && !check(p, TOKEN_END_OF_FILE)) {
        skip_newlines(p);
        if (check(p, TOKEN_END)) break;

        if (check(p, TOKEN_FUNC)) {
            Node* sig = parse_func_signature(p);
            if (sig) {
                node_list_push(p->arena, &node->as.interface_decl.method_sigs, sig);
            }
            expect_newline(p);
        } else {
            errors_push(p->errors, SEVERITY_ERROR, peek(p)->offset, peek(p)->line, peek(p)->column,
                         "Expected method signature in interface.");
            p->had_error = true;
            synchronize(p);
        }
        skip_newlines(p);
    }

    expect(p, TOKEN_END, "Expected 'end' to close interface.");
    return node;
}

static Node* parse_import_decl(Parser* p) {
    Token* tok = advance(p); // consume FROM

    // Parse module path: identifier with dots (e.g., math.vectors)
    Token* path_tok = expect(p, TOKEN_IDENTIFIER, "Expected module name after 'from'.");
    if (!path_tok) return NULL;

    // Track start and end of the full path for dot-separated modules
    char* path_start = path_tok->value;
    char* path_end = path_tok->value + path_tok->size;

    while (check(p, TOKEN_DOT)) {
        advance(p); // consume '.'
        Token* next = expect(p, TOKEN_IDENTIFIER, "Expected module name after '.'.");
        if (!next) break;
        path_end = next->value + next->size;
    }

    size_t path_size = (size_t)(path_end - path_start);

    bool is_export = false;
    if (check(p, TOKEN_EXPORT)) {
        advance(p);
        is_export = true;
    } else {
        expect(p, TOKEN_IMPORT, "Expected 'import' or 'export' after module path.");
    }

    Node* node = make_node(p, NODE_IMPORT_DECL, tok);
    node->as.import_decl.is_export = is_export;
    node->as.import_decl.module_path = path_start;
    node->as.import_decl.module_path_size = path_size;
    memset(&node->as.import_decl.names, 0, sizeof(ImportNameList));

    // Parse comma-separated import names
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected name to import.");
    if (name_tok) {
        ImportName name = {0};
        name.name = name_tok->value;
        name.name_size = name_tok->size;
        name.offset = name_tok->offset;
        name.line = name_tok->line;
        name.column = name_tok->column;
        import_name_list_push(p->arena, &node->as.import_decl.names, name);

        while (match(p, TOKEN_COMMA)) {
            name_tok = expect(p, TOKEN_IDENTIFIER, "Expected name to import.");
            if (!name_tok) break;
            ImportName next = {0};
            next.name = name_tok->value;
            next.name_size = name_tok->size;
            next.offset = name_tok->offset;
            next.line = name_tok->line;
            next.column = name_tok->column;
            import_name_list_push(p->arena, &node->as.import_decl.names, next);
        }
    }

    return node;
}

static Node* parse_enum_decl(Parser* p, bool is_export) {
    Token* tok = advance(p); // consume ENUM
    Token* name_tok = expect(p, TOKEN_IDENTIFIER, "Expected enum name.");
    if (!name_tok) return NULL;

    expect_newline(p);
    skip_newlines(p);

    Node* node = make_node(p, NODE_ENUM_DECL, tok);
    node->as.enum_decl.is_export = is_export;
    node->as.enum_decl.name = name_tok->value;
    node->as.enum_decl.name_size = name_tok->size;
    memset(&node->as.enum_decl.variants, 0, sizeof(EnumVariantList));

    while (!check(p, TOKEN_END) && !check(p, TOKEN_END_OF_FILE)) {
        skip_newlines(p);
        if (check(p, TOKEN_END)) break;

        if (check(p, TOKEN_IDENTIFIER)) {
            Token* var_tok = advance(p);
            EnumVariant variant = {0};
            variant.name = var_tok->value;
            variant.name_size = var_tok->size;
            variant.offset = var_tok->offset;
            variant.line = var_tok->line;
            variant.column = var_tok->column;
            enum_variant_list_push(p->arena, &node->as.enum_decl.variants, variant);
            expect_newline(p);
        } else {
            errors_push(p->errors, SEVERITY_ERROR, peek(p)->offset, peek(p)->line, peek(p)->column,
                         "Expected variant name in enum.");
            p->had_error = true;
            synchronize(p);
        }
        skip_newlines(p);
    }

    expect(p, TOKEN_END, "Expected 'end' to close enum.");
    return node;
}

static Node* parse_export_declaration(Parser* p) {
    advance(p); // consume EXPORT
    if (check(p, TOKEN_CONST))  return parse_const_decl(p, true);
    if (check(p, TOKEN_VAR))    return parse_var_decl(p, true);
    if (check(p, TOKEN_FUNC))   return parse_func_decl(p, true);
    if (check(p, TOKEN_STRUCT)) return parse_struct_decl(p, true);
    if (check(p, TOKEN_ENUM))   return parse_enum_decl(p, true);

    Token* tok = peek(p);
    errors_push(p->errors, SEVERITY_ERROR, tok->offset, tok->line, tok->column,
                "Expected declaration after 'export'.");
    p->had_error = true;
    p->panic_mode = true;
    return NULL;
}

static Node* parse_program(Parser* p) {
    Token* tok = peek(p);
    Node* program = make_node(p, NODE_PROGRAM, tok);
    memset(&program->as.program.declarations, 0, sizeof(NodeList));

    skip_newlines(p);

    while (!check(p, TOKEN_END_OF_FILE)) {
        if (p->panic_mode) {
            synchronize(p);
        }

        Node* decl = NULL;

        if (check(p, TOKEN_FROM)) {
            decl = parse_import_decl(p);
        } else if (check(p, TOKEN_EXPORT)) {
            decl = parse_export_declaration(p);
        } else if (check(p, TOKEN_CONST)) {
            decl = parse_const_decl(p, false);
        } else if (check(p, TOKEN_VAR)) {
            decl = parse_var_decl(p, false);
        } else if (check(p, TOKEN_FUNC)) {
            decl = parse_func_decl(p, false);
        } else if (check(p, TOKEN_STRUCT)) {
            decl = parse_struct_decl(p, false);
        } else if (check(p, TOKEN_INTERFACE)) {
            decl = parse_interface_decl(p);
        } else if (check(p, TOKEN_ENUM)) {
            decl = parse_enum_decl(p, false);
        } else {
            Token* t = peek(p);
            errors_push(p->errors, SEVERITY_ERROR, t->offset, t->line, t->column,
                         "Unexpected top-level token.");
            p->had_error = true;
            p->panic_mode = true;
            synchronize(p);
            skip_newlines(p);
            continue;
        }

        if (decl) {
            node_list_push(p->arena, &program->as.program.declarations, decl);
        }

        skip_newlines(p);
    }

    return program;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Node* parser_parse(Arena* arena, Tokens* tokens, Errors* errors) {
    Parser parser;
    parser.arena = arena;
    parser.errors = errors;
    parser.tokens = tokens;
    parser.pos = 0;
    parser.had_error = false;
    parser.panic_mode = false;
    return parse_program(&parser);
}

// ---------------------------------------------------------------------------
// AST printer
// ---------------------------------------------------------------------------

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static const char* op_to_string(TokenType op) {
    switch (op) {
    case TOKEN_PLUS:                   return "+";
    case TOKEN_MINUS:                  return "-";
    case TOKEN_STAR:                   return "*";
    case TOKEN_SLASH:                  return "/";
    case TOKEN_CARET:                  return "^";
    case TOKEN_AMPERSAND:              return "&";
    case TOKEN_EQUAL:                  return "==";
    case TOKEN_NOT_EQUAL:              return "!=";
    case TOKEN_LESS_THAN:              return "<";
    case TOKEN_GREATER_THAN:           return ">";
    case TOKEN_LESS_THAN_OR_EQUAL:     return "<=";
    case TOKEN_GREATER_THAN_OR_EQUAL:  return ">=";
    case TOKEN_PLUS_ASSIGN:            return "+=";
    case TOKEN_MINUS_ASSIGN:           return "-=";
    case TOKEN_STAR_ASSIGN:            return "*=";
    case TOKEN_SLASH_ASSIGN:           return "/=";
    case TOKEN_AND:                    return "and";
    case TOKEN_OR:                     return "or";
    case TOKEN_NOT:                    return "not";
    default:                           return "?";
    }
}

static void ast_print_type(Node* node, int indent) {
    if (!node) return;
    switch (node->type) {
    case NODE_TYPE_SIMPLE:
        print_indent(indent);
        printf("TypeSimple %.*s\n", (int)node->as.type_simple.name_size, node->as.type_simple.name);
        break;
    case NODE_TYPE_REFERENCE:
        print_indent(indent);
        printf("TypeRef\n");
        ast_print_type(node->as.type_ref.inner, indent + 1);
        break;
    case NODE_TYPE_POINTER:
        print_indent(indent);
        printf("TypePtr\n");
        ast_print_type(node->as.type_ptr.inner, indent + 1);
        break;
    case NODE_TYPE_ARRAY:
        print_indent(indent);
        printf("TypeArray\n");
        ast_print_type(node->as.type_array.inner, indent + 1);
        print_indent(indent + 1);
        printf("size:\n");
        ast_print(node->as.type_array.size_expr, indent + 2);
        break;
    case NODE_TYPE_SLICE:
        print_indent(indent);
        printf("TypeSlice\n");
        ast_print_type(node->as.type_slice.inner, indent + 1);
        break;
    default:
        break;
    }
}

void ast_print(Node* node, int indent) {
    if (!node) return;

    print_indent(indent);

    switch (node->type) {
    case NODE_PROGRAM:
        printf("Program\n");
        for (size_t i = 0; i < node->as.program.declarations.count; i++) {
            ast_print(node->as.program.declarations.nodes[i], indent + 1);
        }
        break;

    case NODE_IMPORT_DECL:
        printf("ImportDecl [%zu:%zu] from %.*s %s",
               node->line, node->column,
               (int)node->as.import_decl.module_path_size, node->as.import_decl.module_path,
               node->as.import_decl.is_export ? "export" : "import");
        for (size_t i = 0; i < node->as.import_decl.names.count; i++) {
            ImportName* name = &node->as.import_decl.names.names[i];
            printf("%s%.*s", i > 0 ? ", " : " ", (int)name->name_size, name->name);
        }
        printf("\n");
        break;

    case NODE_CONST_DECL:
        printf("ConstDecl [%zu:%zu] %.*s%s\n",
               node->line, node->column,
               (int)node->as.const_decl.name_size, node->as.const_decl.name,
               node->as.const_decl.is_export ? " (export)" : "");
        if (node->as.const_decl.type_node) {
            print_indent(indent + 1);
            printf("type:\n");
            ast_print_type(node->as.const_decl.type_node, indent + 2);
        }
        print_indent(indent + 1);
        printf("value:\n");
        ast_print(node->as.const_decl.value, indent + 2);
        break;

    case NODE_VAR_DECL:
        printf("VarDecl [%zu:%zu] %.*s%s\n",
               node->line, node->column,
               (int)node->as.var_decl.name_size, node->as.var_decl.name,
               node->as.var_decl.is_export ? " (export)" : "");
        if (node->as.var_decl.type_node) {
            print_indent(indent + 1);
            printf("type:\n");
            ast_print_type(node->as.var_decl.type_node, indent + 2);
        }
        print_indent(indent + 1);
        printf("value:\n");
        ast_print(node->as.var_decl.value, indent + 2);
        break;

    case NODE_FUNC_DECL:
        printf("FuncDecl [%zu:%zu] %.*s%s\n",
               node->line, node->column,
               (int)node->as.func_decl.name_size, node->as.func_decl.name,
               node->as.func_decl.is_export ? " (export)" : "");
        if (node->as.func_decl.params.count > 0) {
            print_indent(indent + 1);
            printf("params:\n");
            for (size_t i = 0; i < node->as.func_decl.params.count; i++) {
                Param* param = &node->as.func_decl.params.params[i];
                print_indent(indent + 2);
                printf("%.*s:\n", (int)param->name_size, param->name);
                ast_print_type(param->type_node, indent + 3);
            }
        }
        if (node->as.func_decl.return_type) {
            print_indent(indent + 1);
            printf("return_type:\n");
            ast_print_type(node->as.func_decl.return_type, indent + 2);
        }
        if (node->as.func_decl.body.count > 0) {
            print_indent(indent + 1);
            printf("body:\n");
            for (size_t i = 0; i < node->as.func_decl.body.count; i++) {
                ast_print(node->as.func_decl.body.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_STRUCT_DECL:
        printf("StructDecl [%zu:%zu] %.*s%s\n",
               node->line, node->column,
               (int)node->as.struct_decl.name_size, node->as.struct_decl.name,
               node->as.struct_decl.is_export ? " (export)" : "");
        if (node->as.struct_decl.fields.count > 0) {
            print_indent(indent + 1);
            printf("fields:\n");
            for (size_t i = 0; i < node->as.struct_decl.fields.count; i++) {
                Field* f = &node->as.struct_decl.fields.fields[i];
                print_indent(indent + 2);
                printf("%.*s:\n", (int)f->name_size, f->name);
                ast_print_type(f->type_node, indent + 3);
            }
        }
        if (node->as.struct_decl.methods.count > 0) {
            print_indent(indent + 1);
            printf("methods:\n");
            for (size_t i = 0; i < node->as.struct_decl.methods.count; i++) {
                ast_print(node->as.struct_decl.methods.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_INTERFACE_DECL:
        printf("InterfaceDecl [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.interface_decl.name_size, node->as.interface_decl.name);
        if (node->as.interface_decl.method_sigs.count > 0) {
            print_indent(indent + 1);
            printf("methods:\n");
            for (size_t i = 0; i < node->as.interface_decl.method_sigs.count; i++) {
                ast_print(node->as.interface_decl.method_sigs.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_RETURN_STMT:
        printf("ReturnStmt [%zu:%zu]\n", node->line, node->column);
        if (node->as.return_stmt.value) {
            ast_print(node->as.return_stmt.value, indent + 1);
        }
        break;

    case NODE_IF_STMT:
        printf("IfStmt [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("condition:\n");
        ast_print(node->as.if_stmt.condition, indent + 2);
        print_indent(indent + 1);
        printf("then:\n");
        for (size_t i = 0; i < node->as.if_stmt.then_body.count; i++) {
            ast_print(node->as.if_stmt.then_body.nodes[i], indent + 2);
        }
        for (size_t i = 0; i < node->as.if_stmt.elseifs.count; i++) {
            ElseIfBranch* ei = &node->as.if_stmt.elseifs.branches[i];
            print_indent(indent + 1);
            printf("elseif [%zu:%zu]:\n", ei->line, ei->column);
            print_indent(indent + 2);
            printf("condition:\n");
            ast_print(ei->condition, indent + 3);
            print_indent(indent + 2);
            printf("body:\n");
            for (size_t j = 0; j < ei->body.count; j++) {
                ast_print(ei->body.nodes[j], indent + 3);
            }
        }
        if (node->as.if_stmt.else_body.count > 0) {
            print_indent(indent + 1);
            printf("else:\n");
            for (size_t i = 0; i < node->as.if_stmt.else_body.count; i++) {
                ast_print(node->as.if_stmt.else_body.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_FOR_STMT:
        printf("ForStmt [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.for_stmt.var_name_size, node->as.for_stmt.var_name);
        print_indent(indent + 1);
        printf("start:\n");
        ast_print(node->as.for_stmt.start, indent + 2);
        print_indent(indent + 1);
        printf("end:\n");
        ast_print(node->as.for_stmt.end, indent + 2);
        if (node->as.for_stmt.step) {
            print_indent(indent + 1);
            printf("step:\n");
            ast_print(node->as.for_stmt.step, indent + 2);
        }
        print_indent(indent + 1);
        printf("body:\n");
        for (size_t i = 0; i < node->as.for_stmt.body.count; i++) {
            ast_print(node->as.for_stmt.body.nodes[i], indent + 2);
        }
        break;

    case NODE_WHILE_STMT:
        printf("WhileStmt [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("condition:\n");
        ast_print(node->as.while_stmt.condition, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        for (size_t i = 0; i < node->as.while_stmt.body.count; i++) {
            ast_print(node->as.while_stmt.body.nodes[i], indent + 2);
        }
        break;

    case NODE_BREAK_STMT:
        printf("BreakStmt [%zu:%zu]\n", node->line, node->column);
        break;

    case NODE_CONTINUE_STMT:
        printf("ContinueStmt [%zu:%zu]\n", node->line, node->column);
        break;

    case NODE_MATCH_STMT:
        printf("MatchStmt [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("subject:\n");
        ast_print(node->as.match_stmt.subject, indent + 2);
        for (size_t i = 0; i < node->as.match_stmt.cases.count; i++) {
            MatchCase* mc = &node->as.match_stmt.cases.cases[i];
            print_indent(indent + 1);
            printf("case [%zu:%zu]:\n", mc->line, mc->column);
            print_indent(indent + 2);
            printf("values:\n");
            for (size_t j = 0; j < mc->values.count; j++) {
                ast_print(mc->values.nodes[j], indent + 3);
            }
            print_indent(indent + 2);
            printf("body:\n");
            for (size_t j = 0; j < mc->body.count; j++) {
                ast_print(mc->body.nodes[j], indent + 3);
            }
        }
        if (node->as.match_stmt.else_body.count > 0) {
            print_indent(indent + 1);
            printf("else:\n");
            for (size_t i = 0; i < node->as.match_stmt.else_body.count; i++) {
                ast_print(node->as.match_stmt.else_body.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_ASSIGN_STMT:
        printf("AssignStmt [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("target:\n");
        ast_print(node->as.assign_stmt.target, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_print(node->as.assign_stmt.value, indent + 2);
        break;

    case NODE_COMPOUND_ASSIGN_STMT:
        printf("CompoundAssignStmt [%zu:%zu] %s\n",
               node->line, node->column,
               op_to_string(node->as.compound_assign_stmt.op));
        print_indent(indent + 1);
        printf("target:\n");
        ast_print(node->as.compound_assign_stmt.target, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_print(node->as.compound_assign_stmt.value, indent + 2);
        break;

    case NODE_EXPR_STMT:
        printf("ExprStmt [%zu:%zu]\n", node->line, node->column);
        ast_print(node->as.expr_stmt.expr, indent + 1);
        break;

    case NODE_INTEGER_LITERAL:
        printf("IntegerLiteral [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.integer_literal.value_size, node->as.integer_literal.value);
        break;

    case NODE_FLOAT_LITERAL:
        printf("FloatLiteral [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.float_literal.value_size, node->as.float_literal.value);
        break;

    case NODE_STRING_LITERAL:
        printf("StringLiteral [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.string_literal.value_size, node->as.string_literal.value);
        break;

    case NODE_BOOL_LITERAL:
        printf("BoolLiteral [%zu:%zu] %s\n",
               node->line, node->column,
               node->as.bool_literal.value ? "true" : "false");
        break;

    case NODE_NULL_LITERAL:
        printf("NullLiteral [%zu:%zu]\n", node->line, node->column);
        break;

    case NODE_IDENTIFIER:
        printf("Identifier [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.identifier.name_size, node->as.identifier.name);
        break;

    case NODE_SELF:
        printf("Self [%zu:%zu]\n", node->line, node->column);
        break;

    case NODE_BINARY_EXPR:
        printf("BinaryExpr [%zu:%zu] %s\n",
               node->line, node->column,
               op_to_string(node->as.binary_expr.op));
        ast_print(node->as.binary_expr.left, indent + 1);
        ast_print(node->as.binary_expr.right, indent + 1);
        break;

    case NODE_UNARY_EXPR:
        printf("UnaryExpr [%zu:%zu] %s\n",
               node->line, node->column,
               op_to_string(node->as.unary_expr.op));
        ast_print(node->as.unary_expr.operand, indent + 1);
        break;

    case NODE_PAREN_EXPR:
        printf("ParenExpr [%zu:%zu]\n", node->line, node->column);
        ast_print(node->as.paren_expr.inner, indent + 1);
        break;

    case NODE_CALL_EXPR:
        printf("CallExpr [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("callee:\n");
        ast_print(node->as.call_expr.callee, indent + 2);
        if (node->as.call_expr.args.count > 0) {
            print_indent(indent + 1);
            printf("args:\n");
            for (size_t i = 0; i < node->as.call_expr.args.count; i++) {
                ast_print(node->as.call_expr.args.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_FIELD_ACCESS:
        printf("FieldAccess [%zu:%zu] .%.*s\n",
               node->line, node->column,
               (int)node->as.field_access.field_name_size, node->as.field_access.field_name);
        ast_print(node->as.field_access.object, indent + 1);
        break;

    case NODE_METHOD_CALL:
        printf("MethodCall [%zu:%zu] .%.*s()\n",
               node->line, node->column,
               (int)node->as.method_call.method_name_size, node->as.method_call.method_name);
        print_indent(indent + 1);
        printf("object:\n");
        ast_print(node->as.method_call.object, indent + 2);
        if (node->as.method_call.args.count > 0) {
            print_indent(indent + 1);
            printf("args:\n");
            for (size_t i = 0; i < node->as.method_call.args.count; i++) {
                ast_print(node->as.method_call.args.nodes[i], indent + 2);
            }
        }
        break;

    case NODE_STRUCT_LITERAL:
        printf("StructLiteral [%zu:%zu] %.*s\n",
               node->line, node->column,
               (int)node->as.struct_literal.struct_name_size, node->as.struct_literal.struct_name);
        for (size_t i = 0; i < node->as.struct_literal.fields.count; i++) {
            FieldInit* fi = &node->as.struct_literal.fields.inits[i];
            print_indent(indent + 1);
            printf("%.*s:\n", (int)fi->name_size, fi->name);
            ast_print(fi->value, indent + 2);
        }
        break;

    case NODE_CAST_EXPR:
        printf("CastExpr [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("expr:\n");
        ast_print(node->as.cast_expr.expr, indent + 2);
        print_indent(indent + 1);
        printf("target:\n");
        ast_print(node->as.cast_expr.target_type, indent + 2);
        break;

    case NODE_ARRAY_LITERAL:
        printf("ArrayLiteral [%zu:%zu]\n", node->line, node->column);
        for (size_t i = 0; i < node->as.array_literal.elements.count; i++) {
            ast_print(node->as.array_literal.elements.nodes[i], indent + 1);
        }
        break;

    case NODE_INDEX_EXPR:
        printf("IndexExpr [%zu:%zu]\n", node->line, node->column);
        print_indent(indent + 1);
        printf("object:\n");
        ast_print(node->as.index_expr.object, indent + 2);
        print_indent(indent + 1);
        printf("index:\n");
        ast_print(node->as.index_expr.index, indent + 2);
        break;

    case NODE_ENUM_DECL:
        printf("EnumDecl [%zu:%zu] %.*s%s\n",
               node->line, node->column,
               (int)node->as.enum_decl.name_size, node->as.enum_decl.name,
               node->as.enum_decl.is_export ? " (export)" : "");
        for (size_t i = 0; i < node->as.enum_decl.variants.count; i++) {
            EnumVariant* v = &node->as.enum_decl.variants.variants[i];
            print_indent(indent + 1);
            printf("%.*s\n", (int)v->name_size, v->name);
        }
        break;

    case NODE_TYPE_SIMPLE:
    case NODE_TYPE_REFERENCE:
    case NODE_TYPE_POINTER:
    case NODE_TYPE_ARRAY:
    case NODE_TYPE_SLICE:
        ast_print_type(node, indent);
        break;
    }
}
