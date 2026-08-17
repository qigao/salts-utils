/**
 * @file c11_lua_data_bind.h
 * @brief Schema-validated DataBind value adapters for Lua.
 *
 * DataBind remains the owner and schema source of truth. This adapter copies a
 * borrowed immutable DataBindValue into a Lua value; it never retains pointers
 * into the DataBind tree. Lua allocation failures retain Lua's longjmp/error
 * behavior.
 */
#ifndef C11_LUA_DATA_BIND_H
#define C11_LUA_DATA_BIND_H

#include "c11_lua_bind.h"
#include "data_bind.h"

#include <limits.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define C11_LUA_DATA_BIND_DEFAULT_MAX_DEPTH 64u
#define C11_LUA_DATA_BIND_TEXT_CAPACITY 128u

static inline DataBindStatus c11_lua_data_bind_push_value_impl(
    lua_State *L, const DataBindValue *value, size_t depth, size_t max_depth);

static inline DataBindStatus c11_lua_data_bind_push_int64(lua_State *L, int64_t value) {
#if defined(LUA_MININTEGER) && defined(LUA_MAXINTEGER)
  if (value < (int64_t)LUA_MININTEGER || value > (int64_t)LUA_MAXINTEGER)
    return DATA_BIND_ERR_LIMIT;
#else
  lua_Integer converted = (lua_Integer)value;
  if ((int64_t)converted != value) return DATA_BIND_ERR_LIMIT;
#endif
  lua_pushinteger(L, (lua_Integer)value);
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_uint64(lua_State *L, uint64_t value) {
#if defined(LUA_MAXINTEGER)
  if (value > (uint64_t)LUA_MAXINTEGER) return DATA_BIND_ERR_LIMIT;
#else
  lua_Integer converted = (lua_Integer)value;
  if (converted < 0 || (uint64_t)converted != value) return DATA_BIND_ERR_LIMIT;
#endif
  lua_pushinteger(L, (lua_Integer)value);
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_object(
    lua_State *L, const DataBindValue *value, size_t depth, size_t max_depth) {
  size_t count = data_bind_value_field_count(value);
  size_t index;

  if (count > (size_t)INT_MAX) return DATA_BIND_ERR_LIMIT;
  lua_createtable(L, 0, (int)count);
  for (index = 0; index < count; ++index) {
    const char *name = data_bind_value_field_name(value, index);
    const DataBindValue *field = data_bind_value_field_at(value, index);
    DataBindStatus status;

    if (!name || !field) return DATA_BIND_ERR_RUNTIME;
    status = c11_lua_data_bind_push_value_impl(L, field, depth + 1u, max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_setfield(L, -2, name);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_sequence(
    lua_State *L, const DataBindValue *value, size_t depth, size_t max_depth) {
  size_t count = data_bind_value_count(value);
  size_t index;

  if (count > (size_t)INT_MAX) return DATA_BIND_ERR_LIMIT;
  lua_createtable(L, (int)count, 0);
  for (index = 0; index < count; ++index) {
    const DataBindValue *item = data_bind_value_at(value, index);
    DataBindStatus status;

    if (!item) return DATA_BIND_ERR_RUNTIME;
    status = c11_lua_data_bind_push_value_impl(L, item, depth + 1u, max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_rawseti(L, -2, (lua_Integer)index + 1);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_map(
    lua_State *L, const DataBindValue *value, size_t depth, size_t max_depth) {
  size_t count = data_bind_value_count(value);
  size_t index;

  if (count > (size_t)INT_MAX) return DATA_BIND_ERR_LIMIT;
  lua_createtable(L, 0, (int)count);
  for (index = 0; index < count; ++index) {
    DataBindMapEntry entry = data_bind_value_map_entry_at(value, index);
    DataBindStatus status;

    if (!entry.key || !entry.value) return DATA_BIND_ERR_RUNTIME;
    status = c11_lua_data_bind_push_value_impl(L, entry.value, depth + 1u, max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_setfield(L, -2, entry.key);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_text_scalar(
    lua_State *L, const DataBindValue *value, DataBindValueKind kind) {
  char text[C11_LUA_DATA_BIND_TEXT_CAPACITY];
  const char *result = NULL;

  switch (kind) {
    case DATA_BIND_VALUE_UUID:
      result = data_bind_value_as_uuid_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_DATETIME:
      result = data_bind_value_as_datetime_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_DATE:
      result = data_bind_value_as_date_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_TIME:
      result = data_bind_value_as_time_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_DURATION:
      result = data_bind_value_as_duration_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_DECIMAL:
      result = data_bind_value_as_decimal_string(value, text, sizeof(text));
      break;
    case DATA_BIND_VALUE_MONEY:
      result = data_bind_value_as_money_string(value, text, sizeof(text));
      break;
    default:
      return DATA_BIND_ERR_TYPE_MISMATCH;
  }
  if (!result) return DATA_BIND_ERR_TYPE_MISMATCH;
  lua_pushstring(L, result);
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_data_bind_push_value_impl(
    lua_State *L, const DataBindValue *value, size_t depth, size_t max_depth) {
  int base;
  DataBindStatus status = DATA_BIND_OK;

  if (!L || !value || max_depth == 0u) return DATA_BIND_ERR_INVALID_ARG;
  if (depth >= max_depth || !lua_checkstack(L, 3)) return DATA_BIND_ERR_LIMIT;
  base = lua_gettop(L);

  switch (data_bind_value_kind(value)) {
    case DATA_BIND_VALUE_NULL:
      lua_pushnil(L);
      break;
    case DATA_BIND_VALUE_OBJECT:
      status = c11_lua_data_bind_push_object(L, value, depth, max_depth);
      break;
    case DATA_BIND_VALUE_LIST:
    case DATA_BIND_VALUE_SET:
      status = c11_lua_data_bind_push_sequence(L, value, depth, max_depth);
      break;
    case DATA_BIND_VALUE_MAP:
      status = c11_lua_data_bind_push_map(L, value, depth, max_depth);
      break;
    case DATA_BIND_VALUE_INT: {
      int32_t number;
      status = data_bind_value_get_int32(value, &number);
      if (status == DATA_BIND_OK) status = c11_lua_data_bind_push_int64(L, number);
      break;
    }
    case DATA_BIND_VALUE_INT64: {
      int64_t number;
      status = data_bind_value_get_int64(value, &number);
      if (status == DATA_BIND_OK) status = c11_lua_data_bind_push_int64(L, number);
      break;
    }
    case DATA_BIND_VALUE_UINT64: {
      uint64_t number;
      status = data_bind_value_get_uint64(value, &number);
      if (status == DATA_BIND_OK) status = c11_lua_data_bind_push_uint64(L, number);
      break;
    }
    case DATA_BIND_VALUE_DOUBLE: {
      double number;
      status = data_bind_value_get_double(value, &number);
      if (status == DATA_BIND_OK) lua_pushnumber(L, (lua_Number)number);
      break;
    }
    case DATA_BIND_VALUE_BOOL: {
      int boolean;
      status = data_bind_value_get_bool(value, &boolean);
      if (status == DATA_BIND_OK) lua_pushboolean(L, boolean);
      break;
    }
    case DATA_BIND_VALUE_STRING: {
      const char *data = NULL;
      size_t len = 0;
      status = data_bind_value_get_string(value, &data, &len);
      if (status == DATA_BIND_OK) lua_pushlstring(L, data, len);
      break;
    }
    case DATA_BIND_VALUE_BYTES: {
      const uint8_t *data = NULL;
      size_t len = 0;
      status = data_bind_value_get_bytes(value, &data, &len);
      if (status == DATA_BIND_OK) lua_pushlstring(L, (const char *)data, len);
      break;
    }
    case DATA_BIND_VALUE_BIGINT: {
      const char *data = NULL;
      size_t len = 0;
      status = data_bind_value_get_bigint(value, &data, &len);
      if (status == DATA_BIND_OK) lua_pushlstring(L, data, len);
      break;
    }
    case DATA_BIND_VALUE_UUID:
    case DATA_BIND_VALUE_DATETIME:
    case DATA_BIND_VALUE_DATE:
    case DATA_BIND_VALUE_TIME:
    case DATA_BIND_VALUE_DURATION:
    case DATA_BIND_VALUE_DECIMAL:
    case DATA_BIND_VALUE_MONEY:
      status = c11_lua_data_bind_push_text_scalar(L, value, data_bind_value_kind(value));
      break;
    default:
      status = DATA_BIND_ERR_TYPE_MISMATCH;
      break;
  }

  if (status != DATA_BIND_OK || lua_gettop(L) != base + 1) {
    lua_settop(L, base);
    return status == DATA_BIND_OK ? DATA_BIND_ERR_RUNTIME : status;
  }
  return DATA_BIND_OK;
}

/**
 * @brief Copies a schema-validated dynamic value onto the Lua stack.
 * @param max_depth Non-zero maximum node depth, including the root node.
 * @return DATA_BIND_OK or a DataBind error. Failure restores the original stack.
 *
 * Objects/maps become tables, lists/sets become 1-based arrays, bytes become
 * binary Lua strings, and extended schema scalars become canonical strings.
 * Integer values that cannot fit lua_Integer return DATA_BIND_ERR_LIMIT rather
 * than losing precision.
 */
static inline DataBindStatus c11_lua_push_data_bind_value(
    lua_State *L, const DataBindValue *value, size_t max_depth) {
  return c11_lua_data_bind_push_value_impl(L, value, 0u, max_depth);
}

/** Copies an owning DataBindObject's borrowed root value onto the Lua stack. */
static inline DataBindStatus c11_lua_push_data_bind_object(
    lua_State *L, const DataBindObject *object, size_t max_depth) {
  if (!object) return DATA_BIND_ERR_INVALID_ARG;
  return c11_lua_push_data_bind_value(L, data_bind_object_value(object), max_depth);
}

#ifdef __cplusplus
}
#endif

#endif
