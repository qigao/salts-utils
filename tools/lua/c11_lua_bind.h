/**
 * @file c11_lua_bind.h
 * @brief Lightweight C11 Preprocessor and _Generic based Lua binding library.
 *
 * Provides type-generic value pushing/getting, strict checked extraction,
 * function registration, named execution environments, registry references,
 * protected global calls, and X-Macro based struct serialization.
 *
 * STRING LIFETIME & OWNERSHIP MODEL:
 *
 * - const char*: BORROWED - Valid only while Lua value remains on stack.
 *                ⚠️ UNSAFE after lua_pop/lua_settop/stack modification!
 *                Use only for immediate consumption within same Lua call.
 *
 * - tstr_t:      OWNED - Independent lifetime, caller must tstr_free().
 *                ✅ SAFE to use after stack changes, across function boundaries.
 *                Recommended for struct fields that outlive Lua call.
 *
 * - tstr_v:      BORROWED VIEW - Non-owning reference to Lua string.
 *                ⚠️ Same lifetime constraints as const char*.
 *                Use for read-only, short-lived access with O(1) length.
 *
 * MEMORY MANAGEMENT:
 *
 * - Structs with tstr_t fields always require manual cleanup.
 * - Use StructName_cleanup() to free owned tstr_t fields.
 * - StructName_from_lua_arena() copies const char* and tstr_v fields into a
 *   MemoryPool; those fields become invalid after pool_reset()/pool_destroy().
 */
#ifndef C11_LUA_BIND_H
#define C11_LUA_BIND_H

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <float.h>
#include <limits.h>

#include "turbo_error.h"
#include "turbo_str.h"
#include "turbo_str_view.h"
#include "memory_pool.h"


#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Preprocessor Metaprogramming Helpers
 * ------------------------------------------------------------------------- */
#define C11_EXPAND(x) x
#define C11_CONCAT(a, b) C11_CONCAT_INNER(a, b)
#define C11_CONCAT_INNER(a, b) a##b
#define C11_STRINGIFY(x) #x

/* Count variadic macro arguments (up to 16) */
#define C11_COUNT_ARGS(...) C11_EXPAND(C11_COUNT_ARGS_HELPER(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0))
#define C11_COUNT_ARGS_HELPER(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

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
 * @brief Push tstr_t to Lua (borrows data, does NOT transfer ownership)
 * @note The tstr_t remains owned by caller and must be freed separately
 */
static inline void c11_lua_push_tstr(lua_State* L, tstr_t s) {
    lua_pushstring(L, s ? s : "");
}

/**
 * @brief Push tstr_v to Lua (uses pushlstring for binary safety)
 */
static inline void c11_lua_push_tstr_v(lua_State* L, tstr_v v) {
    lua_pushlstring(L, v.data ? v.data : "", v.len);
}

/**
 * @brief Pushes a typed C value onto the Lua stack using C11 _Generic selection.
 */
#define c11_lua_push(L, val) _Generic((val), \
    tstr_t:              c11_lua_push_tstr, \
    tstr_v:              c11_lua_push_tstr_v, \
    _Bool:               c11_lua_push_bool, \
    char:                c11_lua_push_int, \
    signed char:         c11_lua_push_int, \
    unsigned char:       c11_lua_push_int, \
    short:               c11_lua_push_int, \
    unsigned short:      c11_lua_push_int, \
    int:                 c11_lua_push_int, \
    unsigned int:        c11_lua_push_int, \
    long:                c11_lua_push_int, \
    unsigned long:       c11_lua_push_int, \
    long long:           c11_lua_push_int, \
    unsigned long long:  c11_lua_push_int, \
    float:               c11_lua_push_num, \
    double:              c11_lua_push_num, \
    const char*:         c11_lua_push_str, \
    void*:               c11_lua_push_ptr  \
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
 * @brief Get OWNED tstr_t from Lua (creates independent copy)
 * @note Caller must tstr_free() the result when done
 * @return NULL if Lua value is nil/not a string
 */
static inline void c11_lua_get_tstr(lua_State* L, int idx, tstr_t* out) {
    size_t len = 0;
    const char* str;

    if (!out) return;
    str = lua_tolstring(L, idx, &len);
    *out = str ? tstr_dup_len(str, len) : NULL;
}

