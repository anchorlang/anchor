#include "type.h"

#include <stdio.h>
#include <string.h>

static Type* make_primitive(Arena* arena, TypeKind kind) {
    Type* t = arena_alloc(arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    return t;
}

void type_registry_init(TypeRegistry* reg, Arena* arena) {
    reg->arena = arena;
    reg->type_void = make_primitive(arena, TYPE_VOID);
    reg->type_bool = make_primitive(arena, TYPE_BOOL);
    reg->type_byte = make_primitive(arena, TYPE_BYTE);
    reg->type_short = make_primitive(arena, TYPE_SHORT);
    reg->type_ushort = make_primitive(arena, TYPE_USHORT);
    reg->type_int = make_primitive(arena, TYPE_INT);
    reg->type_uint = make_primitive(arena, TYPE_UINT);
    reg->type_long = make_primitive(arena, TYPE_LONG);
    reg->type_ulong = make_primitive(arena, TYPE_ULONG);
    reg->type_isize = make_primitive(arena, TYPE_ISIZE);
    reg->type_usize = make_primitive(arena, TYPE_USIZE);
    reg->type_float = make_primitive(arena, TYPE_FLOAT);
    reg->type_double = make_primitive(arena, TYPE_DOUBLE);
    reg->type_string = make_primitive(arena, TYPE_STRING);
}

Type* type_void(TypeRegistry* reg) { return reg->type_void; }
Type* type_bool(TypeRegistry* reg) { return reg->type_bool; }
Type* type_byte(TypeRegistry* reg) { return reg->type_byte; }
Type* type_short(TypeRegistry* reg) { return reg->type_short; }
Type* type_ushort(TypeRegistry* reg) { return reg->type_ushort; }
Type* type_int(TypeRegistry* reg) { return reg->type_int; }
Type* type_uint(TypeRegistry* reg) { return reg->type_uint; }
Type* type_long(TypeRegistry* reg) { return reg->type_long; }
Type* type_ulong(TypeRegistry* reg) { return reg->type_ulong; }
Type* type_isize(TypeRegistry* reg) { return reg->type_isize; }
Type* type_usize(TypeRegistry* reg) { return reg->type_usize; }
Type* type_float(TypeRegistry* reg) { return reg->type_float; }
Type* type_double(TypeRegistry* reg) { return reg->type_double; }
Type* type_string(TypeRegistry* reg) { return reg->type_string; }

Type* type_struct(TypeRegistry* reg, char* name, size_t name_size,
                  Module* module, FieldList* fields, NodeList* methods) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_STRUCT;
    t->as.struct_type.name = name;
    t->as.struct_type.name_size = name_size;
    t->as.struct_type.module = module;
    t->as.struct_type.fields = fields;
    t->as.struct_type.methods = methods;
    return t;
}

Type* type_interface(TypeRegistry* reg, char* name, size_t name_size,
                     NodeList* method_sigs) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_INTERFACE;
    t->as.interface_type.name = name;
    t->as.interface_type.name_size = name_size;
    t->as.interface_type.method_sigs = method_sigs;
    return t;
}

Type* type_func(TypeRegistry* reg, Type** param_types, int param_count,
                Type* return_type) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_FUNC;
    t->as.func_type.param_types = param_types;
    t->as.func_type.param_count = param_count;
    t->as.func_type.return_type = return_type;
    return t;
}

Type* type_ref(TypeRegistry* reg, Type* inner) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_REF;
    t->as.ref_type.inner = inner;
    return t;
}

Type* type_ptr(TypeRegistry* reg, Type* inner) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_PTR;
    t->as.ptr_type.inner = inner;
    return t;
}

Type* type_array(TypeRegistry* reg, Type* element, int size) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_ARRAY;
    t->as.array_type.element = element;
    t->as.array_type.size = size;
    return t;
}

Type* type_slice(TypeRegistry* reg, Type* element) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_SLICE;
    t->as.slice_type.element = element;
    return t;
}

