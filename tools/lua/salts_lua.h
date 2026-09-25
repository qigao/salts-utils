/**
 * @file salts_lua.h
 * @brief Lightweight C11 Preprocessor and _Generic based Lua binding library.
 *
 * Provides type-generic value pushing/getting, strict checked extraction,
 * function registration, named execution environments, registry references,
 * protected global calls, and typed ordinary-C-function adapters.
 *
 * STRING LIFETIME & OWNERSHIP MODEL:
 *
 * - const char*: BORROWED - Valid only while Lua value remains on stack.
 *                ⚠️ UNSAFE after lua_pop/lua_settop/stack modification!
 *                Use only for immediate consumption within same Lua call.
 *
 * - tstr:      OWNED - Independent lifetime, caller must tstr_free().
 *                ✅ SAFE to use after stack changes, across function boundaries.
 *                Recommended for struct fields that outlive Lua call.
 *
 * - vstr:      BORROWED VIEW - Non-owning reference to Lua string.
 *                ⚠️ Same lifetime constraints as const char*.
 *                Use for read-only, short-lived access with O(1) length.
 *
 * MEMORY MANAGEMENT:
 *
 * - Structs with tstr fields always require manual cleanup.
 * - Use StructName_cleanup() to free owned tstr fields.
 * - StructName_from_lua_arena() copies const char* and vstr fields into a
 *   MemoryPool; those fields become invalid after pool_reset()/pool_destroy().
 */
#ifndef SALTS_LUA_H
#define SALTS_LUA_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include <cmeta/pp.h>

#include "salts_error.h"
#include "tstr.h"
#include "vstr.h"
#include "memory_pool.h"


#ifdef __cplusplus
extern "C" {
#endif

#if defined(TBE_TYPED_H)
#include <cmeta/data.h>

static inline int c11_lua_tbe_scalar_matches(
    const cmeta_data_desc *data, const cmeta_data_desc *canonical) {
    if (!cmeta_data_desc_valid(data) || !cmeta_data_desc_valid(canonical) ||
        data->kind != canonical->kind ||
        !cmeta_type_equal(data->storage_type, canonical->storage_type) ||
        data->storage_type->kind != canonical->storage_type->kind ||
        data->storage_type->size != canonical->storage_type->size ||
        data->storage_type->align != canonical->storage_type->align)
        return 0;
    if (data->kind == CMETA_DATA_SINT || data->kind == CMETA_DATA_UINT)
        return ((const cmeta_data_integer_shape *)data->shape)->bits ==
               ((const cmeta_data_integer_shape *)canonical->shape)->bits;
    if (data->kind == CMETA_DATA_FLOAT)
        return ((const cmeta_data_float_shape *)data->shape)->bits ==
               ((const cmeta_data_float_shape *)canonical->shape)->bits;
    return 0;
}

static inline int c11_lua_tbe_uint_pushable(uint64_t value) {
    return value <= (uint64_t)LUA_MAXINTEGER;
}

static inline int c11_lua_tbe_uint_readable(lua_Integer value,
                                            uint64_t maximum) {
    return value >= 0 && (uint64_t)value <= maximum;
}

static inline DataBindStatus c11_lua_tbe_push_value(
    lua_State *L, const cmeta_data_desc *data, const TbeTypedType *overlay,
    const void *object, size_t depth, size_t max_depth);

static inline DataBindStatus c11_lua_tbe_push_scalar(
    lua_State *L, const cmeta_data_desc *data, const void *object) {
#define C11_LUA_TBE_PUSH_SIGNED(CANONICAL, TYPE) do {                            \
    if (c11_lua_tbe_scalar_matches(data, &(CANONICAL))) {                       \
        TYPE value;                                                              \
        memcpy(&value, object, sizeof(value));                                   \
        lua_pushinteger(L, (lua_Integer)value);                                  \
        return DATA_BIND_OK;                                                     \
    }                                                                            \
} while (0)
#define C11_LUA_TBE_PUSH_UNSIGNED(CANONICAL, TYPE) do {                          \
    if (c11_lua_tbe_scalar_matches(data, &(CANONICAL))) {                       \
        TYPE value;                                                              \
        memcpy(&value, object, sizeof(value));                                   \
        if (!c11_lua_tbe_uint_pushable((uint64_t)value))                         \
            return DATA_BIND_ERR_TYPE_MISMATCH;                                  \
        lua_pushinteger(L, (lua_Integer)value);                                  \
        return DATA_BIND_OK;                                                     \
    }                                                                            \
} while (0)
    C11_LUA_TBE_PUSH_SIGNED(cmeta_data_int8, int8_t);
    C11_LUA_TBE_PUSH_UNSIGNED(cmeta_data_uint8, uint8_t);
    C11_LUA_TBE_PUSH_SIGNED(cmeta_data_int16, int16_t);
    C11_LUA_TBE_PUSH_UNSIGNED(cmeta_data_uint16, uint16_t);
    C11_LUA_TBE_PUSH_SIGNED(cmeta_data_int32, int32_t);
    C11_LUA_TBE_PUSH_UNSIGNED(cmeta_data_uint32, uint32_t);
    C11_LUA_TBE_PUSH_SIGNED(cmeta_data_int64, int64_t);
    C11_LUA_TBE_PUSH_UNSIGNED(cmeta_data_uint64, uint64_t);
#undef C11_LUA_TBE_PUSH_UNSIGNED
#undef C11_LUA_TBE_PUSH_SIGNED
    if (c11_lua_tbe_scalar_matches(data, &cmeta_data_float)) {
        float value;
        memcpy(&value, object, sizeof(value));
        if (!isfinite(value)) return DATA_BIND_ERR_TYPE_MISMATCH;
        lua_pushnumber(L, (lua_Number)value);
        return DATA_BIND_OK;
    }
    if (c11_lua_tbe_scalar_matches(data, &cmeta_data_double)) {
        double value;
        memcpy(&value, object, sizeof(value));
        if (!isfinite(value)) return DATA_BIND_ERR_TYPE_MISMATCH;
        lua_pushnumber(L, (lua_Number)value);
        return DATA_BIND_OK;
    }
    if (data != NULL && data->kind == CMETA_DATA_ENUM) {
        const cmeta_data_enum_shape *shape =
            (const cmeta_data_enum_shape *)data->shape;
        int64_t value;
        const char *text;
        if (cmeta_data_enum_read(data, object, &value) != CMETA_OK)
            return DATA_BIND_ERR_TYPE_MISMATCH;
        text = shape != NULL ? cmeta_enum_to_string(shape->meta, value) : NULL;
        if (text == NULL) return DATA_BIND_ERR_TYPE_MISMATCH;
        lua_pushstring(L, text);
        return DATA_BIND_OK;
    }
    return DATA_BIND_ERR_SCHEMA;
}