/**
 * @brief Get BORROWED tstr_v from Lua (non-owning view)
 * @warning View valid ONLY while Lua value remains on stack.
 *          Same lifetime constraints as const char*.
 */
static inline void c11_lua_get_tstr_v(lua_State* L, int idx, tstr_v* out) {
    if (!out) return;
    size_t len;
    const char* str = lua_tolstring(L, idx, &len);
    *out = str ? tstr_v_from_buf(str, len) : tstr_v_from_buf("", 0);
}

static inline void c11_lua_get_ptr(lua_State* L, int idx, void** out) {
    if (out) *out = lua_touserdata(L, idx);
}

/**
 * @brief Extracts a value from the Lua stack into a typed pointer using C11 _Generic selection.
 */
#define c11_lua_get(L, idx, ptr) _Generic((ptr), \
    tstr_t*:             c11_lua_get_tstr, \
    tstr_v*:             c11_lua_get_tstr_v, \
    _Bool*:              c11_lua_get_bool, \
    char*:               c11_lua_get_char, \
    signed char*:        c11_lua_get_schar, \
    unsigned char*:      c11_lua_get_uchar, \
    short*:              c11_lua_get_short, \
    unsigned short*:     c11_lua_get_ushort, \
    int*:                c11_lua_get_int, \
    unsigned int*:       c11_lua_get_uint, \
    long*:               c11_lua_get_long, \
    unsigned long*:      c11_lua_get_ulong, \
    long long*:          c11_lua_get_llong, \
    unsigned long long*: c11_lua_get_ullong, \
    float*:              c11_lua_get_float, \
    double*:             c11_lua_get_double, \
    const char**:        c11_lua_get_str, \
    void**:              c11_lua_get_ptr \
)(L, idx, ptr)

/* -------------------------------------------------------------------------
 * Strict Value Extraction
 *
 * These helpers reject implicit Lua conversions and leave output unchanged on
 * failure. They return TurboUtils error codes: TURBO_OK, TURBO_EINVAL,
 * TURBO_EPROTO, TURBO_ERANGE, or TURBO_ENOMEM.
 * ------------------------------------------------------------------------- */
static inline int c11_lua_checked_integer(lua_State* L, int idx, lua_Integer* out) {
    lua_Integer value;

    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return TURBO_EPROTO;
#if LUA_VERSION_NUM >= 503
    if (!lua_isinteger(L, idx)) return TURBO_EPROTO;
#else
    {
        lua_Number number = lua_tonumber(L, idx);
        value = lua_tointeger(L, idx);
        if ((lua_Number)value != number) return TURBO_EPROTO;
    }
#endif
    value = lua_tointeger(L, idx);
    *out = value;
    return TURBO_OK;
}

#define C11_LUA_DEFINE_CHECKED_SIGNED_GETTER(name, type, min_value, max_value) \
    static inline int name(lua_State* L, int idx, type* out) { \
        lua_Integer value; \
        int rc; \
        if (!out) return TURBO_EINVAL; \
        rc = c11_lua_checked_integer(L, idx, &value); \
        if (rc != TURBO_OK) return rc; \
        if (value < (lua_Integer)(min_value) || value > (lua_Integer)(max_value)) \
            return TURBO_ERANGE; \
        *out = (type)value; \
        return TURBO_OK; \
    }

#define C11_LUA_DEFINE_CHECKED_UNSIGNED_GETTER(name, type, max_value) \
    static inline int name(lua_State* L, int idx, type* out) { \
        lua_Integer value; \
        int rc; \
        if (!out) return TURBO_EINVAL; \
        rc = c11_lua_checked_integer(L, idx, &value); \
        if (rc != TURBO_OK) return rc; \
        if (value < 0 || (unsigned long long)value > (unsigned long long)(max_value)) \
            return TURBO_ERANGE; \
        *out = (type)value; \
        return TURBO_OK; \
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
    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TBOOLEAN) return TURBO_EPROTO;
    *out = lua_toboolean(L, idx) != 0;
    return TURBO_OK;
}