Type* type_enum(TypeRegistry* reg, char* name, size_t name_size,
                Module* module, EnumVariantList* variants) {
    Type* t = arena_alloc(reg->arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = TYPE_ENUM;
    t->as.enum_type.name = name;
    t->as.enum_type.name_size = name_size;
    t->as.enum_type.module = module;
    t->as.enum_type.variants = variants;
    return t;
}

static int type_name_write(Type* type, char* buf, int size) {
    if (!type) return snprintf(buf, size, "?");

    switch (type->kind) {
    case TYPE_VOID:   return snprintf(buf, size, "void");
    case TYPE_BOOL:   return snprintf(buf, size, "bool");
    case TYPE_BYTE:   return snprintf(buf, size, "byte");
    case TYPE_SHORT:  return snprintf(buf, size, "short");
    case TYPE_USHORT: return snprintf(buf, size, "ushort");
    case TYPE_INT:    return snprintf(buf, size, "int");
    case TYPE_UINT:   return snprintf(buf, size, "uint");
    case TYPE_LONG:   return snprintf(buf, size, "long");
    case TYPE_ULONG:  return snprintf(buf, size, "ulong");
    case TYPE_ISIZE:  return snprintf(buf, size, "isize");
    case TYPE_USIZE:  return snprintf(buf, size, "usize");
    case TYPE_FLOAT:  return snprintf(buf, size, "float");
    case TYPE_DOUBLE: return snprintf(buf, size, "double");
    case TYPE_STRING: return snprintf(buf, size, "string");
    case TYPE_STRUCT:
        return snprintf(buf, size, "%.*s",
                        (int)type->as.struct_type.name_size, type->as.struct_type.name);
    case TYPE_INTERFACE:
        return snprintf(buf, size, "%.*s",
                        (int)type->as.interface_type.name_size, type->as.interface_type.name);
    case TYPE_FUNC: {
        int pos = 0;
        pos += snprintf(buf + pos, size - pos, "(");
        for (int i = 0; i < type->as.func_type.param_count; i++) {
            if (i > 0) pos += snprintf(buf + pos, size - pos, ", ");
            pos += type_name_write(type->as.func_type.param_types[i], buf + pos, size - pos);
        }
        pos += snprintf(buf + pos, size - pos, ") -> ");
        pos += type_name_write(type->as.func_type.return_type, buf + pos, size - pos);
        return pos;
    }
    case TYPE_REF: {
        int pos = snprintf(buf, size, "&");
        pos += type_name_write(type->as.ref_type.inner, buf + pos, size - pos);
        return pos;
    }
    case TYPE_PTR: {
        int pos = snprintf(buf, size, "*");
        pos += type_name_write(type->as.ptr_type.inner, buf + pos, size - pos);
        return pos;
    }
    case TYPE_ARRAY: {
        int pos = type_name_write(type->as.array_type.element, buf, size);
        pos += snprintf(buf + pos, size - pos, "[%d]", type->as.array_type.size);
        return pos;
    }
    case TYPE_SLICE: {
        int pos = type_name_write(type->as.slice_type.element, buf, size);
        pos += snprintf(buf + pos, size - pos, "[]");
        return pos;
    }
    case TYPE_ENUM:
        return snprintf(buf, size, "%.*s",
                        (int)type->as.enum_type.name_size, type->as.enum_type.name);
    }
    return snprintf(buf, size, "?");
}

const char* type_name(Type* type) {
    static char bufs[4][256];
    static int idx = 0;
    char* buf = bufs[idx];
    idx = (idx + 1) % 4;
    type_name_write(type, buf, 256);
    return buf;
}

bool type_equals(Type* a, Type* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
    case TYPE_REF:
        return type_equals(a->as.ref_type.inner, b->as.ref_type.inner);
    case TYPE_PTR:
        return type_equals(a->as.ptr_type.inner, b->as.ptr_type.inner);
    case TYPE_FUNC: {
        if (a->as.func_type.param_count != b->as.func_type.param_count) return false;
        if (!type_equals(a->as.func_type.return_type, b->as.func_type.return_type)) return false;
        for (int i = 0; i < a->as.func_type.param_count; i++) {
            if (!type_equals(a->as.func_type.param_types[i], b->as.func_type.param_types[i]))
                return false;
        }
        return true;
    }
    case TYPE_ARRAY:
        return type_equals(a->as.array_type.element, b->as.array_type.element)
            && a->as.array_type.size == b->as.array_type.size;
    case TYPE_SLICE:
        return type_equals(a->as.slice_type.element, b->as.slice_type.element);
    default:
        // primitives, structs, interfaces: pointer equality already checked above
        return false;
    }
}

bool type_is_integer(Type* type) {
    if (!type) return false;
    switch (type->kind) {
    case TYPE_BYTE:
    case TYPE_SHORT:
    case TYPE_USHORT:
    case TYPE_INT:
    case TYPE_UINT:
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_ISIZE:
    case TYPE_USIZE:
        return true;
    default:
        return false;
    }
}

bool type_is_numeric(Type* type) {
    if (!type) return false;
    return type_is_integer(type) || type->kind == TYPE_FLOAT || type->kind == TYPE_DOUBLE;
}

// Returns the bit-width rank of an integer type (for implicit conversion checks).
// Higher rank = wider type. isize/usize ranked same as long/ulong (pointer-width).
int type_integer_rank(Type* type) {
    if (!type) return -1;
    switch (type->kind) {
    case TYPE_BYTE:   return 1;
    case TYPE_SHORT:  return 2;
    case TYPE_USHORT: return 2;
    case TYPE_INT:    return 3;
    case TYPE_UINT:   return 3;
    case TYPE_ISIZE:  return 4;
    case TYPE_USIZE:  return 4;
    case TYPE_LONG:   return 4;
    case TYPE_ULONG:  return 4;
    default:          return -1;
    }
}

// C99-style implicit integer conversion: widening is allowed, narrowing is not.
// Same-rank conversions (e.g. int<->uint, long<->ulong, isize<->usize) are allowed.
bool type_integer_convertible(Type* from, Type* to) {
    if (!from || !to) return false;
    if (!type_is_integer(from) || !type_is_integer(to)) return false;
    return type_integer_rank(from) <= type_integer_rank(to);
}
