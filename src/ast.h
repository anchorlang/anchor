#ifndef ANCC_AST_H
#define ANCC_AST_H

#include "lexer.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum NodeType {
    // program root
    NODE_PROGRAM,

    // declarations
    NODE_IMPORT_DECL,
    NODE_CONST_DECL,
    NODE_VAR_DECL,
    NODE_FUNC_DECL,
    NODE_STRUCT_DECL,
    NODE_INTERFACE_DECL,

    NODE_ENUM_DECL,

    // statements
    NODE_RETURN_STMT,
    NODE_IF_STMT,
    NODE_FOR_STMT,
    NODE_WHILE_STMT,
    NODE_BREAK_STMT,
    NODE_CONTINUE_STMT,
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
    NODE_CAST_EXPR,
    NODE_SIZEOF_EXPR,
    NODE_ARRAY_LITERAL,
    NODE_INDEX_EXPR,

    // types
    NODE_TYPE_SIMPLE,
    NODE_TYPE_REFERENCE,
    NODE_TYPE_POINTER,
    NODE_TYPE_ARRAY,
    NODE_TYPE_SLICE,
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

typedef struct EnumVariant {
    char* name;
    size_t name_size;
    size_t offset;
    size_t line;
    size_t column;
} EnumVariant;

typedef struct EnumVariantList {
    EnumVariant* variants;
    size_t count;
    size_t capacity;
} EnumVariantList;

typedef struct TypeParam {
    char* name;
    size_t name_size;
} TypeParam;

typedef struct TypeParamList {
    TypeParam* params;
    size_t count;
    size_t capacity;
} TypeParamList;

typedef struct ImportName {
    char* name;
    size_t name_size;
    size_t offset;
    size_t line;
    size_t column;
} ImportName;

typedef struct ImportNameList {
    ImportName* names;
    size_t count;
    size_t capacity;
} ImportNameList;

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
            char* module_path;
            size_t module_path_size;
            ImportNameList names;
        } import_decl;

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
            bool is_extern;
            char* name;
            size_t name_size;
            TypeParamList type_params;
            ParamList params;
            Node* return_type;
            NodeList body;
        } func_decl;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            TypeParamList type_params;
            FieldList fields;
            NodeList methods;
        } struct_decl;

        struct {
            char* name;
            size_t name_size;
            NodeList method_sigs;
        } interface_decl;

        struct {
            bool is_export;
            char* name;
            size_t name_size;
            EnumVariantList variants;
        } enum_decl;

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
        struct { Node* callee; NodeList type_args; NodeList args; } call_expr;
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
            NodeList type_args;
            FieldInitList fields;
        } struct_literal;
        struct { Node* expr; Node* target_type; } cast_expr;
        struct { Node* type_node; } sizeof_expr;
        struct { NodeList elements; } array_literal;
        struct { Node* object; Node* index; } index_expr;

        struct { char* name; size_t name_size; NodeList type_args; } type_simple;
        struct { Node* inner; } type_ref;
        struct { Node* inner; } type_ptr;
        struct { Node* inner; Node* size_expr; } type_array;
        struct { Node* inner; } type_slice;
    } as;
};

#endif