static inline DataBindStatus c11_lua_tbe_push_value(
    lua_State *L, const cmeta_data_desc *data, const TbeTypedType *overlay,
    const void *object, size_t depth, size_t max_depth) {
    const cmeta_data_struct_shape *shape;
    int top;
    size_t i;
    if (data == NULL || object == NULL) return DATA_BIND_ERR_INVALID_ARG;
    if (data->kind != CMETA_DATA_STRUCT)
        return c11_lua_tbe_push_scalar(L, data, object);
    if (overlay == NULL) return DATA_BIND_ERR_SCHEMA;
    if (depth > max_depth) return DATA_BIND_ERR_LIMIT;
    shape = (const cmeta_data_struct_shape *)data->shape;
    if (shape == NULL || shape->field_count != overlay->field_count)
        return DATA_BIND_ERR_SCHEMA;
    top = lua_gettop(L);
    lua_createtable(L, 0, (int)shape->field_count);
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        const TbeTypedField *wire = &overlay->fields[i];
        const TbeTypedType *nested =
            field->value->kind == CMETA_DATA_STRUCT ? wire->nested_overlay : NULL;
        DataBindStatus status;
        if (wire->name == NULL || wire->name[0] == '\0') {
            lua_settop(L, top);
            return DATA_BIND_ERR_SCHEMA;
        }
        status = c11_lua_tbe_push_value(
            L, field->value, nested,
            (const uint8_t *)object + field->offset, depth + 1u, max_depth);
        if (status != DATA_BIND_OK) {
            lua_settop(L, top);
            return status;
        }
        lua_setfield(L, -2, wire->name);
    }
    return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_push_tbe_typed_descriptor(
    lua_State *L, const TbeTypedDescriptor *descriptor,
    const void *object, size_t max_depth) {
    if (L == NULL || descriptor == NULL || object == NULL)
        return DATA_BIND_ERR_INVALID_ARG;
    if (tbe_typed_descriptor_validate(descriptor, NULL) != DATA_BIND_OK)
        return DATA_BIND_ERR_SCHEMA;
    return c11_lua_tbe_push_value(
        L, descriptor->native_data, descriptor->overlay, object, 0u, max_depth);
}

static inline DataBindStatus c11_lua_tbe_read_value(
    lua_State *L, int index, const cmeta_data_desc *data,
    const TbeTypedType *overlay, void *object, size_t depth,
    size_t max_depth);

static inline DataBindStatus c11_lua_tbe_read_scalar(
    lua_State *L, int index, const cmeta_data_desc *data, void *object) {
    lua_Integer raw;
#define C11_LUA_TBE_READ_SIGNED(CANONICAL, TYPE, MINIMUM, MAXIMUM) do {           \
    if (c11_lua_tbe_scalar_matches(data, &(CANONICAL))) {                       \
        TYPE value;                                                              \
        if (!lua_isinteger(L, index)) return DATA_BIND_ERR_TYPE_MISMATCH;        \
        raw = lua_tointeger(L, index);                                            \
        if (raw < (lua_Integer)(MINIMUM) || raw > (lua_Integer)(MAXIMUM))        \
            return DATA_BIND_ERR_TYPE_MISMATCH;                                  \
        value = (TYPE)raw;                                                        \
        memcpy(object, &value, sizeof(value));                                   \
        return DATA_BIND_OK;                                                     \
    }                                                                            \
} while (0)
#define C11_LUA_TBE_READ_UNSIGNED(CANONICAL, TYPE, MAXIMUM) do {                  \
    if (c11_lua_tbe_scalar_matches(data, &(CANONICAL))) {                       \
        TYPE value;                                                              \
        if (!lua_isinteger(L, index)) return DATA_BIND_ERR_TYPE_MISMATCH;        \
        raw = lua_tointeger(L, index);                                            \
        if (!c11_lua_tbe_uint_readable(raw, (uint64_t)(MAXIMUM)))                \
            return DATA_BIND_ERR_TYPE_MISMATCH;                                  \
        value = (TYPE)raw;                                                        \
        memcpy(object, &value, sizeof(value));                                   \
        return DATA_BIND_OK;                                                     \
    }                                                                            \
} while (0)
    C11_LUA_TBE_READ_SIGNED(cmeta_data_int8, int8_t, INT8_MIN, INT8_MAX);
    C11_LUA_TBE_READ_UNSIGNED(cmeta_data_uint8, uint8_t, UINT8_MAX);
    C11_LUA_TBE_READ_SIGNED(cmeta_data_int16, int16_t, INT16_MIN, INT16_MAX);
    C11_LUA_TBE_READ_UNSIGNED(cmeta_data_uint16, uint16_t, UINT16_MAX);
    C11_LUA_TBE_READ_SIGNED(cmeta_data_int32, int32_t, INT32_MIN, INT32_MAX);
    C11_LUA_TBE_READ_UNSIGNED(cmeta_data_uint32, uint32_t, UINT32_MAX);
    C11_LUA_TBE_READ_SIGNED(cmeta_data_int64, int64_t, INT64_MIN, INT64_MAX);
    C11_LUA_TBE_READ_UNSIGNED(cmeta_data_uint64, uint64_t, UINT64_MAX);
#undef C11_LUA_TBE_READ_UNSIGNED
#undef C11_LUA_TBE_READ_SIGNED
    if (c11_lua_tbe_scalar_matches(data, &cmeta_data_float)) {
        lua_Number raw_number;
        float value;
        if (!lua_isnumber(L, index)) return DATA_BIND_ERR_TYPE_MISMATCH;
        raw_number = lua_tonumber(L, index);
        if (!isfinite((double)raw_number) || raw_number < -(lua_Number)FLT_MAX ||
            raw_number > (lua_Number)FLT_MAX)
            return DATA_BIND_ERR_TYPE_MISMATCH;
        value = (float)raw_number;
        memcpy(object, &value, sizeof(value));
        return DATA_BIND_OK;
    }
    if (c11_lua_tbe_scalar_matches(data, &cmeta_data_double)) {
        lua_Number raw_number;
        double value;
        if (!lua_isnumber(L, index)) return DATA_BIND_ERR_TYPE_MISMATCH;
        raw_number = lua_tonumber(L, index);
        value = (double)raw_number;
        if (!isfinite(value)) return DATA_BIND_ERR_TYPE_MISMATCH;
        memcpy(object, &value, sizeof(value));
        return DATA_BIND_OK;
    }
    if (data != NULL && data->kind == CMETA_DATA_ENUM) {
        const cmeta_data_enum_shape *shape =
            (const cmeta_data_enum_shape *)data->shape;
        int64_t value;
        if (lua_type(L, index) == LUA_TSTRING) {
            if (shape == NULL ||
                !cmeta_enum_from_string(shape->meta, lua_tostring(L, index), &value))
                return DATA_BIND_ERR_TYPE_MISMATCH;
        } else if (lua_isinteger(L, index)) {
            value = (int64_t)lua_tointeger(L, index);
        } else {
            return DATA_BIND_ERR_TYPE_MISMATCH;
        }
        return cmeta_data_enum_assign(data, object, value) == CMETA_OK
                   ? DATA_BIND_OK : DATA_BIND_ERR_TYPE_MISMATCH;
    }
    return DATA_BIND_ERR_SCHEMA;
}