static inline int c11_lua_get_checked_float(lua_State* L, int idx, float* out) {
    lua_Number value;

    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return TURBO_EPROTO;
    value = lua_tonumber(L, idx);
    if (value > (lua_Number)FLT_MAX || value < (lua_Number)-FLT_MAX) return TURBO_ERANGE;
    *out = (float)value;
    return TURBO_OK;
}

static inline int c11_lua_get_checked_double(lua_State* L, int idx, double* out) {
    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TNUMBER) return TURBO_EPROTO;
    *out = (double)lua_tonumber(L, idx);
    return TURBO_OK;
}

static inline int c11_lua_get_checked_str(lua_State* L, int idx, const char** out) {
    const char* value;

    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return TURBO_EPROTO;
    value = lua_tostring(L, idx);
    if (!value) return TURBO_EPROTO;
    *out = value;
    return TURBO_OK;
}

static inline int c11_lua_get_checked_tstr(lua_State* L, int idx, tstr_t* out) {
    size_t len = 0;
    const char* value;
    tstr_t copy;

    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return TURBO_EPROTO;
    value = lua_tolstring(L, idx, &len);
    if (!value) return TURBO_EPROTO;
    copy = tstr_dup_len(value, len);
    if (!copy) return TURBO_ENOMEM;
    *out = copy;
    return TURBO_OK;
}

static inline int c11_lua_get_checked_tstr_v(lua_State* L, int idx, tstr_v* out) {
    size_t len = 0;
    const char* value;

    if (!L || !out) return TURBO_EINVAL;
    if (lua_type(L, idx) != LUA_TSTRING) return TURBO_EPROTO;
    value = lua_tolstring(L, idx, &len);
    if (!value) return TURBO_EPROTO;
    *out = tstr_v_from_buf(value, len);
    return TURBO_OK;
}

static inline int c11_lua_get_checked_ptr(lua_State* L, int idx, void** out) {
    void* value;

    if (!L || !out) return TURBO_EINVAL;
    if (!lua_isuserdata(L, idx)) return TURBO_EPROTO;
    value = lua_touserdata(L, idx);
    if (!value) return TURBO_EPROTO;
    *out = value;
    return TURBO_OK;
}

/**
 * @brief Strictly extracts a Lua value selected by the output pointer type.
 * @param L Lua state.
 * @param idx Stack index of the value to read.
 * @param ptr Non-NULL output pointer; its value is unchanged on failure.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_EPROTO, TURBO_ERANGE, or TURBO_ENOMEM.
 *
 * tstr_t output is an owned binary-safe copy and must be released with
 * tstr_free(). const char* and tstr_v outputs borrow Lua-owned storage.
 *
 * @code
 * int value;
 * if (c11_lua_get_checked(L, 1, &value) != TURBO_OK) {
 *     return luaL_argerror(L, 1, "integer expected");
 * }
 * @endcode
 */
#define c11_lua_get_checked(L, idx, ptr) _Generic((ptr), \
    tstr_t*:             c11_lua_get_checked_tstr, \
    tstr_v*:             c11_lua_get_checked_tstr_v, \
    _Bool*:              c11_lua_get_checked_bool, \
    char*:               c11_lua_get_checked_char, \
    signed char*:        c11_lua_get_checked_schar, \
    unsigned char*:      c11_lua_get_checked_uchar, \
    short*:              c11_lua_get_checked_short, \
    unsigned short*:     c11_lua_get_checked_ushort, \
    int*:                c11_lua_get_checked_int, \
    unsigned int*:       c11_lua_get_checked_uint, \
    long*:               c11_lua_get_checked_long, \
    unsigned long*:      c11_lua_get_checked_ulong, \
    long long*:          c11_lua_get_checked_llong, \
    unsigned long long*: c11_lua_get_checked_ullong, \
    float*:              c11_lua_get_checked_float, \
    double*:             c11_lua_get_checked_double, \
    const char**:        c11_lua_get_checked_str, \
    void**:              c11_lua_get_checked_ptr \
)(L, idx, ptr)


/* -------------------------------------------------------------------------
 * Zero-Boilerplate Function Registration
 * ------------------------------------------------------------------------- */
#define C11_LUA_REG_PAIR(func) { #func, func }

