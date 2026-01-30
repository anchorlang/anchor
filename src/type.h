#ifndef ANCC_TYPE_H
#define ANCC_TYPE_H

#include "arena.h"
#include "ast.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Module Module;
typedef struct Type Type;

typedef enum TypeKind {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_BYTE,
    TYPE_SHORT,
    TYPE_USHORT,
    TYPE_INT,
    TYPE_UINT,
    TYPE_LONG,
    TYPE_ULONG,
    TYPE_ISIZE,
    TYPE_USIZE,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_STRING,
    TYPE_STRUCT,
    TYPE_INTERFACE,
    TYPE_FUNC,
    TYPE_REF,
    TYPE_PTR,
} TypeKind;

struct Type {
    TypeKind kind;
    union {
        struct {
            char* name;
            size_t name_size;
            Module* module;
            FieldList* fields;
            NodeList* methods;
        } struct_type;

        struct {
            char* name;
            size_t name_size;
            NodeList* method_sigs;
        } interface_type;

        struct {
            Type** param_types;
            int param_count;
            Type* return_type;
        } func_type;

        struct {
            Type* inner;
        } ref_type;

        struct {
            Type* inner;
        } ptr_type;
    } as;
};

typedef struct TypeRegistry {
    Arena* arena;
    Type* type_void;
    Type* type_bool;
    Type* type_byte;
    Type* type_short;
    Type* type_ushort;
    Type* type_int;
    Type* type_uint;
    Type* type_long;
    Type* type_ulong;
    Type* type_isize;
    Type* type_usize;
    Type* type_float;
    Type* type_double;
    Type* type_string;
} TypeRegistry;

void type_registry_init(TypeRegistry* reg, Arena* arena);

Type* type_void(TypeRegistry* reg);
Type* type_bool(TypeRegistry* reg);
Type* type_byte(TypeRegistry* reg);
Type* type_short(TypeRegistry* reg);
Type* type_ushort(TypeRegistry* reg);
Type* type_int(TypeRegistry* reg);
Type* type_uint(TypeRegistry* reg);
Type* type_long(TypeRegistry* reg);
Type* type_ulong(TypeRegistry* reg);
Type* type_isize(TypeRegistry* reg);
Type* type_usize(TypeRegistry* reg);
Type* type_float(TypeRegistry* reg);
Type* type_double(TypeRegistry* reg);
Type* type_string(TypeRegistry* reg);

Type* type_struct(TypeRegistry* reg, char* name, size_t name_size,
                  Module* module, FieldList* fields, NodeList* methods);
Type* type_interface(TypeRegistry* reg, char* name, size_t name_size,
                     NodeList* method_sigs);
Type* type_func(TypeRegistry* reg, Type** param_types, int param_count,
                Type* return_type);
Type* type_ref(TypeRegistry* reg, Type* inner);
Type* type_ptr(TypeRegistry* reg, Type* inner);

const char* type_name(Type* type);
bool type_equals(Type* a, Type* b);
bool type_is_numeric(Type* type);
bool type_is_integer(Type* type);
int type_integer_rank(Type* type);
bool type_integer_convertible(Type* from, Type* to);

#endif