static inline DataBindStatus c11_lua_tbe_read_value(
    lua_State *L, int index, const cmeta_data_desc *data,
    const TbeTypedType *overlay, void *object, size_t depth,
    size_t max_depth) {
    const cmeta_data_struct_shape *shape;
    int table_index;
    size_t i;
    if (data == NULL || object == NULL) return DATA_BIND_ERR_INVALID_ARG;
    if (data->kind != CMETA_DATA_STRUCT)
        return c11_lua_tbe_read_scalar(L, index, data, object);
    if (overlay == NULL || !lua_istable(L, index))
        return DATA_BIND_ERR_TYPE_MISMATCH;
    if (depth > max_depth) return DATA_BIND_ERR_LIMIT;
    shape = (const cmeta_data_struct_shape *)data->shape;
    if (shape == NULL || shape->field_count != overlay->field_count)
        return DATA_BIND_ERR_SCHEMA;
    table_index = lua_absindex(L, index);
    for (i = 0u; i < shape->field_count; ++i) {
        const cmeta_data_field_desc *field = &shape->fields[i];
        const TbeTypedField *wire = &overlay->fields[i];
        const TbeTypedType *nested =
            field->value->kind == CMETA_DATA_STRUCT ? wire->nested_overlay : NULL;
        DataBindStatus status;
        if (wire->name == NULL || wire->name[0] == '\0')
            return DATA_BIND_ERR_SCHEMA;
        lua_getfield(L, table_index, wire->name);
        status = lua_isnil(L, -1)
                     ? DATA_BIND_ERR_TYPE_MISMATCH
                     : c11_lua_tbe_read_value(
                           L, -1, field->value, nested,
                           (uint8_t *)object + field->offset,
                           depth + 1u, max_depth);
        lua_pop(L, 1);
        if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_read_tbe_typed_descriptor(
    lua_State *L, int index, const TbeTypedDescriptor *descriptor,
    void *object, size_t max_depth, size_t max_dynamic_items) {
    void *temporary;
    DataBindStatus status;
    (void)max_dynamic_items;
    if (L == NULL || descriptor == NULL || object == NULL)
        return DATA_BIND_ERR_INVALID_ARG;
    if (tbe_typed_descriptor_validate(descriptor, NULL) != DATA_BIND_OK)
        return DATA_BIND_ERR_SCHEMA;
    temporary = malloc(descriptor->native_data->storage_type->size);
    if (temporary == NULL) return DATA_BIND_ERR_OOM;
    status = tbe_typed_descriptor_init(descriptor, temporary, NULL);
    if (status == DATA_BIND_OK)
        status = c11_lua_tbe_read_value(
            L, index, descriptor->native_data, descriptor->overlay,
            temporary, 0u, max_depth);
    if (status == DATA_BIND_OK)
        status = tbe_typed_descriptor_clear(descriptor, object, NULL);
    if (status == DATA_BIND_OK) {
        memcpy(object, temporary, descriptor->native_data->storage_type->size);
        memset(temporary, 0, descriptor->native_data->storage_type->size);
    }
    (void)tbe_typed_descriptor_clear(descriptor, temporary, NULL);
    free(temporary);
    return status;
}
#endif /* TBE_TYPED_H */

/* The legacy flat typed-function declaration can contain 20 tokens, beyond
 * CMeta's 16-item public iteration contract. Keep this compatibility-only
 * counter until that public declaration form can be retired. */
#define C11_COUNT_ARGS(...) \
    C11_COUNT_ARGS_HELPER(__VA_ARGS__, \
        32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, \
        16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define C11_COUNT_ARGS_HELPER( \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, \
    _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, \
    _31, _32, N, ...) N

/* -------------------------------------------------------------------------
 * Type-Generic Value Pushing (C11 _Generic)
 * ------------------------------------------------------------------------- */
static inline void c11_lua_push_bool(lua_State* L, bool v) { lua_pushboolean(L, v ? 1 : 0); }
static inline void c11_lua_push_int(lua_State* L, long long v) { lua_pushinteger(L, (lua_Integer)v); }
static inline void c11_lua_push_num(lua_State* L, double v) { lua_pushnumber(L, (lua_Number)v); }

/**
 * @brief Push C string to Lua (NULL converted to empty string)
 * @warning For NULL-as-nil semantics, use c11_lua_push_str_or_nil()
 */
static inline void c11_lua_push_str(lua_State* L, const char* v) { lua_pushstring(L, v ? v : ""); }

/**
 * @brief Push C string to Lua (NULL becomes nil)
 */
static inline void c11_lua_push_str_or_nil(lua_State* L, const char* v) {
    if (v) lua_pushstring(L, v);
    else lua_pushnil(L);
}

static inline void c11_lua_push_ptr(lua_State* L, void* v) { lua_pushlightuserdata(L, v); }

/**
 * @brief Push tstr to Lua (borrows data, does NOT transfer ownership)
 * @note The tstr remains owned by caller and must be freed separately
 */
static inline void c11_lua_push_tstr(lua_State* L, tstr s) {
    lua_pushstring(L, s ? s : "");
}

/**
 * @brief Push vstr to Lua (uses pushlstring for binary safety)
 */
static inline void c11_lua_push_vstr(lua_State* L, vstr v) {
    lua_pushlstring(L, v.data ? v.data : "", v.len);
}

/* Lua conversion policy is declared once and replayed into the public
 * _Generic entry points. Keep these schemas defined: those entry points
 * expand lazily in the including translation unit. */
#define C11_LUA_BOOLEAN_TYPE(M) \
    Schema(M, \
        (_Bool, _Bool*, c11_lua_push_bool, c11_lua_get_bool, c11_lua_get_checked_bool, c11_lua_get_arena_bool))

#define C11_LUA_NUMERIC_TYPES(M) \
    Schema(M, \
        (char,               char*,               c11_lua_push_int,  c11_lua_get_char,   c11_lua_get_checked_char,   c11_lua_get_arena_char), \
        (signed char,        signed char*,        c11_lua_push_int,  c11_lua_get_schar,  c11_lua_get_checked_schar,  c11_lua_get_arena_schar), \
        (unsigned char,      unsigned char*,      c11_lua_push_int,  c11_lua_get_uchar,  c11_lua_get_checked_uchar,  c11_lua_get_arena_uchar), \
        (short,              short*,              c11_lua_push_int,  c11_lua_get_short,  c11_lua_get_checked_short,  c11_lua_get_arena_short), \
        (unsigned short,     unsigned short*,     c11_lua_push_int,  c11_lua_get_ushort, c11_lua_get_checked_ushort, c11_lua_get_arena_ushort), \
        (int,                int*,                c11_lua_push_int,  c11_lua_get_int,    c11_lua_get_checked_int,    c11_lua_get_arena_int), \
        (unsigned int,       unsigned int*,       c11_lua_push_int,  c11_lua_get_uint,   c11_lua_get_checked_uint,   c11_lua_get_arena_uint), \
        (long,               long*,               c11_lua_push_int,  c11_lua_get_long,   c11_lua_get_checked_long,   c11_lua_get_arena_long), \
        (unsigned long,      unsigned long*,      c11_lua_push_int,  c11_lua_get_ulong,  c11_lua_get_checked_ulong,  c11_lua_get_arena_ulong), \
        (long long,          long long*,          c11_lua_push_int,  c11_lua_get_llong,  c11_lua_get_checked_llong,  c11_lua_get_arena_llong), \
        (unsigned long long, unsigned long long*, c11_lua_push_int,  c11_lua_get_ullong, c11_lua_get_checked_ullong, c11_lua_get_arena_ullong), \
        (float,              float*,              c11_lua_push_num,  c11_lua_get_float,  c11_lua_get_checked_float,  c11_lua_get_arena_float), \
        (double,             double*,             c11_lua_push_num,  c11_lua_get_double, c11_lua_get_checked_double, c11_lua_get_arena_double))

#define C11_LUA_STRING_TYPES(M) \
    Schema(M, \
        (tstr,        tstr*,        c11_lua_push_tstr, c11_lua_get_tstr, c11_lua_get_checked_tstr, c11_lua_get_arena_tstr_t), \
        (vstr,        vstr*,        c11_lua_push_vstr, c11_lua_get_vstr, c11_lua_get_checked_vstr, c11_lua_get_arena_vstr), \
        (const char*, const char**, c11_lua_push_str,  c11_lua_get_str,  c11_lua_get_checked_str,  c11_lua_get_arena_str))

#define C11_LUA_POINTER_TYPES(M) \
    Schema(M, \
        (void*, void**, c11_lua_push_ptr, c11_lua_get_ptr, c11_lua_get_checked_ptr))

#define C11_LUA_PUSH_FIRST_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) value_type: push_fn
#define C11_LUA_PUSH_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) , value_type: push_fn
#define C11_LUA_PUSH_POINTER_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn) , value_type: push_fn

/**
 * @brief Pushes a typed C value onto the Lua stack using C11 _Generic selection.
 */
#define c11_lua_push(L, val) _Generic((val), \
    Replay(C11_LUA_BOOLEAN_TYPE, C11_LUA_PUSH_FIRST_ASSOC) \
    Replay(C11_LUA_NUMERIC_TYPES, C11_LUA_PUSH_ASSOC) \
    Replay(C11_LUA_STRING_TYPES, C11_LUA_PUSH_ASSOC) \
    Replay(C11_LUA_POINTER_TYPES, C11_LUA_PUSH_POINTER_ASSOC) \
)(L, val)

/* -------------------------------------------------------------------------
 * Type-Generic Value Extraction (C11 _Generic)
 * ------------------------------------------------------------------------- */
static inline void c11_lua_get_bool(lua_State* L, int idx, bool* out) {
    if (out) *out = lua_toboolean(L, idx) != 0;
}

static inline void c11_lua_get_char(lua_State* L, int idx, char* out) {
    if (out) *out = (char)lua_tointeger(L, idx);
}

static inline void c11_lua_get_schar(lua_State* L, int idx, signed char* out) {
    if (out) *out = (signed char)lua_tointeger(L, idx);
}

static inline void c11_lua_get_uchar(lua_State* L, int idx, unsigned char* out) {
    if (out) *out = (unsigned char)lua_tointeger(L, idx);
}

static inline void c11_lua_get_short(lua_State* L, int idx, short* out) {
    if (out) *out = (short)lua_tointeger(L, idx);
}

static inline void c11_lua_get_ushort(lua_State* L, int idx, unsigned short* out) {
    if (out) *out = (unsigned short)lua_tointeger(L, idx);
}

static inline void c11_lua_get_int(lua_State* L, int idx, int* out) {
    if (out) *out = (int)lua_tointeger(L, idx);
}

static inline void c11_lua_get_uint(lua_State* L, int idx, unsigned int* out) {
    if (out) *out = (unsigned int)lua_tointeger(L, idx);
}

static inline void c11_lua_get_long(lua_State* L, int idx, long* out) {
    if (out) *out = (long)lua_tointeger(L, idx);
}

static inline void c11_lua_get_ulong(lua_State* L, int idx, unsigned long* out) {
    if (out) *out = (unsigned long)lua_tointeger(L, idx);
}

static inline void c11_lua_get_llong(lua_State* L, int idx, long long* out) {
    if (out) *out = (long long)lua_tointeger(L, idx);
}

static inline void c11_lua_get_ullong(lua_State* L, int idx, unsigned long long* out) {
    if (out) *out = (unsigned long long)lua_tointeger(L, idx);
}

static inline void c11_lua_get_float(lua_State* L, int idx, float* out) {
    if (out) *out = (float)lua_tonumber(L, idx);
}

static inline void c11_lua_get_double(lua_State* L, int idx, double* out) {
    if (out) *out = (double)lua_tonumber(L, idx);
}

/**
 * @brief Get BORROWED const char* from Lua
 * @warning Pointer valid ONLY while Lua value remains on stack.
 *          Do NOT use after lua_pop() or any stack-modifying operation!
 *          For persistent storage, use c11_lua_get_tstr() instead.
 */
static inline void c11_lua_get_str(lua_State* L, int idx, const char** out) {
    if (out) *out = lua_tostring(L, idx);
}

/**
 * @brief Get OWNED tstr from Lua (creates independent copy)
 * @note Caller must tstr_free() the result when done
 * @return NULL if Lua value is nil/not a string
 */
static inline void c11_lua_get_tstr(lua_State* L, int idx, tstr* out) {
    size_t len = 0;
    const char* str;

    if (!out) return;
    str = lua_tolstring(L, idx, &len);
    *out = str ? tstr_dup_len(str, len) : NULL;
}

/**
 * @brief Get BORROWED vstr from Lua (non-owning view)
 * @warning View valid ONLY while Lua value remains on stack.
 *          Same lifetime constraints as const char*.
 */
static inline void c11_lua_get_vstr(lua_State* L, int idx, vstr* out) {
    if (!out) return;
    size_t len;
    const char* str = lua_tolstring(L, idx, &len);
    *out = str ? vstr_from_buf(str, len) : vstr_from_buf("", 0);
}

static inline void c11_lua_get_ptr(lua_State* L, int idx, void** out) {
    if (out) *out = lua_touserdata(L, idx);
}

#define C11_LUA_GET_FIRST_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) output_type: get_fn
#define C11_LUA_GET_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) , output_type: get_fn
#define C11_LUA_GET_POINTER_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn) , output_type: get_fn

/**
 * @brief Extracts a value from the Lua stack into a typed pointer using C11 _Generic selection.
 */
#define c11_lua_get(L, idx, ptr) _Generic((ptr), \
    Replay(C11_LUA_BOOLEAN_TYPE, C11_LUA_GET_FIRST_ASSOC) \
    Replay(C11_LUA_NUMERIC_TYPES, C11_LUA_GET_ASSOC) \
    Replay(C11_LUA_STRING_TYPES, C11_LUA_GET_ASSOC) \
    Replay(C11_LUA_POINTER_TYPES, C11_LUA_GET_POINTER_ASSOC) \
)(L, idx, ptr)

/* -------------------------------------------------------------------------
 * Strict Value Extraction
 *
 * These helpers reject implicit Lua conversions and leave output unchanged on
 * failure. They return Salts error codes: SALTS_OK, SALTS_EINVAL,
 * SALTS_EPROTO, SALTS_ERANGE, or SALTS_ENOMEM.
 * ------------------------------------------------------------------------- */
static inline int c11_lua_checked_integer(lua_State* L, int idx, lua_Integer* out) {
    lua_Integer value;

    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return SALTS_EPROTO;
#if LUA_VERSION_NUM >= 503
    if (!lua_isinteger(L, idx)) return SALTS_EPROTO;
#else
    {
        lua_Number number = lua_tonumber(L, idx);
        value = lua_tointeger(L, idx);
        if ((lua_Number)value != number) return SALTS_EPROTO;
    }
#endif
    value = lua_tointeger(L, idx);
    *out = value;
    return SALTS_OK;
}

#define C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(name, type, min_value, max_value) \
    static inline int name(lua_State* L, int idx, type* out) { \
        lua_Integer value; \
        int rc; \
        if (!out) return SALTS_EINVAL; \
        rc = c11_lua_checked_integer(L, idx, &value); \
        if (rc != SALTS_OK) return rc; \
        if (value < (lua_Integer)(min_value) || value > (lua_Integer)(max_value)) \
            return SALTS_ERANGE; \
        *out = (type)value; \
        return SALTS_OK; \
    }

#define C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(name, type, max_value) \
    static inline int name(lua_State* L, int idx, type* out) { \
        lua_Integer value; \
        int rc; \
        if (!out) return SALTS_EINVAL; \
        rc = c11_lua_checked_integer(L, idx, &value); \
        if (rc != SALTS_OK) return rc; \
        if (value < 0 || (unsigned long long)value > (unsigned long long)(max_value)) \
            return SALTS_ERANGE; \
        *out = (type)value; \
        return SALTS_OK; \
    }

C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_char, char, CHAR_MIN, CHAR_MAX)
C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_schar, signed char, SCHAR_MIN, SCHAR_MAX)
C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(c11_lua_get_checked_uchar, unsigned char, UCHAR_MAX)
C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_short, short, SHRT_MIN, SHRT_MAX)
C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(c11_lua_get_checked_ushort, unsigned short, USHRT_MAX)
C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_int, int, INT_MIN, INT_MAX)
C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(c11_lua_get_checked_uint, unsigned int, UINT_MAX)
C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_long, long, LONG_MIN, LONG_MAX)
C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(c11_lua_get_checked_ulong, unsigned long, ULONG_MAX)
C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(c11_lua_get_checked_llong, long long, LLONG_MIN, LLONG_MAX)
C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(c11_lua_get_checked_ullong, unsigned long long, ULLONG_MAX)