#define C11_LUA_REG_1(f1) C11_LUA_REG_PAIR(f1)
#define C11_LUA_REG_2(f1, f2) C11_LUA_REG_PAIR(f1), C11_LUA_REG_PAIR(f2)
#define C11_LUA_REG_3(f1, f2, f3) C11_LUA_REG_2(f1, f2), C11_LUA_REG_PAIR(f3)
#define C11_LUA_REG_4(f1, f2, f3, f4) C11_LUA_REG_3(f1, f2, f3), C11_LUA_REG_PAIR(f4)
#define C11_LUA_REG_5(f1, f2, f3, f4, f5) C11_LUA_REG_4(f1, f2, f3, f4), C11_LUA_REG_PAIR(f5)
#define C11_LUA_REG_6(f1, f2, f3, f4, f5, f6) C11_LUA_REG_5(f1, f2, f3, f4, f5), C11_LUA_REG_PAIR(f6)
#define C11_LUA_REG_7(f1, f2, f3, f4, f5, f6, f7) C11_LUA_REG_6(f1, f2, f3, f4, f5, f6), C11_LUA_REG_PAIR(f7)
#define C11_LUA_REG_8(f1, f2, f3, f4, f5, f6, f7, f8) C11_LUA_REG_7(f1, f2, f3, f4, f5, f6, f7), C11_LUA_REG_PAIR(f8)
#define C11_LUA_REG_9(f1, f2, f3, f4, f5, f6, f7, f8, f9) C11_LUA_REG_8(f1, f2, f3, f4, f5, f6, f7, f8), C11_LUA_REG_PAIR(f9)
#define C11_LUA_REG_10(f1, f2, f3, f4, f5, f6, f7, f8, f9, f10) C11_LUA_REG_9(f1, f2, f3, f4, f5, f6, f7, f8, f9), C11_LUA_REG_PAIR(f10)

/**
 * @brief Batch registers C functions (lua_CFunction) into a new Lua table and leaves it on top of the stack.
 */
#define C11_LUA_BIND_FUNCS(L, ...) \
    do { \
        const luaL_Reg _c11_lua_funcs[] = { \
            C11_EXPAND(C11_CONCAT(C11_LUA_REG_, C11_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)), \
            { NULL, NULL } \
        }; \
        lua_newtable(L); \
        luaL_setfuncs(L, _c11_lua_funcs, 0); \
    } while(0)

/**
 * @brief Binds one lua_CFunction under an explicit global name.
 * @return TURBO_OK, or TURBO_EINVAL when an argument is invalid.
 */
static inline int c11_lua_bind_function_named(lua_State* L, const char* name,
                                               lua_CFunction function) {
    if (!L || !name || name[0] == '\0' || !function) return TURBO_EINVAL;
    lua_pushcfunction(L, function);
    lua_setglobal(L, name);
    return TURBO_OK;
}

/**
 * @brief Binds a NULL-terminated luaL_Reg array into the global table.
 * @return TURBO_OK, or TURBO_EINVAL; validation occurs before any binding.
 */
static inline int c11_lua_bind_functions(lua_State* L, const luaL_Reg* functions) {
    const luaL_Reg* entry;

    if (!L || !functions) return TURBO_EINVAL;
    for (entry = functions; entry->name; ++entry) {
        if (entry->name[0] == '\0' || !entry->func) return TURBO_EINVAL;
    }
    for (entry = functions; entry->name; ++entry) {
        lua_pushcfunction(L, entry->func);
        lua_setglobal(L, entry->name);
    }
    return TURBO_OK;
}

