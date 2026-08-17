/**
 * @file c11_lua_typed.h
 * @brief Direct adapters between TBE owning C records and Lua values.
 *
 * The TbeTypedType descriptor is the sole field/type metadata source. Values
 * are copied onto the Lua stack and no pointer into the owning C record is
 * retained. A generated owning record must remain initialized while this
 * function reads it.
 */
#ifndef C11_LUA_TYPED_H
#define C11_LUA_TYPED_H

#include "c11_lua_data_bind.h"
#include "tbe_typed.h"
#include "turbo_uuid.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline DataBindStatus c11_lua_typed_push_record(
    lua_State *L, const TbeTypedType *type, const void *object, size_t depth,
    size_t max_depth);

static inline int c11_lua_typed_optional_present(const TbeTypedType *type,
                                                 const void *object,
                                                 const TbeTypedField *field) {
  const uint8_t *presence;
  unsigned bit;

  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0u) return 1;
  bit = field->optional_bit;
  if (bit >= type->presence_size * CHAR_BIT) return 0;
  presence = (const uint8_t *)object + type->presence_offset;
  return (presence[bit / CHAR_BIT] & (uint8_t)(1u << (bit % CHAR_BIT))) != 0u;
}

static inline DataBindStatus c11_lua_typed_push_scalar(
    lua_State *L, TbeTypedKind kind, TbeTypedKind wire_kind, const void *value) {
  if (kind == TBE_TYPED_ENUM) kind = wire_kind;
  switch (kind) {
    case TBE_TYPED_BOOL:
      lua_pushboolean(L, *(const uint8_t *)value != 0u);
      return DATA_BIND_OK;
    case TBE_TYPED_I8:
      return c11_lua_data_bind_push_int64(L, *(const int8_t *)value);
    case TBE_TYPED_U8:
      return c11_lua_data_bind_push_uint64(L, *(const uint8_t *)value);
    case TBE_TYPED_I16:
      return c11_lua_data_bind_push_int64(L, *(const int16_t *)value);
    case TBE_TYPED_U16:
      return c11_lua_data_bind_push_uint64(L, *(const uint16_t *)value);
    case TBE_TYPED_I32:
      return c11_lua_data_bind_push_int64(L, *(const int32_t *)value);
    case TBE_TYPED_U32:
      return c11_lua_data_bind_push_uint64(L, *(const uint32_t *)value);
    case TBE_TYPED_I64:
      return c11_lua_data_bind_push_int64(L, *(const int64_t *)value);
    case TBE_TYPED_U64:
      return c11_lua_data_bind_push_uint64(L, *(const uint64_t *)value);
    case TBE_TYPED_F32: {
      float number = *(const float *)value;
      if (!isfinite(number)) return DATA_BIND_ERR_TYPE_MISMATCH;
      lua_pushnumber(L, (lua_Number)number);
      return DATA_BIND_OK;
    }
    case TBE_TYPED_F64: {
      double number = *(const double *)value;
      if (!isfinite(number)) return DATA_BIND_ERR_TYPE_MISMATCH;
      lua_pushnumber(L, (lua_Number)number);
      return DATA_BIND_OK;
    }
    case TBE_TYPED_STRING: {
      tstr_t text = *(const tstr_t *)value;
      size_t len = text != NULL ? tstr_len(text) : 0u;
      if (!tstr_v_utf8_valid(tstr_v_from_buf(text != NULL ? text : "", len)))
        return DATA_BIND_ERR_TYPE_MISMATCH;
      lua_pushlstring(L, text != NULL ? text : "", len);
      return DATA_BIND_OK;
    }
    case TBE_TYPED_UUID: {
      char text[TURBO_UUID_STRING_SIZE];
      if (turbo_uuid_format((const turbo_uuid_t *)value, text, sizeof(text)) != TURBO_OK)
        return DATA_BIND_ERR_TYPE_MISMATCH;
      lua_pushstring(L, text);
      return DATA_BIND_OK;
    }
    default:
      return DATA_BIND_ERR_TYPE_MISMATCH;
  }
}