#undef C11_LUA_DEFINE_CHECKED_SIGNED_GETTER
#undef C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER

static inline int c11_lua_get_checked_bool(lua_State* L, int idx, bool* out) {
    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TBOOLEAN) return SALTS_EPROTO;
    *out = lua_toboolean(L, idx) != 0;
    return SALTS_OK;
}

static inline int c11_lua_get_checked_float(lua_State* L, int idx, float* out) {
    lua_Number value;

    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return SALTS_EPROTO;
    value = lua_tonumber(L, idx);
    if (value > (lua_Number)FLT_MAX || value < (lua_Number)-FLT_MAX) return SALTS_ERANGE;
    *out = (float)value;
    return SALTS_OK;
}

static inline int c11_lua_get_checked_double(lua_State* L, int idx, double* out) {
    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return SALTS_EPROTO;
    *out = (double)lua_tonumber(L, idx);
    return SALTS_OK;
}

static inline int c11_lua_get_checked_str(lua_State* L, int idx, const char** out) {
    const char* value;

    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return SALTS_EPROTO;
    value = lua_tostring(L, idx);
    if (!value) return SALTS_EPROTO;
    *out = value;
    return SALTS_OK;
}

static inline int c11_lua_get_checked_tstr(lua_State* L, int idx, tstr* out) {
    size_t len = 0;
    const char* value;
    tstr copy;

    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return SALTS_EPROTO;
    value = lua_tolstring(L, idx, &len);
    if (!value) return SALTS_EPROTO;
    copy = tstr_dup_len(value, len);
    if (!copy) return SALTS_ENOMEM;
    *out = copy;
    return SALTS_OK;
}