#define C11_LUA_BIND_FUNCTION(L, function) \
    c11_lua_bind_function_named((L), #function, (function))

#define C11_LUA_BIND_FUNCTION_AS(L, lua_name, function) \
    c11_lua_bind_function_named((L), (lua_name), (function))

#define C11_LUA_BIND_GLOBAL_FUNCS(L, ...) \
    do { \
        const luaL_Reg _c11_lua_global_funcs[] = { \
            C11_EXPAND(C11_CONCAT(C11_LUA_REG_, C11_COUNT_ARGS(__VA_ARGS__))(__VA_ARGS__)), \
            { NULL, NULL } \
        }; \
        (void)c11_lua_bind_functions((L), _c11_lua_global_funcs); \
    } while(0)

/* -------------------------------------------------------------------------
 * Named Environments
 * ------------------------------------------------------------------------- */
/**
 * @brief Creates and stores a named environment table.
 * @param parent_name Optional named parent table used as __index.
 * @param allow_global_fallback Use _G as __index when parent_name is absent.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOENT, or TURBO_EPROTO.
 * @note The Lua stack is restored to its original height on every return.
 */
static inline int c11_lua_create_environment(lua_State* L, const char* name,
                                              const char* parent_name,
                                              bool allow_global_fallback) {
    int base;

    if (!L || !name || name[0] == '\0') return TURBO_EINVAL;
    base = lua_gettop(L);
    lua_newtable(L);
    lua_newtable(L);

    if (parent_name && parent_name[0] != '\0') {
        lua_getglobal(L, parent_name);
        if (lua_isnil(L, -1)) {
            lua_settop(L, base);
            return TURBO_ENOENT;
        }
        if (!lua_istable(L, -1)) {
            lua_settop(L, base);
            return TURBO_EPROTO;
        }
    } else if (allow_global_fallback) {
        lua_getglobal(L, "_G");
    } else {
        lua_newtable(L);
    }

    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
    return TURBO_OK;
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
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EALREADY.
 */
static inline int c11_lua_ref_create(lua_State* L, int idx, c11_lua_ref_t* out) {
    if (!L || !out || lua_type(L, idx) == LUA_TNONE) return TURBO_EINVAL;
    if (c11_lua_ref_is_valid(out)) return TURBO_EALREADY;

    lua_pushvalue(L, idx);
    out->value = luaL_ref(L, LUA_REGISTRYINDEX);
    out->owner = L;
    return TURBO_OK;
}

/**
 * @brief Pushes a retained value onto its owner state's stack.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOENT, or TURBO_EBADF.
 */
static inline int c11_lua_ref_push(lua_State* L, const c11_lua_ref_t* ref) {
    if (!L || !ref) return TURBO_EINVAL;
    if (!c11_lua_ref_is_valid(ref)) return TURBO_ENOENT;
    if (ref->owner != L) return TURBO_EBADF;

    if (ref->value == LUA_REFNIL) {
        lua_pushnil(L);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref->value);
    }
    return TURBO_OK;
}

/** Releases a retained value and resets the handle to its initialized state. */
static inline int c11_lua_ref_release(c11_lua_ref_t* ref) {
    if (!ref) return TURBO_EINVAL;
    if (!c11_lua_ref_is_valid(ref)) return TURBO_ENOENT;

    luaL_unref(ref->owner, LUA_REGISTRYINDEX, ref->value);
    c11_lua_ref_init(ref);
    return TURBO_OK;
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

/* tstr_t requires its SDS header and therefore remains heap-owned. */
static inline void c11_lua_get_arena_tstr_t(lua_State* L, int idx, tstr_t* out, MemoryPool* arena) {
    (void)arena;
    c11_lua_get_tstr(L, idx, out);
}

static inline void c11_lua_get_arena_str(lua_State* L, int idx, const char** out, MemoryPool* arena) {
    if (!out) return;
    const char* str = lua_tostring(L, idx);
    *out = str ? tstr_v_to_pool(tstr_v_from_cstr(str), arena) : NULL;
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

static inline void c11_lua_get_arena_tstr_v(lua_State* L, int idx, tstr_v* out, MemoryPool* arena) {
    size_t len = 0;
    const char* str;
    char* copy;

    if (!out) return;
    str = lua_tolstring(L, idx, &len);
    if (!str) {
        *out = tstr_v_from_buf("", 0);
        return;
    }
    copy = tstr_v_to_pool(tstr_v_from_buf(str, len), arena);
    *out = tstr_v_from_buf(copy, copy ? len : 0);
}

#define c11_lua_get_arena(L, idx, ptr, arena) _Generic((ptr), \
    tstr_t*:             c11_lua_get_arena_tstr_t, \
    tstr_v*:             c11_lua_get_arena_tstr_v, \
    _Bool*:              c11_lua_get_arena_bool, \
    char*:               c11_lua_get_arena_char, \
    signed char*:        c11_lua_get_arena_schar, \
    unsigned char*:      c11_lua_get_arena_uchar, \
    short*:              c11_lua_get_arena_short, \
    unsigned short*:     c11_lua_get_arena_ushort, \
    int*:                c11_lua_get_arena_int, \
    unsigned int*:       c11_lua_get_arena_uint, \
    long*:               c11_lua_get_arena_long, \
    unsigned long*:      c11_lua_get_arena_ulong, \
    long long*:          c11_lua_get_arena_llong, \
    unsigned long long*: c11_lua_get_arena_ullong, \
    float*:              c11_lua_get_arena_float, \
    double*:             c11_lua_get_arena_double, \
    const char**:        c11_lua_get_arena_str \
)(L, idx, ptr, arena)

/**
 * @brief Defines a C struct and auto-generates serialization functions:
 *        - StructName_to_lua(L, obj)         : Push to Lua table
 *        - StructName_from_lua(L, idx, obj)  : Extract from Lua table
 *        - StructName_from_lua_arena(L, idx, obj, arena) : Copy borrowed strings into an arena
 *        - StructName_cleanup(obj)           : Free owned tstr_t fields
 *
 * @param StructName Name of the struct to define.
 * @param FIELDS An X-Macro list of fields formatted as: X(type, name) ...
 *
 * OWNERSHIP NOTES:
 * - Fields of type tstr_t always require cleanup via StructName_cleanup().
 * - from_lua() returns borrowed tstr_v and const char* fields that must not
 *   outlive the Lua value.
 * - from_lua_arena() copies tstr_v and const char* fields into arena; those
 *   fields remain valid until pool_reset() or pool_destroy().
 *
 * EXAMPLE:
 * @code
 *   #define PLAYER_FIELDS(X) \
 *       X(int, id) \
 *       X(tstr_v, name) \
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
 * @brief Helper macro to define cleanup function for structs with tstr_t fields.
 *        Call this after C11_LUA_DEFINE_STRUCT if your struct contains tstr_t.
 *
 * @param StructName Name of the struct
 * @param TSTR_FIELDS Comma-separated list of tstr_t field names
 *
 * EXAMPLE:
 * @code
 *   C11_LUA_DEFINE_STRUCT_CLEANUP(Player, name, title, description)
 * @endcode
 */
#define C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj_ptr, field) \
    tstr_free((obj_ptr)->field); \
    (obj_ptr)->field = NULL;

#define C11_LUA_DEFINE_STRUCT_CLEANUP_1(S, f1) \
    static inline void S##_cleanup(S* obj) { \
        if (!obj) return; \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f1) \
    }

