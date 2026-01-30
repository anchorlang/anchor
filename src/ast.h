#ifndef ANCC_AST_H
#define ANCC_AST_H

#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum NodeType {
    // program root
    NODE_PROGRAM,

    // declarations
    NODE_CONST_DECL,
    NODE_VAR_DECL,
    NODE_FUNC_DECL,
    NODE_STRUCT_DECL,
    NODE_INTERFACE_DECL,

    // statements
    NODE_RETURN_STMT,
    NODE_IF_STMT,
    NODE_FOR_STMT,
    NODE_WHILE_STMT,
    NODE_BREAK_STMT,
    NODE_MATCH_STMT,
    NODE_ASSIGN_STMT,
    NODE_COMPOUND_ASSIGN_STMT,
    NODE_EXPR_STMT,

    // expressions
    NODE_INTEGER_LITERAL,
    NODE_FLOAT_LITERAL,
    NODE_STRING_LITERAL,
    NODE_BOOL_LITERAL,
    NODE_NULL_LITERAL,
    NODE_IDENTIFIER,
    NODE_SELF,
    NODE_BINARY_EXPR,
    NODE_UNARY_EXPR,
    NODE_PAREN_EXPR,
    NODE_CALL_EXPR,
    NODE_FIELD_ACCESS,
    NODE_METHOD_CALL,
    NODE_STRUCT_LITERAL,

    // types
    NODE_TYPE_SIMPLE,
    NODE_TYPE_REFERENCE,
    NODE_TYPE_POINTER,
} NodeType;

typedef struct Node Node;

typedef struct NodeList {
    Node** nodes;
    size_t count;
    size_t capacity;
} NodeList;

typedef struct Param {
    char* name;
    size_t name_size;
    Node* type_node;
    size_t offset;
    size_t line;
    size_t column;
} Param;

typedef struct ParamList {
    Param* params;
    size_t count;
    size_t capacity;
} ParamList;

typedef struct Field {
    char* name;
    size_t name_size;
    Node* type_node;
    size_t offset;
    size_t line;
    size_t column;
} Field;

typedef struct FieldList {
    Field* fields;
    size_t count;
    size_t capacity;
} FieldList;

typedef struct FieldInit {
    char* name;
    size_t name_size;
    Node* value;
    size_t offset;
    size_t line;
    size_t column;
} FieldInit;

typedef struct FieldInitList {
    FieldInit* inits;
    size_t count;
    size_t capacity;
} FieldInitList;

typedef struct ElseIfBranch {
    Node* condition;
    NodeList body;
    size_t offset;
    size_t line;
    size_t column;
} ElseIfBranch;

typedef struct ElseIfList {
    ElseIfBranch* branches;
    size_t count;
    size_t capacity;
} ElseIfList;

typedef struct MatchCase {
    NodeList values;
    NodeList body;
    size_t offset;
    size_t line;
    size_t column;
} MatchCase;

typedef struct MatchCaseList {
    MatchCase* cases;
    size_t count;
    size_t capacity;
} MatchCaseList;

struct Node {
    NodeType type;
    size_t offset;
    size_t line;
    size_t column;
    void* resolved_type;

    union {
        struct { NodeList declarations; } program;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            Node* type_node;
            Node* value;
        } const_decl;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            Node* type_node;
            Node* value;
        } var_decl;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            ParamList params;
            Node* return_type;
            NodeList body;
        } func_decl;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            FieldList fields;
            NodeList methods;
        } struct_decl;

        struct {
            char* name;
            size_t name_size;
            NodeList method_sigs;
        } interface_decl;

        struct {
            Node* value;
        } return_stmt;

        struct {
            Node* condition;
            NodeList then_body;
            ElseIfList elseifs;
            NodeList else_body;
        } if_stmt;

        struct {
            char* var_name;
            size_t var_name_size;
            Node* start;
            Node* end;
            Node* step;
            NodeList body;
        } for_stmt;

        struct {
            Node* condition;
            NodeList body;
        } while_stmt;

        struct {
            Node* subject;
            MatchCaseList cases;
            NodeList else_body;
        } match_stmt;

        struct {
            Node* target;
            Node* value;
        } assign_stmt;

        struct {
            TokenType op;
            Node* target;
            Node* value;
        } compound_assign_stmt;

        struct {
            Node* expr;
        } expr_stmt;

        struct { char* value; size_t value_size; } integer_literal;
        struct { char* value; size_t value_size; } float_literal;
        struct { char* value; size_t value_size; } string_literal;
        struct { bool value; } bool_literal;
        struct { char* name; size_t name_size; } identifier;

        struct { TokenType op; Node* left; Node* right; } binary_expr;
        struct { TokenType op; Node* operand; } unary_expr;
        struct { Node* inner; } paren_expr;
        struct { Node* callee; NodeList args; } call_expr;
        struct { Node* object; char* field_name; size_t field_name_size; } field_access;
        struct {
            Node* object;
            char* method_name;
            size_t method_name_size;
            NodeList args;
        } method_call;
        struct {
            char* struct_name;
            size_t struct_name_size;
            FieldInitList fields;
        } struct_literal;

        struct { char* name; size_t name_size; } type_simple;
        struct { Node* inner; } type_ref;
        struct { Node* inner; } type_ptr;
    } as;
};

#endif