static inline int c11_lua_get_checked_vstr(lua_State* L, int idx, vstr* out) {
    size_t len = 0;
    const char* value;

    if (!L || !out) return SALTS_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return SALTS_EPROTO;
    value = lua_tolstring(L, idx, &len);
    if (!value) return SALTS_EPROTO;
    *out = vstr_from_buf(value, len);
    return SALTS_OK;
}

static inline int c11_lua_get_checked_ptr(lua_State* L, int idx, void** out) {
    void* value;

    if (!L || !out) return SALTS_EINVAL;
    if (!lua_isuserdata(L, idx)) return SALTS_EPROTO;
    value = lua_touserdata(L, idx);
    if (!value) return SALTS_EPROTO;
    *out = value;
    return SALTS_OK;
}

#define C11_LUA_CHECKED_FIRST_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) output_type: checked_fn
#define C11_LUA_CHECKED_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) , output_type: checked_fn
#define C11_LUA_CHECKED_POINTER_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn) , output_type: checked_fn

/**
 * @brief Strictly extracts a Lua value selected by the output pointer type.
 * @param L Lua state.
 * @param idx Stack index of the value to read.
 * @param ptr Non-NULL output pointer; its value is unchanged on failure.
 * @return SALTS_OK, SALTS_EINVAL, SALTS_EPROTO, SALTS_ERANGE, or SALTS_ENOMEM.
 *
 * tstr output is an owned binary-safe copy and must be released with
 * tstr_free(). const char* and vstr outputs borrow Lua-owned storage.
 *
 * @code
 * int value;
 * if (c11_lua_get_checked(L, 1, &value) != SALTS_OK) {
 *     return luaL_argerror(L, 1, "integer expected");
 * }
 * @endcode
 */
#define c11_lua_get_checked(L, idx, ptr) _Generic((ptr), \
    Replay(C11_LUA_BOOLEAN_TYPE, C11_LUA_CHECKED_FIRST_ASSOC) \
    Replay(C11_LUA_NUMERIC_TYPES, C11_LUA_CHECKED_ASSOC) \
    Replay(C11_LUA_STRING_TYPES, C11_LUA_CHECKED_ASSOC) \
    Replay(C11_LUA_POINTER_TYPES, C11_LUA_CHECKED_POINTER_ASSOC) \
)(L, idx, ptr)


/* Low-level escape hatch for APIs already expressed as lua_CFunction. */
static inline int c11_lua_bind_raw_function(lua_State* L, const char* name,
                                             lua_CFunction function) {
    if (!L || !name || name[0] == '\0' || !function) return SALTS_EINVAL;
    lua_pushcfunction(L, function);
    lua_setglobal(L, name);
    return SALTS_OK;
}

#define C11_LUA_BIND_RAW(L, lua_name, function) \
    c11_lua_bind_raw_function((L), (lua_name), (function))

/* -------------------------------------------------------------------------
 * Typed C Function Adapters
 *
 * C11 cannot inspect a function pointer's parameter list. C11_LUA_FUNCTION
 * keeps the signature explicit as type/name pairs and dispatches by pair count
 * to generate the lua_CFunction trampoline. Functions with more than four
 * parameters are supported up to arity nine.
 * ------------------------------------------------------------------------- */
static inline int c11_lua_typed_argument_error(lua_State* L, int index, int rc) {
    const char* message = "unsupported argument";
    switch (rc) {
        case SALTS_EPROTO: message = "argument type mismatch"; break;
        case SALTS_ERANGE: message = "argument value out of range"; break;
        case SALTS_ENOMEM: message = "argument allocation failed"; break;
        case SALTS_EINVAL: message = "invalid argument binding"; break;
        default: break;
    }
    return luaL_argerror(L, index, message);
}

#define C11_LUA_TYPED_REQUIRE_ARITY(L, function_name, expected) \
    do { \
        int _c11_actual = lua_gettop((L)); \
        if (_c11_actual != (expected)) \
            return luaL_error((L), "%s expects %d argument(s), got %d", \
                              (function_name), (expected), _c11_actual); \
    } while (0)

#define C11_LUA_TYPED_READ(L, index, type, name) \
    type name; \
    do { \
        int _c11_rc = c11_lua_get_checked((L), (index), &name); \
        if (_c11_rc != SALTS_OK) \
            return c11_lua_typed_argument_error((L), (index), _c11_rc); \
    } while (0)