#define C11_LUA_DEFINE_STRUCT_CLEANUP_2(S, f1, f2) \
    static inline void S##_cleanup(S* obj) { \
        if (!obj) return; \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f1) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f2) \
    }

#define C11_LUA_DEFINE_STRUCT_CLEANUP_3(S, f1, f2, f3) \
    static inline void S##_cleanup(S* obj) { \
        if (!obj) return; \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f1) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f2) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f3) \
    }

#define C11_LUA_DEFINE_STRUCT_CLEANUP_4(S, f1, f2, f3, f4) \
    static inline void S##_cleanup(S* obj) { \
        if (!obj) return; \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f1) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f2) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f3) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f4) \
    }

#define C11_LUA_DEFINE_STRUCT_CLEANUP_5(S, f1, f2, f3, f4, f5) \
    static inline void S##_cleanup(S* obj) { \
        if (!obj) return; \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f1) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f2) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f3) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f4) \
        C11_LUA_DEFINE_STRUCT_CLEANUP_HELPER(obj, f5) \
    }

/* Dispatcher based on argument count */
#define C11_LUA_DEFINE_STRUCT_CLEANUP(StructName, ...) \
    C11_EXPAND(C11_CONCAT(C11_LUA_DEFINE_STRUCT_CLEANUP_, C11_COUNT_ARGS(__VA_ARGS__))(StructName, __VA_ARGS__))

#ifdef __cplusplus
}
#endif

#endif /* C11_LUA_BIND_H */