static inline DataBindStatus c11_lua_typed_push_one(
    lua_State *L, TbeTypedKind kind, TbeTypedKind wire_kind,
    const TbeTypedType *object_type, const void *value, size_t depth,
    size_t max_depth) {
  if (depth >= max_depth || !lua_checkstack(L, 4)) return DATA_BIND_ERR_LIMIT;
  if (kind == TBE_TYPED_OBJECT) {
    if (object_type == NULL) return DATA_BIND_ERR_TYPE_MISMATCH;
    return c11_lua_typed_push_record(L, object_type, value, depth, max_depth);
  }
  if (kind == TBE_TYPED_BYTES) {
    const turbo_vec_t *bytes = (const turbo_vec_t *)value;
    if (bytes->elem_size != sizeof(uint8_t) || bytes->size > bytes->capacity ||
        (bytes->size != 0u && bytes->data == NULL))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    lua_pushlstring(L, (const char *)(bytes->data != NULL ? bytes->data : ""), bytes->size);
    return DATA_BIND_OK;
  }
  return c11_lua_typed_push_scalar(L, kind, wire_kind, value);
}

static inline DataBindStatus c11_lua_typed_push_sequence(
    lua_State *L, const TbeTypedField *field, const void *field_value,
    size_t depth, size_t max_depth) {
  const uint8_t *data;
  size_t count;
  size_t index;

  if (field->kind == TBE_TYPED_FIXED_ARRAY) {
    data = (const uint8_t *)field_value;
    count = field->fixed_count;
  } else {
    const turbo_vec_t *vector = (const turbo_vec_t *)field_value;
    if (vector->elem_size != field->element_size || vector->size > vector->capacity ||
        (vector->size != 0u && vector->data == NULL))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    data = (const uint8_t *)vector->data;
    count = vector->size;
  }
  if (count > (size_t)INT_MAX ||
      (count != 0u && field->element_size > SIZE_MAX / count))
    return DATA_BIND_ERR_LIMIT;

  lua_createtable(L, (int)count, 0);
  for (index = 0; index < count; ++index) {
    DataBindStatus status = c11_lua_typed_push_one(
        L, field->element_kind, field->element_wire_kind, field->object_type,
        data + index * field->element_size, depth + 1u, max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_rawseti(L, -2, (lua_Integer)index + 1);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_push_map(
    lua_State *L, const TbeTypedField *field, const void *field_value,
    size_t depth, size_t max_depth) {
  const turbo_vec_t *map = (const turbo_vec_t *)field_value;
  size_t index;

  if (map->elem_size != field->map_entry_size || map->size > map->capacity ||
      (map->size != 0u && map->data == NULL))
    return DATA_BIND_ERR_TYPE_MISMATCH;
  if (map->size > (size_t)INT_MAX ||
      (map->size != 0u && field->map_entry_size > SIZE_MAX / map->size))
    return DATA_BIND_ERR_LIMIT;

  lua_createtable(L, 0, (int)map->size);
  for (index = 0; index < map->size; ++index) {
    const uint8_t *entry = (const uint8_t *)map->data + index * field->map_entry_size;
    tstr_t key;
    size_t key_len;
    DataBindStatus status;

    memcpy(&key, entry + field->map_key_offset, sizeof(key));
    key_len = key != NULL ? tstr_len(key) : 0u;
    if (!tstr_v_utf8_valid(tstr_v_from_buf(key != NULL ? key : "", key_len)))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    lua_pushlstring(L, key != NULL ? key : "", key_len);
    status = c11_lua_typed_push_one(
        L, field->map_value_kind, field->map_value_wire_kind,
        field->map_value_type, entry + field->map_value_offset, depth + 1u,
        max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_settable(L, -3);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_push_field(
    lua_State *L, const TbeTypedField *field, const void *field_value,
    size_t depth, size_t max_depth) {
  if (depth >= max_depth || !lua_checkstack(L, 4)) return DATA_BIND_ERR_LIMIT;
  if (field->kind == TBE_TYPED_FIXED_BYTES) {
    lua_pushlstring(L, (const char *)field_value, field->fixed_count);
    return DATA_BIND_OK;
  }
  if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
      field->kind == TBE_TYPED_SET)
    return c11_lua_typed_push_sequence(L, field, field_value, depth, max_depth);
  if (field->kind == TBE_TYPED_MAP)
    return c11_lua_typed_push_map(L, field, field_value, depth, max_depth);
  return c11_lua_typed_push_one(L, field->kind, field->wire_kind,
                                field->object_type, field_value, depth,
                                max_depth);
}

static inline DataBindStatus c11_lua_typed_push_record(
    lua_State *L, const TbeTypedType *type, const void *object, size_t depth,
    size_t max_depth) {
  size_t field_index;
  size_t present_count = 0u;

  if (depth >= max_depth || !lua_checkstack(L, 4)) return DATA_BIND_ERR_LIMIT;
  for (field_index = 0; field_index < type->field_count; ++field_index) {
    if (c11_lua_typed_optional_present(type, object, &type->fields[field_index]))
      ++present_count;
  }
  if (present_count > (size_t)INT_MAX) return DATA_BIND_ERR_LIMIT;
  lua_createtable(L, 0, (int)present_count);
  for (field_index = 0; field_index < type->field_count; ++field_index) {
    const TbeTypedField *field = &type->fields[field_index];
    DataBindStatus status;
    if (!c11_lua_typed_optional_present(type, object, field)) continue;
    status = c11_lua_typed_push_field(
        L, field, (const uint8_t *)object + field->offset, depth + 1u,
        max_depth);
    if (status != DATA_BIND_OK) return status;
    lua_setfield(L, -2, field->name);
  }
  return DATA_BIND_OK;
}

/**
 * Copy one initialized owning TBE record to a Lua table.
 *
 * Missing optional fields are omitted. Lists and sets become 1-based arrays;
 * maps and nested records become tables; bytes become binary Lua strings.
 * Integers outside lua_Integer fail with DATA_BIND_ERR_LIMIT. On every failure
 * the original Lua stack is restored.
 */
static inline DataBindStatus c11_lua_push_tbe_typed(
    lua_State *L, const TbeTypedType *type, const void *object,
    size_t max_depth) {
  DataBindStatus status;
  int base;

  if (L == NULL || type == NULL || object == NULL || max_depth == 0u)
    return DATA_BIND_ERR_INVALID_ARG;
  status = tbe_typed_validate_descriptor(type, NULL);
  if (status != DATA_BIND_OK) return status;
  base = lua_gettop(L);
  status = c11_lua_typed_push_record(L, type, object, 0u, max_depth);
  if (status != DATA_BIND_OK || lua_gettop(L) != base + 1) {
    lua_settop(L, base);
    return status == DATA_BIND_OK ? DATA_BIND_ERR_RUNTIME : status;
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_read_record(
    lua_State *L, int index, const TbeTypedType *type, void *object,
    size_t depth, size_t max_depth, size_t max_dynamic_items);

static inline void c11_lua_typed_optional_set(const TbeTypedType *type,
                                              void *object,
                                              const TbeTypedField *field) {
  uint8_t *presence;
  unsigned bit;

  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0u) return;
  bit = field->optional_bit;
  if (bit >= type->presence_size * CHAR_BIT) return;
  presence = (uint8_t *)object + type->presence_offset;
  presence[bit / CHAR_BIT] |= (uint8_t)(1u << (bit % CHAR_BIT));
}

static inline DataBindStatus c11_lua_typed_read_scalar(
    lua_State *L, int index, TbeTypedKind kind, TbeTypedKind wire_kind,
    void *out, size_t max_dynamic_items) {
  lua_Integer integer;

  if (kind == TBE_TYPED_ENUM) kind = wire_kind;
  if (kind == TBE_TYPED_BOOL) {
    if (lua_type(L, index) != LUA_TBOOLEAN) return DATA_BIND_ERR_TYPE_MISMATCH;
    *(uint8_t *)out = (uint8_t)(lua_toboolean(L, index) != 0);
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_F32 || kind == TBE_TYPED_F64) {
    lua_Number number;
    if (lua_type(L, index) != LUA_TNUMBER) return DATA_BIND_ERR_TYPE_MISMATCH;
    number = lua_tonumber(L, index);
    if (!isfinite((double)number)) return DATA_BIND_ERR_TYPE_MISMATCH;
    if (kind == TBE_TYPED_F32) {
      if ((double)number < -(double)FLT_MAX || (double)number > (double)FLT_MAX)
        return DATA_BIND_ERR_TYPE_MISMATCH;
      *(float *)out = (float)number;
    } else {
      *(double *)out = (double)number;
    }
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_STRING) {
    const char *text;
    size_t len;
    tstr_t copy;
    if (lua_type(L, index) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    text = lua_tolstring(L, index, &len);
    if (len > max_dynamic_items) return DATA_BIND_ERR_LIMIT;
    if (!tstr_v_utf8_valid(tstr_v_from_buf(text, len)))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    copy = tstr_dup_len(text, len);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    *(tstr_t *)out = copy;
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_UUID) {
    const char *text;
    size_t len;
    if (lua_type(L, index) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    text = lua_tolstring(L, index, &len);
    if (len != TURBO_UUID_STRING_SIZE - 1u ||
        turbo_uuid_parse(text, (turbo_uuid_t *)out) != TURBO_OK)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    return DATA_BIND_OK;
  }
  if (!lua_isinteger(L, index)) return DATA_BIND_ERR_TYPE_MISMATCH;
  integer = lua_tointeger(L, index);
  switch (kind) {
    case TBE_TYPED_I8:
      if (integer < INT8_MIN || integer > INT8_MAX) return DATA_BIND_ERR_TYPE_MISMATCH;
      *(int8_t *)out = (int8_t)integer;
      break;
    case TBE_TYPED_U8:
      if (integer < 0 || (lua_Unsigned)integer > UINT8_MAX)
        return DATA_BIND_ERR_TYPE_MISMATCH;
      *(uint8_t *)out = (uint8_t)integer;
      break;
    case TBE_TYPED_I16:
      if (integer < INT16_MIN || integer > INT16_MAX) return DATA_BIND_ERR_TYPE_MISMATCH;
      *(int16_t *)out = (int16_t)integer;
      break;
    case TBE_TYPED_U16:
      if (integer < 0 || (lua_Unsigned)integer > UINT16_MAX)
        return DATA_BIND_ERR_TYPE_MISMATCH;
      *(uint16_t *)out = (uint16_t)integer;
      break;
    case TBE_TYPED_I32:
      if (integer < INT32_MIN || integer > INT32_MAX) return DATA_BIND_ERR_TYPE_MISMATCH;
      *(int32_t *)out = (int32_t)integer;
      break;
    case TBE_TYPED_U32:
      if (integer < 0 || (lua_Unsigned)integer > UINT32_MAX)
        return DATA_BIND_ERR_TYPE_MISMATCH;
      *(uint32_t *)out = (uint32_t)integer;
      break;
    case TBE_TYPED_I64:
      *(int64_t *)out = (int64_t)integer;
      break;
    case TBE_TYPED_U64:
      if (integer < 0) return DATA_BIND_ERR_TYPE_MISMATCH;
      *(uint64_t *)out = (uint64_t)(lua_Unsigned)integer;
      break;
    default:
      return DATA_BIND_ERR_TYPE_MISMATCH;
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_init_one(
    TbeTypedKind kind, const TbeTypedType *object_type, void *out) {
  if (kind == TBE_TYPED_OBJECT) {
    if (object_type == NULL) return DATA_BIND_ERR_TYPE_MISMATCH;
    return tbe_typed_init(object_type, out, NULL);
  }
  if (kind == TBE_TYPED_BYTES)
    return turbo_vec_init((turbo_vec_t *)out, sizeof(uint8_t)) == TURBO_OK
               ? DATA_BIND_OK
               : DATA_BIND_ERR_OOM;
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_read_one(
    lua_State *L, int index, TbeTypedKind kind, TbeTypedKind wire_kind,
    const TbeTypedType *object_type, void *out, size_t depth,
    size_t max_depth, size_t max_dynamic_items) {
  if (depth >= max_depth || !lua_checkstack(L, 4)) return DATA_BIND_ERR_LIMIT;
  if (kind == TBE_TYPED_OBJECT)
    return c11_lua_typed_read_record(L, index, object_type, out, depth,
                                     max_depth, max_dynamic_items);
  if (kind == TBE_TYPED_BYTES) {
    const char *bytes;
    size_t len;
    turbo_vec_t *vector = (turbo_vec_t *)out;
    if (lua_type(L, index) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    bytes = lua_tolstring(L, index, &len);
    if (len > max_dynamic_items) return DATA_BIND_ERR_LIMIT;
    if (turbo_vec_resize(vector, len) != TURBO_OK) return DATA_BIND_ERR_OOM;
    if (len != 0u) memcpy(vector->data, bytes, len);
    return DATA_BIND_OK;
  }
  return c11_lua_typed_read_scalar(L, index, kind, wire_kind, out,
                                   max_dynamic_items);
}

static inline DataBindStatus c11_lua_typed_check_sequence_shape(
    lua_State *L, int index, size_t count, size_t max_dynamic_items) {
  size_t entries = 0u;
  int table_index = lua_absindex(L, index);

  if (count > max_dynamic_items) return DATA_BIND_ERR_LIMIT;
  lua_pushnil(L);
  while (lua_next(L, table_index) != 0) {
    lua_Integer key;
    if (!lua_isinteger(L, -2)) return DATA_BIND_ERR_TYPE_MISMATCH;
    key = lua_tointeger(L, -2);
    if (key < 1 || (lua_Unsigned)key > count)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    ++entries;
    if (entries > count) return DATA_BIND_ERR_TYPE_MISMATCH;
    lua_pop(L, 1);
  }
  return entries == count ? DATA_BIND_OK : DATA_BIND_ERR_TYPE_MISMATCH;
}

static inline DataBindStatus c11_lua_typed_read_sequence(
    lua_State *L, int index, const TbeTypedField *field, void *out,
    size_t depth, size_t max_depth, size_t max_dynamic_items) {
  int table_index = lua_absindex(L, index);
  size_t count;
  size_t element_index;
  uint8_t *data;
  DataBindStatus status;

  if (lua_type(L, table_index) != LUA_TTABLE) return DATA_BIND_ERR_TYPE_MISMATCH;
  count = lua_rawlen(L, table_index);
  if (field->kind == TBE_TYPED_FIXED_ARRAY && count != field->fixed_count)
    return DATA_BIND_ERR_TYPE_MISMATCH;
  status = c11_lua_typed_check_sequence_shape(L, table_index, count,
                                              max_dynamic_items);
  if (status != DATA_BIND_OK) return status;
  if (count != 0u && field->element_size > SIZE_MAX / count)
    return DATA_BIND_ERR_LIMIT;

  if (field->kind == TBE_TYPED_FIXED_ARRAY) {
    data = (uint8_t *)out;
  } else {
    turbo_vec_t *vector = (turbo_vec_t *)out;
    if (turbo_vec_resize(vector, count) != TURBO_OK) return DATA_BIND_ERR_OOM;
    data = (uint8_t *)vector->data;
    if (count != 0u) memset(data, 0, count * field->element_size);
    for (element_index = 0; element_index < count; ++element_index) {
      status = c11_lua_typed_init_one(field->element_kind,
                                      field->object_type,
                                      data + element_index * field->element_size);
      if (status != DATA_BIND_OK) return status;
    }
  }
  for (element_index = 0; element_index < count; ++element_index) {
    lua_rawgeti(L, table_index, (lua_Integer)element_index + 1);
    status = c11_lua_typed_read_one(
        L, -1, field->element_kind, field->element_wire_kind,
        field->object_type, data + element_index * field->element_size,
        depth + 1u, max_depth, max_dynamic_items);
    lua_pop(L, 1);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_count_map(
    lua_State *L, int index, size_t max_dynamic_items, size_t *out_count) {
  int table_index = lua_absindex(L, index);
  size_t count = 0u;

  lua_pushnil(L);
  while (lua_next(L, table_index) != 0) {
    const char *key;
    size_t key_len;
    if (lua_type(L, -2) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    key = lua_tolstring(L, -2, &key_len);
    if (key_len > max_dynamic_items) return DATA_BIND_ERR_LIMIT;
    if (!tstr_v_utf8_valid(tstr_v_from_buf(key, key_len)) ||
        memchr(key, '\0', key_len) != NULL)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    ++count;
    if (count > max_dynamic_items) return DATA_BIND_ERR_LIMIT;
    lua_pop(L, 1);
  }
  *out_count = count;
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_read_map(
    lua_State *L, int index, const TbeTypedField *field, void *out,
    size_t depth, size_t max_depth, size_t max_dynamic_items) {
  int table_index = lua_absindex(L, index);
  turbo_vec_t *map = (turbo_vec_t *)out;
  size_t count;
  size_t item_index = 0u;
  DataBindStatus status;

  if (lua_type(L, table_index) != LUA_TTABLE) return DATA_BIND_ERR_TYPE_MISMATCH;
  status = c11_lua_typed_count_map(L, table_index, max_dynamic_items, &count);
  if (status != DATA_BIND_OK) return status;
  if (count != 0u && field->map_entry_size > SIZE_MAX / count)
    return DATA_BIND_ERR_LIMIT;
  if (turbo_vec_resize(map, count) != TURBO_OK) return DATA_BIND_ERR_OOM;
  if (count != 0u) memset(map->data, 0, count * field->map_entry_size);

  lua_pushnil(L);
  while (lua_next(L, table_index) != 0) {
    uint8_t *entry = (uint8_t *)map->data + item_index * field->map_entry_size;
    const char *key;
    size_t key_len;
    tstr_t key_copy;

    key = lua_tolstring(L, -2, &key_len);
    key_copy = tstr_dup_len(key, key_len);
    if (key_copy == NULL) return DATA_BIND_ERR_OOM;
    memcpy(entry + field->map_key_offset, &key_copy, sizeof(key_copy));
    status = c11_lua_typed_init_one(field->map_value_kind,
                                    field->map_value_type,
                                    entry + field->map_value_offset);
    if (status == DATA_BIND_OK)
      status = c11_lua_typed_read_one(
          L, -1, field->map_value_kind, field->map_value_wire_kind,
          field->map_value_type, entry + field->map_value_offset,
          depth + 1u, max_depth, max_dynamic_items);
    if (status != DATA_BIND_OK) return status;
    ++item_index;
    lua_pop(L, 1);
  }
  return DATA_BIND_OK;
}

static inline int c11_lua_typed_record_has_field(const TbeTypedType *type,
                                                 const char *key,
                                                 size_t key_len) {
  size_t field_index;
  for (field_index = 0; field_index < type->field_count; ++field_index) {
    const char *name = type->fields[field_index].name;
    if (strlen(name) == key_len && memcmp(name, key, key_len) == 0) return 1;
  }
  return 0;
}

static inline DataBindStatus c11_lua_typed_check_record_shape(
    lua_State *L, int index, const TbeTypedType *type) {
  int table_index = lua_absindex(L, index);
  size_t entries = 0u;

  lua_pushnil(L);
  while (lua_next(L, table_index) != 0) {
    const char *key;
    size_t key_len;
    if (lua_type(L, -2) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    key = lua_tolstring(L, -2, &key_len);
    if (!c11_lua_typed_record_has_field(type, key, key_len))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    ++entries;
    if (entries > type->field_count) return DATA_BIND_ERR_TYPE_MISMATCH;
    lua_pop(L, 1);
  }
  return DATA_BIND_OK;
}

static inline DataBindStatus c11_lua_typed_read_field(
    lua_State *L, int index, const TbeTypedField *field, void *out,
    size_t depth, size_t max_depth, size_t max_dynamic_items) {
  if (depth >= max_depth || !lua_checkstack(L, 4)) return DATA_BIND_ERR_LIMIT;
  if (field->kind == TBE_TYPED_FIXED_BYTES) {
    const char *bytes;
    size_t len;
    if (lua_type(L, index) != LUA_TSTRING) return DATA_BIND_ERR_TYPE_MISMATCH;
    bytes = lua_tolstring(L, index, &len);
    if (len != field->fixed_count) return DATA_BIND_ERR_TYPE_MISMATCH;
    if (len != 0u) memcpy(out, bytes, len);
    return DATA_BIND_OK;
  }
  if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
      field->kind == TBE_TYPED_SET)
    return c11_lua_typed_read_sequence(L, index, field, out, depth,
                                       max_depth, max_dynamic_items);
  if (field->kind == TBE_TYPED_MAP)
    return c11_lua_typed_read_map(L, index, field, out, depth, max_depth,
                                  max_dynamic_items);
  return c11_lua_typed_read_one(L, index, field->kind, field->wire_kind,
                                field->object_type, out, depth, max_depth,
                                max_dynamic_items);
}

static inline DataBindStatus c11_lua_typed_read_record(
    lua_State *L, int index, const TbeTypedType *type, void *object,
    size_t depth, size_t max_depth, size_t max_dynamic_items) {
  int table_index = lua_absindex(L, index);
  size_t field_index;
  DataBindStatus status;

  if (type == NULL || depth >= max_depth || !lua_checkstack(L, 4))
    return DATA_BIND_ERR_LIMIT;
  if (lua_type(L, table_index) != LUA_TTABLE) return DATA_BIND_ERR_TYPE_MISMATCH;
  status = c11_lua_typed_check_record_shape(L, table_index, type);
  if (status != DATA_BIND_OK) return status;

  for (field_index = 0; field_index < type->field_count; ++field_index) {
    const TbeTypedField *field = &type->fields[field_index];
    void *out = (uint8_t *)object + field->offset;
    lua_pushstring(L, field->name);
    lua_rawget(L, table_index);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0u) continue;
      return DATA_BIND_ERR_TYPE_MISMATCH;
    }
    status = c11_lua_typed_read_field(L, -1, field, out, depth + 1u,
                                      max_depth, max_dynamic_items);
    lua_pop(L, 1);
    if (status != DATA_BIND_OK) return status;
    c11_lua_typed_optional_set(type, object, field);
  }
  return DATA_BIND_OK;
}

/**
 * Transactionally copy one canonical Lua table into an initialized TBE record.
 *
 * The table is borrowed only for this call. Canonical descriptor field names
 * are required; unknown keys, missing required fields, sparse arrays, mixed
 * map keys, range loss, and configured resource-limit violations fail. The
 * destination is replaced only after a complete conversion. On failure both
 * the destination and Lua stack are unchanged.
 */
static inline DataBindStatus c11_lua_read_tbe_typed(
    lua_State *L, int index, const TbeTypedType *type, void *object,
    size_t max_depth, size_t max_dynamic_items) {
  void *temporary;
  DataBindStatus status;
  int base;
  int table_index;

  if (L == NULL || type == NULL || object == NULL || max_depth == 0u ||
      max_dynamic_items == 0u)
    return DATA_BIND_ERR_INVALID_ARG;
  status = tbe_typed_validate_descriptor(type, NULL);
  if (status != DATA_BIND_OK) return status;
  base = lua_gettop(L);
  table_index = lua_absindex(L, index);
  if (table_index <= 0 || table_index > base || lua_type(L, table_index) != LUA_TTABLE)
    return DATA_BIND_ERR_TYPE_MISMATCH;

  temporary = calloc(1u, type->size);
  if (temporary == NULL) return DATA_BIND_ERR_OOM;
  status = tbe_typed_init(type, temporary, NULL);
  if (status == DATA_BIND_OK)
    status = c11_lua_typed_read_record(
        L, table_index, type, temporary, 0u, max_depth, max_dynamic_items);
  lua_settop(L, base);
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

#ifdef __cplusplus
}
#endif

#endif