/** Defines a lua_CFunction adapter for a value-returning C function. */
#define C11_LUA_FUNCTION_BINDER(function) CMETA_PP_CAT(function, _lua_bind)
#define C11_LUA_FUNCTION_TRAMPOLINE(function) CMETA_PP_CAT(function, _lua_trampoline)
#define C11_LUA_BIND(L, function) C11_LUA_FUNCTION_BINDER(function)((L), #function)
#define C11_LUA_BIND_AS(L, lua_name, function) \
    C11_LUA_FUNCTION_BINDER(function)((L), (lua_name))

#define C11_LUA_FUNCTION_EPILOGUE(function) \
    static inline int C11_LUA_FUNCTION_BINDER(function)(lua_State* L, \
                                                         const char* name) { \
        return c11_lua_bind_raw_function( \
            L, name, C11_LUA_FUNCTION_TRAMPOLINE(function)); \
    }

/**
 * Declares a yield-capable lua_CFunction and gives it the same binder naming
 * convention as C11_LUA_FUNCTION_n. The function must use lua_yieldk() when it
 * needs to suspend; state that survives suspension belongs in lua_KContext,
 * Lua userdata, or another explicitly owned object.
 */
#define C11_LUA_COROUTINE(function) \
    static inline int C11_LUA_FUNCTION_BINDER(function)(lua_State* L, \
                                                         const char* name) { \
        return c11_lua_bind_raw_function((L), (name), (function)); \
    }

#define C11_LUA_FUNCTION_IMPL_0(function, return_type) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 0); \
        _c11_result = function(); \
        c11_lua_push(L, _c11_result); \
        return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_1(function, return_type, t1, a1) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 1); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        _c11_result = function(a1); \
        c11_lua_push(L, _c11_result); \
        return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_2(function, return_type, t1, a1, t2, a2) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 2); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        _c11_result = function(a1, a2); \
        c11_lua_push(L, _c11_result); \
        return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_3(function, return_type, t1, a1, t2, a2, t3, a3) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 3); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); \
        _c11_result = function(a1, a2, a3); \
        c11_lua_push(L, _c11_result); \
        return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_4(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 4); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); \
        C11_LUA_TYPED_READ(L, 4, t4, a4); \
        _c11_result = function(a1, a2, a3, a4); \
        c11_lua_push(L, _c11_result); \
        return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_5(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 5); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); \
        _c11_result = function(a1, a2, a3, a4, a5); \
        c11_lua_push(L, _c11_result); return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_6(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 6); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        _c11_result = function(a1, a2, a3, a4, a5, a6); \
        c11_lua_push(L, _c11_result); return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_7(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 7); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); \
        _c11_result = function(a1, a2, a3, a4, a5, a6, a7); \
        c11_lua_push(L, _c11_result); return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_8(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7, t8, a8) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 8); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); C11_LUA_TYPED_READ(L, 8, t8, a8); \
        _c11_result = function(a1, a2, a3, a4, a5, a6, a7, a8); \
        c11_lua_push(L, _c11_result); return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_FUNCTION_IMPL_9(function, return_type, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7, t8, a8, t9, a9) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        return_type _c11_result; \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 9); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); C11_LUA_TYPED_READ(L, 8, t8, a8); \
        C11_LUA_TYPED_READ(L, 9, t9, a9); \
        _c11_result = function(a1, a2, a3, a4, a5, a6, a7, a8, a9); \
        c11_lua_push(L, _c11_result); return 1; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

/* Select by total macro argument count: function + return type + type/name pairs. */
#define C11_LUA_FUNCTION_ARGS_2 C11_LUA_FUNCTION_IMPL_0
#define C11_LUA_FUNCTION_ARGS_4 C11_LUA_FUNCTION_IMPL_1
#define C11_LUA_FUNCTION_ARGS_6 C11_LUA_FUNCTION_IMPL_2
#define C11_LUA_FUNCTION_ARGS_8 C11_LUA_FUNCTION_IMPL_3
#define C11_LUA_FUNCTION_ARGS_10 C11_LUA_FUNCTION_IMPL_4
#define C11_LUA_FUNCTION_ARGS_12 C11_LUA_FUNCTION_IMPL_5
#define C11_LUA_FUNCTION_ARGS_14 C11_LUA_FUNCTION_IMPL_6
#define C11_LUA_FUNCTION_ARGS_16 C11_LUA_FUNCTION_IMPL_7
#define C11_LUA_FUNCTION_ARGS_18 C11_LUA_FUNCTION_IMPL_8
#define C11_LUA_FUNCTION_ARGS_20 C11_LUA_FUNCTION_IMPL_9
#define C11_LUA_FUNCTION(...) \
    CMETA_PP_CAT(C11_LUA_FUNCTION_ARGS_, \
                 C11_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

/** Defines a lua_CFunction adapter for a void C function. */
#define C11_LUA_VOID_FUNCTION_IMPL_0(function) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 0); \
        function(); \
        return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_1(function, t1, a1) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 1); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        function(a1); \
        return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_2(function, t1, a1, t2, a2) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 2); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        function(a1, a2); \
        return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_3(function, t1, a1, t2, a2, t3, a3) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 3); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); \
        function(a1, a2, a3); \
        return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_4(function, t1, a1, t2, a2, t3, a3, t4, a4) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 4); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); \
        C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); \
        C11_LUA_TYPED_READ(L, 4, t4, a4); \
        function(a1, a2, a3, a4); \
        return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_5(function, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 5); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); function(a1, a2, a3, a4, a5); return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_6(function, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 6); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        function(a1, a2, a3, a4, a5, a6); return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_7(function, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 7); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); function(a1, a2, a3, a4, a5, a6, a7); return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_8(function, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7, t8, a8) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 8); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); C11_LUA_TYPED_READ(L, 8, t8, a8); \
        function(a1, a2, a3, a4, a5, a6, a7, a8); return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

#define C11_LUA_VOID_FUNCTION_IMPL_9(function, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5, t6, a6, t7, a7, t8, a8, t9, a9) \
    static int C11_LUA_FUNCTION_TRAMPOLINE(function)(lua_State* L) { \
        C11_LUA_TYPED_REQUIRE_ARITY(L, #function, 9); \
        C11_LUA_TYPED_READ(L, 1, t1, a1); C11_LUA_TYPED_READ(L, 2, t2, a2); \
        C11_LUA_TYPED_READ(L, 3, t3, a3); C11_LUA_TYPED_READ(L, 4, t4, a4); \
        C11_LUA_TYPED_READ(L, 5, t5, a5); C11_LUA_TYPED_READ(L, 6, t6, a6); \
        C11_LUA_TYPED_READ(L, 7, t7, a7); C11_LUA_TYPED_READ(L, 8, t8, a8); \
        C11_LUA_TYPED_READ(L, 9, t9, a9); \
        function(a1, a2, a3, a4, a5, a6, a7, a8, a9); return 0; \
    } \
    C11_LUA_FUNCTION_EPILOGUE(function)

/* Select by total macro argument count: function + type/name pairs. */
#define C11_LUA_VOID_FUNCTION_ARGS_1 C11_LUA_VOID_FUNCTION_IMPL_0
#define C11_LUA_VOID_FUNCTION_ARGS_3 C11_LUA_VOID_FUNCTION_IMPL_1
#define C11_LUA_VOID_FUNCTION_ARGS_5 C11_LUA_VOID_FUNCTION_IMPL_2
#define C11_LUA_VOID_FUNCTION_ARGS_7 C11_LUA_VOID_FUNCTION_IMPL_3
#define C11_LUA_VOID_FUNCTION_ARGS_9 C11_LUA_VOID_FUNCTION_IMPL_4
#define C11_LUA_VOID_FUNCTION_ARGS_11 C11_LUA_VOID_FUNCTION_IMPL_5
#define C11_LUA_VOID_FUNCTION_ARGS_13 C11_LUA_VOID_FUNCTION_IMPL_6
#define C11_LUA_VOID_FUNCTION_ARGS_15 C11_LUA_VOID_FUNCTION_IMPL_7
#define C11_LUA_VOID_FUNCTION_ARGS_17 C11_LUA_VOID_FUNCTION_IMPL_8
#define C11_LUA_VOID_FUNCTION_ARGS_19 C11_LUA_VOID_FUNCTION_IMPL_9
#define C11_LUA_VOID_FUNCTION(...) \
    CMETA_PP_CAT(C11_LUA_VOID_FUNCTION_ARGS_, \
                 C11_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)

/* -------------------------------------------------------------------------
 * Named Environments
 * ------------------------------------------------------------------------- */
/**
 * @brief Creates and stores a named environment table.
 * @param parent_name Optional named parent table used as __index.
 * @param allow_global_fallback Use _G as __index when parent_name is absent.
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ENOENT, or SALTS_EPROTO.
 * @note The Lua stack is restored to its original height on every return.
 */
static inline int c11_lua_create_environment(lua_State* L, const char* name,
                                              const char* parent_name,
                                              bool allow_global_fallback) {
    int base;

    if (!L || !name || name[0] == '\0') return SALTS_EINVAL;
    base = lua_gettop(L);
    lua_newtable(L);
    lua_newtable(L);

    if (parent_name && parent_name[0] != '\0') {
        lua_getglobal(L, parent_name);
        if (lua_isnil(L, -1)) {
            lua_settop(L, base);
            return SALTS_ENOENT;
        }
        if (!lua_istable(L, -1)) {
            lua_settop(L, base);
            return SALTS_EPROTO;
        }
    } else if (allow_global_fallback) {
        lua_getglobal(L, "_G");
    } else {
        lua_newtable(L);
    }

    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
    return SALTS_OK;
}

/** Creates an environment with an empty fallback or the named parent table. */
static inline int c11_lua_create_isolated_environment(lua_State* L, const char* name,
                                                       const char* parent_name) {
    return c11_lua_create_environment(L, name, parent_name, false);
}

/**
 * Load and run code with a named environment as _ENV.
 *
 * @param environment_name Existing global environment table name.
 * @param code NUL-terminated Lua source code.
 * @return A Lua status code. On failure one error object is added to the stack;
 *         on success the script results are left on the stack.
 */
static inline int c11_lua_run_script_in_environment(lua_State* L,
                                                     const char* environment_name,
                                                     const char* code) {
    int base;
    int status;

    if (!L) return LUA_ERRRUN;
    base = lua_gettop(L);
    if (!environment_name || environment_name[0] == '\0' || !code) {
        lua_pushliteral(L, "c11_lua_bind: invalid environment or script");
        return LUA_ERRRUN;
    }

    status = luaL_loadstring(L, code);
    if (status != LUA_OK) return status;

    lua_getglobal(L, environment_name);
    if (!lua_istable(L, -1)) {
        lua_settop(L, base);
        lua_pushfstring(L, "c11_lua_bind: environment '%s' was not found", environment_name);
        return LUA_ERRRUN;
    }
    if (!lua_setupvalue(L, -2, 1)) {
        lua_settop(L, base);
        lua_pushliteral(L, "c11_lua_bind: loaded chunk has no _ENV upvalue");
        return LUA_ERRRUN;
    }
    return lua_pcall(L, 0, LUA_MULTRET, 0);
}

/* -------------------------------------------------------------------------
 * Registry References
 *
 * A reference must be initialized with C11_LUA_REF_INIT or c11_lua_ref_init(),
 * and released before its owner lua_State is closed. References cannot be
 * pushed into a different state.
 * ------------------------------------------------------------------------- */
typedef struct c11_lua_ref {
    lua_State* owner;
    int value;
} c11_lua_ref_t;

#define C11_LUA_REF_INIT { NULL, LUA_NOREF }

static inline void c11_lua_ref_init(c11_lua_ref_t* ref) {
    if (!ref) return;
    ref->owner = NULL;
    ref->value = LUA_NOREF;
}

static inline bool c11_lua_ref_is_valid(const c11_lua_ref_t* ref) {
    return ref && ref->owner && ref->value != LUA_NOREF;
}

/**
 * @brief Retains a stack value in the Lua registry without consuming it.
 * @return SALTS_OK, SALTS_EINVAL, or SALTS_EALREADY.
 */
static inline int c11_lua_ref_create(lua_State* L, int idx, c11_lua_ref_t* out) {
    if (!L || !out || lua_type(L, idx) == LUA_TNONE) return SALTS_EINVAL;
    if (c11_lua_ref_is_valid(out)) return SALTS_EALREADY;

    lua_pushvalue(L, idx);
    out->value = luaL_ref(L, LUA_REGISTRYINDEX);
    out->owner = L;
    return SALTS_OK;
}

/**
 * @brief Pushes a retained value onto its owner state's stack.
 * @return SALTS_OK, SALTS_EINVAL, SALTS_ENOENT, or SALTS_EBADF.
 */
static inline int c11_lua_ref_push(lua_State* L, const c11_lua_ref_t* ref) {
    if (!L || !ref) return SALTS_EINVAL;
    if (!c11_lua_ref_is_valid(ref)) return SALTS_ENOENT;
    if (ref->owner != L) return SALTS_EBADF;

    if (ref->value == LUA_REFNIL) {
        lua_pushnil(L);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref->value);
    }
    return SALTS_OK;
}

/** Releases a retained value and resets the handle to its initialized state. */
static inline int c11_lua_ref_release(c11_lua_ref_t* ref) {
    if (!ref) return SALTS_EINVAL;
    if (!c11_lua_ref_is_valid(ref)) return SALTS_ENOENT;

    luaL_unref(ref->owner, LUA_REGISTRYINDEX, ref->value);
    c11_lua_ref_init(ref);
    return SALTS_OK;
}

/* -------------------------------------------------------------------------
 * Protected Calls
 * ------------------------------------------------------------------------- */
/**
 * @brief Calls a named global function through lua_pcall().
 * @param nargs Number of arguments already at the top of the stack.
 * @param nresults Expected result count or LUA_MULTRET.
 * @return A Lua status code. Failure leaves an error object on the stack.
 * @note A missing/non-callable global consumes the supplied arguments.
 */
static inline int c11_lua_pcall_global(lua_State* L, const char* name,
                                       int nargs, int nresults) {
    int top;

    if (!L) return LUA_ERRRUN;
    top = lua_gettop(L);
    if (!name || name[0] == '\0' || nargs < 0 || nargs > top ||
        (nresults < 0 && nresults != LUA_MULTRET)) {
        lua_pushliteral(L, "c11_lua_bind: invalid protected-call arguments");
        return LUA_ERRRUN;
    }

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        lua_settop(L, top - nargs);
        lua_pushfstring(L, "c11_lua_bind: global '%s' is not callable", name);
        return LUA_ERRRUN;
    }

    lua_insert(L, -(nargs + 1));
    return lua_pcall(L, nargs, nresults, 0);
}

/* -------------------------------------------------------------------------
 * X-Macro Based C Struct Reflection & Lua Table Serialization
 * ------------------------------------------------------------------------- */
#define C11_STRUCT_FIELD_DECL(type, name) type name;

#define C11_STRUCT_FIELD_PUSH(type, name) \
    lua_pushstring(L, #name); \
    c11_lua_push(L, obj->name); \
    lua_settable(L, -3);

#define C11_STRUCT_FIELD_GET(type, name) \
    lua_getfield(L, idx, #name); \
    c11_lua_get(L, -1, &(obj->name)); \
    lua_pop(L, 1);

/* Arena-based allocation for strings. */
#define C11_STRUCT_FIELD_GET_ARENA(type, name) \
    lua_getfield(L, idx, #name); \
    c11_lua_get_arena(L, -1, &(obj->name), arena); \
    lua_pop(L, 1);

/* tstr requires its SDS header and therefore remains heap-owned. */
static inline void c11_lua_get_arena_tstr_t(lua_State* L, int idx, tstr* out, MemoryPool* arena) {
    (void)arena;
    c11_lua_get_tstr(L, idx, out);
}

static inline void c11_lua_get_arena_str(lua_State* L, int idx, const char** out, MemoryPool* arena) {
    if (!out) return;
    const char* str = lua_tostring(L, idx);
    *out = str ? vstr_to_pool(vstr_from_cstr(str), arena) : NULL;
}

/* Scalar values do not allocate from the arena. */
#define C11_LUA_DEFINE_ARENA_SCALAR_GETTER(name, type, getter) \
    static inline void name(lua_State* L, int idx, type* out, MemoryPool* arena) { \
        (void)arena; \
        getter(L, idx, out); \
    }

C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_bool, bool, c11_lua_get_bool)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_char, char, c11_lua_get_char)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_schar, signed char, c11_lua_get_schar)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_uchar, unsigned char, c11_lua_get_uchar)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_short, short, c11_lua_get_short)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_ushort, unsigned short, c11_lua_get_ushort)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_int, int, c11_lua_get_int)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_uint, unsigned int, c11_lua_get_uint)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_long, long, c11_lua_get_long)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_ulong, unsigned long, c11_lua_get_ulong)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_llong, long long, c11_lua_get_llong)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_ullong, unsigned long long, c11_lua_get_ullong)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_float, float, c11_lua_get_float)
C11_LUA_DEFINE_ARENA_SCALAR_GETTER(c11_lua_get_arena_double, double, c11_lua_get_double)

#undef C11_LUA_DEFINE_ARENA_SCALAR_GETTER

static inline void c11_lua_get_arena_vstr(lua_State* L, int idx, vstr* out, MemoryPool* arena) {
    size_t len = 0;
    const char* str;
    char* copy;

    if (!out) return;
    str = lua_tolstring(L, idx, &len);
    if (!str) {
        *out = vstr_from_buf("", 0);
        return;
    }
    copy = vstr_to_pool(vstr_from_buf(str, len), arena);
    *out = vstr_from_buf(copy, copy ? len : 0);
}

#define C11_LUA_ARENA_FIRST_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) output_type: arena_fn
#define C11_LUA_ARENA_ASSOC(value_type, output_type, push_fn, get_fn, checked_fn, arena_fn) , output_type: arena_fn

#define c11_lua_get_arena(L, idx, ptr, arena) _Generic((ptr), \
    Replay(C11_LUA_BOOLEAN_TYPE, C11_LUA_ARENA_FIRST_ASSOC) \
    Replay(C11_LUA_NUMERIC_TYPES, C11_LUA_ARENA_ASSOC) \
    Replay(C11_LUA_STRING_TYPES, C11_LUA_ARENA_ASSOC) \
)(L, idx, ptr, arena)

/**
 * @brief Defines a C struct and auto-generates serialization functions:
 *        - StructName_to_lua(L, obj)         : Push to Lua table
 *        - StructName_from_lua(L, idx, obj)  : Extract from Lua table
 *        - StructName_from_lua_arena(L, idx, obj, arena) : Copy borrowed strings into an arena
 *        - StructName_cleanup(obj)           : Free owned tstr fields
 *
 * @param StructName Name of the struct to define.
 * @param FIELDS An X-Macro list of fields formatted as: X(type, name) ...
 *
 * OWNERSHIP NOTES:
 * - Fields of type tstr always require cleanup via StructName_cleanup().
 * - from_lua() returns borrowed vstr and const char* fields that must not
 *   outlive the Lua value.
 * - from_lua_arena() copies vstr and const char* fields into arena; those
 *   fields remain valid until pool_reset() or pool_destroy().
 *
 * EXAMPLE:
 * @code
 *   #define PLAYER_FIELDS(X) \
 *       X(int, id) \
 *       X(vstr, name) \
 *       X(double, hp)
 *
 *   C11_LUA_DEFINE_STRUCT(Player, PLAYER_FIELDS)
 *
 *   // Arena-backed view remains valid after the Lua value is popped.
 *   MemoryPool *arena = pool_create(4096);
 *   Player player;
 *   Player_from_lua_arena(L, -1, &player, arena);
 *   printf("Name: %.*s\n", (int)player.name.len, player.name.data);
 *   pool_destroy(arena);
 * @endcode
 */
#define C11_LUA_DEFINE_STRUCT(StructName, FIELDS) \
    typedef struct StructName StructName; \
    struct StructName { \
        FIELDS(C11_STRUCT_FIELD_DECL) \
    }; \
    \
    static inline void StructName##_to_lua(lua_State* L, const StructName* obj) { \
        if (!L || !obj) return; \
        lua_newtable(L); \
        FIELDS(C11_STRUCT_FIELD_PUSH) \
    } \
    \
    static inline void StructName##_from_lua(lua_State* L, int idx, StructName* obj) { \
        if (!L || !obj) return; \
        int abs_idx = (idx < 0 && idx > LUA_REGISTRYINDEX) ? (lua_gettop(L) + idx + 1) : idx; \
        (void)abs_idx; /* Use idx directly in FIELDS macros to allow pseudo-indices */ \
        FIELDS(C11_STRUCT_FIELD_GET) \
    } \
    \
    static inline void StructName##_from_lua_arena(lua_State* L, int idx, StructName* obj, MemoryPool* arena) { \
        if (!L || !obj || !arena) return; \
        int abs_idx = (idx < 0 && idx > LUA_REGISTRYINDEX) ? (lua_gettop(L) + idx + 1) : idx; \
        (void)abs_idx; \
        FIELDS(C11_STRUCT_FIELD_GET_ARENA) \
    }

/**
 * @brief Helper macro to define cleanup function for structs with tstr fields.
 *        Call this after C11_LUA_DEFINE_STRUCT if your struct contains tstr.
 *
 * @param StructName Name of the struct
 * @param TSTR_FIELDS Comma-separated list of tstr field names
 *
 * EXAMPLE:
 * @code
 *   C11_LUA_DEFINE_STRUCT_CLEANUP(Player, name, title, description)
 * @endcode
 */
#define C11_LUA_DEFINE_STRUCT_CLEANUP_FIELD(field, obj_ptr) \
    tstr_free((obj_ptr)->field); \
    (obj_ptr)->field = NULL;

#define C11_LUA_DEFINE_STRUCT_CLEANUP(StructName, ...) \
    static inline void StructName##_cleanup(StructName* obj) { \
        if (!obj) return; \
        CMETA_PP_FOR_EACH(C11_LUA_DEFINE_STRUCT_CLEANUP_FIELD, obj, __VA_ARGS__) \
    }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_LUA_H */
