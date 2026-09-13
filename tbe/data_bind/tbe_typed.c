#include "tbe_typed_internal.h"

#include "data_bind_internal.h"
#include "fmt.h"
#include "tbe_wire.h"
#include <json_parser.h>
#include <salts_cmeta_data.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct TbeTypedCMetaKindMapping {
  const cmeta_data_desc *data;
  TbeTypedKind kind;
} TbeTypedCMetaKindMapping;

static const TbeTypedCMetaKindMapping typed_cmeta_kind_mappings[] = {
    {&cmeta_data_bool, TBE_TYPED_BOOL},
    {&salts_int8_cmeta_data, TBE_TYPED_I8},
    {&salts_uint8_cmeta_data, TBE_TYPED_U8},
    {&salts_int16_cmeta_data, TBE_TYPED_I16},
    {&salts_uint16_cmeta_data, TBE_TYPED_U16},
    {&salts_int32_cmeta_data, TBE_TYPED_I32},
    {&salts_uint32_cmeta_data, TBE_TYPED_U32},
    {&salts_int64_cmeta_data, TBE_TYPED_I64},
    {&salts_uint64_cmeta_data, TBE_TYPED_U64},
    {&cmeta_data_float, TBE_TYPED_F32},
    {&cmeta_data_double, TBE_TYPED_F64},
};

static int typed_cmeta_shape_matches(const cmeta_data_desc *data,
                                     const cmeta_data_desc *canonical) {
  if (data->kind == CMETA_DATA_SINT || data->kind == CMETA_DATA_UINT) {
    const cmeta_data_integer_shape *actual =
        (const cmeta_data_integer_shape *)data->shape;
    const cmeta_data_integer_shape *expected =
        (const cmeta_data_integer_shape *)canonical->shape;
    return actual->bits == expected->bits;
  }
  if (data->kind == CMETA_DATA_FLOAT) {
    const cmeta_data_float_shape *actual =
        (const cmeta_data_float_shape *)data->shape;
    const cmeta_data_float_shape *expected =
        (const cmeta_data_float_shape *)canonical->shape;
    return actual->bits == expected->bits;
  }
  return data->kind == CMETA_DATA_BOOL;
}

static int typed_cmeta_scalar_matches(const cmeta_data_desc *data,
                                      const cmeta_data_desc *canonical) {
  const cmeta_type_desc *actual = data->storage_type;
  const cmeta_type_desc *expected = canonical->storage_type;
  return data->kind == canonical->kind && cmeta_type_equal(actual, expected) &&
         actual->kind == expected->kind && actual->size == expected->size &&
         actual->align == expected->align && typed_cmeta_shape_matches(data, canonical);
}

int tbe_typed_kind_from_cmeta_data(const cmeta_data_desc *data,
                                   TbeTypedKind *out_kind) {
  size_t i;
  if (out_kind == NULL || !cmeta_data_desc_valid(data)) return 0;
  if (salts_uuid_cmeta_data_valid(data)) {
    *out_kind = TBE_TYPED_UUID;
    return 1;
  }
  for (i = 0u;
       i < sizeof(typed_cmeta_kind_mappings) / sizeof(typed_cmeta_kind_mappings[0]); ++i) {
    if (typed_cmeta_scalar_matches(data, typed_cmeta_kind_mappings[i].data)) {
      *out_kind = typed_cmeta_kind_mappings[i].kind;
      return 1;
    }
  }
  return 0;
}

static DataBindStatus typed_error(DataBindError *error, DataBindStatus status, const char *path,
                                  const char *message) {
  if (error != NULL && error->size >= sizeof(error->size)) {
    if (error->size >= offsetof(DataBindError, code) + sizeof(error->code)) error->code = status;
    if (error->size >= offsetof(DataBindError, line) + sizeof(error->line)) error->line = -1;
    if (error->size >= offsetof(DataBindError, column) + sizeof(error->column)) error->column = -1;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path))
      snprintf(error->path, sizeof(error->path), "%s", path ? path : "");
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
      snprintf(error->message, sizeof(error->message), "%s", message ? message : "");
  }
  return status;
}

static int typed_is_integer(TbeTypedKind kind) {
  return kind >= TBE_TYPED_I8 && kind <= TBE_TYPED_U64;
}

static int typed_is_signed(TbeTypedKind kind) {
  return kind == TBE_TYPED_I8 || kind == TBE_TYPED_I16 || kind == TBE_TYPED_I32 ||
         kind == TBE_TYPED_I64;
}

static TbeTypedKind typed_enum_storage_kind(TbeTypedKind wire_kind) {
  return typed_is_integer(wire_kind) ? wire_kind : TBE_TYPED_I32;
}

static size_t typed_kind_size(TbeTypedKind kind) {
  switch (kind) {
  case TBE_TYPED_BOOL:
  case TBE_TYPED_I8:
  case TBE_TYPED_U8:
    return 1;
  case TBE_TYPED_I16:
  case TBE_TYPED_U16:
    return 2;
  case TBE_TYPED_I32:
  case TBE_TYPED_U32:
  case TBE_TYPED_F32:
  case TBE_TYPED_ENUM:
    return 4;
  case TBE_TYPED_I64:
  case TBE_TYPED_U64:
  case TBE_TYPED_F64:
    return 8;
  case TBE_TYPED_UUID:
    return SALTS_UUID_SIZE;
  default:
    return 0;
  }
}

static int typed_size_fits(size_t offset, size_t size, size_t capacity) {
  return offset <= capacity && size <= capacity - offset;
}

static int typed_multiply_fits(size_t count, size_t size, size_t *total) {
  if (total == NULL || (size != 0 && count > SIZE_MAX / size)) return 0;
  *total = count * size;
  return 1;
}

static int typed_add_fits(size_t left, size_t right, size_t *total) {
  if (total == NULL || right > SIZE_MAX - left) return 0;
  *total = left + right;
  return 1;
}

static int typed_ranges_overlap(size_t left_offset, size_t left_size, size_t right_offset,
                                size_t right_size) {
  size_t left_end;
  size_t right_end;
  if (left_size == 0 || right_size == 0) return 0;
  if (!typed_add_fits(left_offset, left_size, &left_end) ||
      !typed_add_fits(right_offset, right_size, &right_end))
    return 1;
  return left_offset < right_end && right_offset < left_end;
}

static DataBindStatus typed_validate_descriptor_at(const TbeTypedType *type, unsigned depth,
                                                   DataBindError *error);

static int typed_optional_present(const TbeTypedType *type, const void *object,
                                  const TbeTypedField *field) {
  const uint8_t *presence;
  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0) return 1;
  if (type->presence_size == 0) return 0;
  presence = (const uint8_t *)object + type->presence_offset;
  return (presence[field->optional_bit / 8u] & (uint8_t)(1u << (field->optional_bit % 8u))) != 0;
}

static void typed_optional_set(const TbeTypedType *type, void *object, const TbeTypedField *field) {
  uint8_t *presence;
  if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) == 0 || type->presence_size == 0) return;
  presence = (uint8_t *)object + type->presence_offset;
  presence[field->optional_bit / 8u] |= (uint8_t)(1u << (field->optional_bit % 8u));
}

static DataBindStatus typed_init_value(TbeTypedKind kind, const TbeTypedType *object_type,
                                       void *ptr, size_t count, DataBindError *error) {
  size_t i;
  if (kind == TBE_TYPED_STRING) {
    for (i = 0; i < count; ++i)
      ((tstr *)ptr)[i] = NULL;
  } else if (kind == TBE_TYPED_BYTES) {
    for (i = 0; i < count; ++i) {
      if (vec_init_bytes(&((vec_t *)ptr)[i], sizeof(uint8_t), _Alignof(uint8_t),
                               SIZE_MAX) != STL_OK)
        return typed_error(error, DATA_BIND_ERR_RUNTIME, NULL, "Failed to initialize vector");
    }
  } else if (kind == TBE_TYPED_OBJECT) {
    for (i = 0; i < count; ++i) {
      DataBindStatus status =
          tbe_typed_init(object_type, (uint8_t *)ptr + i * object_type->size, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_init(const TbeTypedType *type, void *object, DataBindError *error) {
  size_t i;
  DataBindStatus descriptor_status;
  if (type == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed object");
  descriptor_status = typed_validate_descriptor_at(type, 0u, error);
  if (descriptor_status != DATA_BIND_OK) return descriptor_status;
  memset(object, 0, type->size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    void *ptr = (uint8_t *)object + field->offset;
    DataBindStatus status;
    if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      status =
          typed_init_value(field->element_kind, field->object_type, ptr, field->fixed_count, error);
    } else if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET || field->kind == TBE_TYPED_MAP) {
      status = vec_init_bytes((vec_t *)ptr,
                                    field->kind == TBE_TYPED_BYTES ? sizeof(uint8_t)
                                                                   : field->element_size,
                                    _Alignof(uint8_t),
                                    SIZE_MAX) == STL_OK
                   ? DATA_BIND_OK
                   : typed_error(error, DATA_BIND_ERR_RUNTIME, field->name,
                                 "Failed to initialize typed vector");
    } else {
      status = typed_init_value(field->kind, field->object_type, ptr, 1, error);
    }
    if (status != DATA_BIND_OK) {
      tbe_typed_clear(type, object);
      return status;
    }
  }
  return DATA_BIND_OK;
}

static void typed_clear_value(TbeTypedKind kind, const TbeTypedType *object_type, void *ptr,
                              size_t count) {
  size_t i;
  if (kind == TBE_TYPED_STRING) {
    for (i = 0; i < count; ++i)
      tstr_free(((tstr *)ptr)[i]);
  } else if (kind == TBE_TYPED_BYTES) {
    for (i = 0; i < count; ++i)
      vec_destroy(&((vec_t *)ptr)[i]);
  } else if (kind == TBE_TYPED_OBJECT && object_type != NULL) {
    for (i = 0; i < count; ++i)
      tbe_typed_clear(object_type, (uint8_t *)ptr + i * object_type->size);
  }
}

void tbe_typed_clear(const TbeTypedType *type, void *object) {
  size_t i;
  if (type == NULL || object == NULL) return;
  if (typed_validate_descriptor_at(type, 0u, NULL) != DATA_BIND_OK) return;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    void *ptr = (uint8_t *)object + field->offset;
    if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      typed_clear_value(field->element_kind, field->object_type, ptr, field->fixed_count);
    } else if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET) {
      vec_t *vec = (vec_t *)ptr;
      typed_clear_value(field->element_kind, field->object_type, vec->data, vec->size);
      vec_destroy(vec);
    } else if (field->kind == TBE_TYPED_MAP) {
      vec_t *vec = (vec_t *)ptr;
      size_t j;
      for (j = 0; j < vec->size; ++j) {
        uint8_t *entry = (uint8_t *)vec->data + j * field->map_entry_size;
        tstr_free(*(tstr *)(entry + field->map_key_offset));
        typed_clear_value(field->map_value_kind, field->map_value_type,
                          entry + field->map_value_offset, 1);
      }
      vec_destroy(vec);
    } else if (field->kind == TBE_TYPED_BYTES) {
      vec_destroy((vec_t *)ptr);
    } else {
      typed_clear_value(field->kind, field->object_type, ptr, 1);
    }
  }
  memset(object, 0, type->size);
}

static DataBindStatus typed_read_scalar(TbeTypedKind kind, TbeTypedKind wire_kind,
                                        const DataBindValue *value, void *out, const char *path,
                                        DataBindError *error) {
  int64_t signed_value = 0;
  uint64_t unsigned_value = 0;
  double double_value = 0.0;
  int bool_value = 0;
  if (kind == TBE_TYPED_BOOL) {
    if (data_bind_value_get_bool(value, &bool_value) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected boolean value");
    *(uint8_t *)out = (uint8_t)(bool_value != 0);
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_F32 || kind == TBE_TYPED_F64) {
    if (data_bind_value_get_double(value, &double_value) != DATA_BIND_OK || !isfinite(double_value))
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected finite number");
    if (kind == TBE_TYPED_F32) {
      if (double_value < -(double)FLT_MAX || double_value > (double)FLT_MAX)
        return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                           "Number is out of range for float32");
      *(float *)out = (float)double_value;
    } else {
      *(double *)out = double_value;
    }
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_ENUM) kind = typed_enum_storage_kind(wire_kind);
  if (kind == TBE_TYPED_U64) {
    if (data_bind_value_get_uint64(value, &unsigned_value) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected unsigned integer");
    *(uint64_t *)out = unsigned_value;
    return DATA_BIND_OK;
  }
  if (!typed_is_integer(kind) || data_bind_value_get_int64(value, &signed_value) != DATA_BIND_OK)
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected integer value");
  switch (kind) {
  case TBE_TYPED_I8:
    if (signed_value < INT8_MIN || signed_value > INT8_MAX) goto range_error;
    *(int8_t *)out = (int8_t)signed_value;
    break;
  case TBE_TYPED_U8:
    if (signed_value < 0 || signed_value > UINT8_MAX) goto range_error;
    *(uint8_t *)out = (uint8_t)signed_value;
    break;
  case TBE_TYPED_I16:
    if (signed_value < INT16_MIN || signed_value > INT16_MAX) goto range_error;
    *(int16_t *)out = (int16_t)signed_value;
    break;
  case TBE_TYPED_U16:
    if (signed_value < 0 || signed_value > UINT16_MAX) goto range_error;
    *(uint16_t *)out = (uint16_t)signed_value;
    break;
  case TBE_TYPED_I32:
    if (signed_value < INT32_MIN || signed_value > INT32_MAX) goto range_error;
    *(int32_t *)out = (int32_t)signed_value;
    break;
  case TBE_TYPED_U32:
    if (signed_value < 0 || (uint64_t)signed_value > UINT32_MAX) goto range_error;
    *(uint32_t *)out = (uint32_t)signed_value;
    break;
  case TBE_TYPED_I64:
    *(int64_t *)out = signed_value;
    break;
  default:
    goto range_error;
  }
  return DATA_BIND_OK;
range_error:
  return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Integer value is out of range");
}

static DataBindStatus typed_from_value_at(const TbeTypedType *type, const DataBindValue *value,
                                          void *object, const char *prefix,
                                          DataBindError *error);

static DataBindStatus typed_from_one(TbeTypedKind kind, TbeTypedKind wire_kind,
                                     const TbeTypedType *object_type, const DataBindValue *value,
                                     void *out, const char *path, DataBindError *error) {
  if (kind == TBE_TYPED_STRING) {
    const char *text;
    size_t len;
    if (data_bind_value_get_string(value, &text, &len) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected string value");
    *(tstr *)out = tstr_dup_len(text, len);
    return *(tstr *)out != NULL
               ? DATA_BIND_OK
               : typed_error(error, DATA_BIND_ERR_OOM, path, "Out of memory copying string");
  }
  if (kind == TBE_TYPED_BYTES) {
    const uint8_t *bytes;
    size_t len;
    vec_t *vec = (vec_t *)out;
    if (data_bind_value_get_bytes(value, &bytes, &len) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected bytes value");
    if (vec_resize(vec, len) != STL_OK)
      return typed_error(error, DATA_BIND_ERR_OOM, path, "Out of memory copying bytes");
    if (len != 0) memcpy(vec->data, bytes, len);
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_UUID) {
    if (data_bind_value_get_uuid(value, ((salts_uuid_t *)out)->bytes) != DATA_BIND_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Expected UUID value");
    return DATA_BIND_OK;
  }
  if (kind == TBE_TYPED_OBJECT)
    return typed_from_value_at(object_type, value, out, "", error);
  return typed_read_scalar(kind, wire_kind, value, out, path, error);
}

static DataBindStatus typed_from_value_at(const TbeTypedType *type, const DataBindValue *value,
                                          void *object, const char *prefix, DataBindError *error) {
  size_t i;
  if (type == NULL || value == NULL || object == NULL ||
      data_bind_value_kind(value) != DATA_BIND_VALUE_OBJECT)
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, type ? type->name : NULL,
                       "Expected schema object");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const DataBindValue *child;
    char field_path[512];
    void *out = (uint8_t *)object + field->offset;
    DataBindStatus status = DATA_BIND_OK;
    size_t j;
    if (prefix != NULL && prefix[0] != '\0') {
      int written = snprintf(field_path, sizeof(field_path), "%s%s", prefix, field->name);
      if (written < 0 || (size_t)written >= sizeof(field_path))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Flattened field path is too long");
      child = data_bind_value_get(value, field_path);
    } else {
      child = data_bind_value_get(value, field->name);
    }
    if (child == NULL && field->kind == TBE_TYPED_OBJECT && field->object_type != NULL) {
      char nested_prefix[512];
      int written = snprintf(nested_prefix, sizeof(nested_prefix), "%s%s.", prefix ? prefix : "",
                             field->name);
      if (written < 0 || (size_t)written >= sizeof(nested_prefix))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Flattened object path is too long");
      status = typed_from_value_at(field->object_type, value, out, nested_prefix, error);
      if (status == DATA_BIND_OK) {
        typed_optional_set(type, object, field);
        continue;
      }
      if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0) continue;
      return status;
    }
    if (child == NULL) {
      if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0) continue;
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                         "Required field is missing");
    }
    typed_optional_set(type, object, field);
    if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_FIXED_BYTES) {
      const uint8_t *bytes;
      size_t len;
      if (data_bind_value_get_bytes(child, &bytes, &len) != DATA_BIND_OK)
        return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, "Expected bytes value");
      if (field->kind == TBE_TYPED_FIXED_BYTES) {
        if (len != field->fixed_count)
          return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                             "Fixed bytes length does not match schema");
        memcpy(out, bytes, len);
      } else {
        vec_t *vec = (vec_t *)out;
        if (vec_resize(vec, len) != STL_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name, "Out of memory copying bytes");
        if (len != 0) memcpy(vec->data, bytes, len);
      }
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      if (data_bind_value_count(child) != field->fixed_count)
        return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                           "Fixed array length does not match schema");
      for (j = 0; j < field->fixed_count; ++j) {
        status =
            typed_from_one(field->element_kind, field->element_wire_kind, field->object_type,
                           data_bind_value_at(child, j),
                           (uint8_t *)out + j * field->element_size, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET) {
      vec_t *vec = (vec_t *)out;
      size_t count = data_bind_value_count(child);
      if (vec_resize(vec, count) != STL_OK)
        return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                           "Out of memory resizing typed vector");
      memset(vec->data, 0, count * field->element_size);
      for (j = 0; j < count; ++j) {
        void *element = (uint8_t *)vec->data + j * field->element_size;
        if (field->element_kind == TBE_TYPED_OBJECT || field->element_kind == TBE_TYPED_BYTES) {
          status = typed_init_value(field->element_kind, field->object_type, element, 1, error);
          if (status != DATA_BIND_OK) return status;
        }
        status = typed_from_one(field->element_kind, field->element_wire_kind, field->object_type,
                                data_bind_value_at(child, j), element, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else if (field->kind == TBE_TYPED_MAP) {
      vec_t *vec = (vec_t *)out;
      size_t count = data_bind_value_count(child);
      if (vec_resize(vec, count) != STL_OK)
        return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                           "Out of memory resizing typed map");
      memset(vec->data, 0, count * field->map_entry_size);
      for (j = 0; j < count; ++j) {
        DataBindMapEntry item = data_bind_value_map_entry_at(child, j);
        uint8_t *entry = (uint8_t *)vec->data + j * field->map_entry_size;
        *(tstr *)(entry + field->map_key_offset) = tstr_dup(item.key);
        if (*(tstr *)(entry + field->map_key_offset) == NULL)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying map key");
        if (field->map_value_kind == TBE_TYPED_OBJECT || field->map_value_kind == TBE_TYPED_BYTES) {
          status = typed_init_value(field->map_value_kind, field->map_value_type,
                                    entry + field->map_value_offset, 1, error);
          if (status != DATA_BIND_OK) return status;
        }
        status = typed_from_one(field->map_value_kind, field->map_value_wire_kind,
                                field->map_value_type, item.value,
                                entry + field->map_value_offset, field->name, error);
        if (status != DATA_BIND_OK) return status;
      }
    } else {
      status = typed_from_one(field->kind, field->wire_kind, field->object_type, child, out,
                              field->name, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  if (error != NULL && error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_from_value(const TbeTypedType *type, const DataBindValue *value,
                                    void *object, DataBindError *error) {
  void *temporary;
  DataBindStatus status;
  if (value == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed value");
  status = typed_validate_descriptor_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  temporary = calloc(1, type->size);
  if (temporary == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type->name,
                       "Out of memory creating typed conversion");
  status = tbe_typed_init(type, temporary, error);
  if (status == DATA_BIND_OK) status = typed_from_value_at(type, value, temporary, "", error);
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

static json_value_t *typed_scalar_json(TbeTypedKind kind, TbeTypedKind wire_kind,
                                       const void *ptr, const char *path,
                                       DataBindError *error) {
  if (kind == TBE_TYPED_BOOL) return json_create_bool(*(const uint8_t *)ptr != 0);
  if (kind == TBE_TYPED_STRING) {
    tstr text = *(const tstr *)ptr;
    size_t len = text ? tstr_len(text) : 0;
    if (!vstr_utf8_valid(vstr_from_buf(text ? text : "", len))) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Typed string is not valid UTF-8 JSON text");
      return NULL;
    }
    return json_create_string_n(text ? text : "", len);
  }
  if (kind == TBE_TYPED_UUID) {
    char text[SALTS_UUID_STRING_SIZE];
    if (salts_uuid_format((const salts_uuid_t *)ptr, text, sizeof(text)) != SALTS_OK) return NULL;
    return json_create_string(text);
  }
  if (kind == TBE_TYPED_F32) {
    float value = *(const float *)ptr;
    if (!isfinite(value)) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Typed float must be finite for JSON");
      return NULL;
    }
    return json_create_number(value);
  }
  if (kind == TBE_TYPED_F64) {
    double value = *(const double *)ptr;
    if (!isfinite(value)) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Typed double must be finite for JSON");
      return NULL;
    }
    return json_create_number(value);
  }
  if (kind == TBE_TYPED_ENUM) kind = typed_enum_storage_kind(wire_kind);
  switch (kind) {
  case TBE_TYPED_I8:
    return json_create_int64(*(const int8_t *)ptr);
  case TBE_TYPED_U8:
    return json_create_int64(*(const uint8_t *)ptr);
  case TBE_TYPED_I16:
    return json_create_int64(*(const int16_t *)ptr);
  case TBE_TYPED_U16:
    return json_create_int64(*(const uint16_t *)ptr);
  case TBE_TYPED_I32:
    return json_create_int64(*(const int32_t *)ptr);
  case TBE_TYPED_U32:
    return json_create_int64(*(const uint32_t *)ptr);
  case TBE_TYPED_I64:
    return json_create_int64(*(const int64_t *)ptr);
  case TBE_TYPED_U64:
    return json_create_uint64(*(const uint64_t *)ptr);
  default:
    return NULL;
  }
}

static json_value_t *typed_bytes_json(const uint8_t *data, size_t len, const char *path,
                                      DataBindError *error) {
  if (len != 0 && data == NULL) {
    typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path, "Typed byte storage is invalid");
    return NULL;
  }
  if (!vstr_utf8_valid(vstr_from_buf((const char *)data, len))) {
    typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                "Typed bytes are not valid UTF-8 JSON text");
    return NULL;
  }
  return json_create_string_n((const char *)data, len);
}

static json_value_t *typed_one_json(TbeTypedKind kind, TbeTypedKind wire_kind,
                                    const TbeTypedType *object_type, const void *ptr,
                                    const char *path, DataBindError *error) {
  if (kind == TBE_TYPED_OBJECT) return tbe_typed_to_json(object_type, ptr, error);
  if (kind == TBE_TYPED_BYTES) {
    const vec_t *vec = (const vec_t *)ptr;
    return typed_bytes_json((const uint8_t *)vec->data, vec->size, path, error);
  }
  return typed_scalar_json(kind, wire_kind, ptr, path, error);
}

json_value_t *tbe_typed_to_json(const TbeTypedType *type, const void *object,
                                DataBindError *error) {
  json_value_t *root;
  size_t i;
  if (type == NULL || object == NULL) {
    typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed object");
    return NULL;
  }
  if (typed_validate_descriptor_at(type, 0u, error) != DATA_BIND_OK) return NULL;
  root = json_create_object();
  if (root == NULL) {
    typed_error(error, DATA_BIND_ERR_OOM, type->name, "Out of memory creating JSON object");
    return NULL;
  }
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    json_value_t *child = NULL;
    size_t j;
    if (!typed_optional_present(type, object, field)) continue;
    if (field->kind == TBE_TYPED_BYTES || field->kind == TBE_TYPED_FIXED_BYTES) {
      const uint8_t *data;
      size_t len;
      if (field->kind == TBE_TYPED_BYTES) {
        const vec_t *vec = (const vec_t *)ptr;
        data = (const uint8_t *)vec->data;
        len = vec->size;
      } else {
        data = (const uint8_t *)ptr;
        len = field->fixed_count;
      }
      child = typed_bytes_json(data, len, field->name, error);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET) {
      const uint8_t *data;
      size_t count;
      child = json_create_array();
      if (field->kind == TBE_TYPED_FIXED_ARRAY) {
        data = (const uint8_t *)ptr;
        count = field->fixed_count;
      } else {
        const vec_t *vec = (const vec_t *)ptr;
        data = (const uint8_t *)vec->data;
        count = vec->size;
      }
      for (j = 0; child != NULL && j < count; ++j) {
        json_value_t *item =
            typed_one_json(field->element_kind, field->element_wire_kind, field->object_type,
                           data + j * field->element_size, field->name, error);
        if (item == NULL || !json_array_add_checked(child, item)) {
          (json_free(item), item = NULL);
          (json_free(child), child = NULL);
        }
      }
    } else if (field->kind == TBE_TYPED_MAP) {
      const vec_t *vec = (const vec_t *)ptr;
      child = json_create_object();
      for (j = 0; child != NULL && j < vec->size; ++j) {
        const uint8_t *entry = (const uint8_t *)vec->data + j * field->map_entry_size;
        tstr key = *(const tstr *)(entry + field->map_key_offset);
        size_t key_len = key ? tstr_len(key) : 0;
        json_value_t *item;
        if (!vstr_utf8_valid(vstr_from_buf(key ? key : "", key_len)) ||
            (key != NULL && memchr(key, '\0', key_len) != NULL)) {
          typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                      "Typed map key is not valid UTF-8 JSON text");
          (json_free(child), child = NULL);
          break;
        }
        item = typed_one_json(
            field->map_value_kind, field->map_value_wire_kind, field->map_value_type,
            entry + field->map_value_offset, field->name, error);
        if (item == NULL || !json_object_add_checked(child, key ? key : "", item)) {
          (json_free(item), item = NULL);
          (json_free(child), child = NULL);
        }
      }
    } else {
      child = typed_one_json(field->kind, field->wire_kind, field->object_type, ptr, field->name,
                             error);
    }
    if (child == NULL || !json_object_add_checked(root, field->name, child)) {
      (json_free(child), child = NULL);
      (json_free(root), root = NULL);
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field->name,
                  "Typed field cannot be represented as JSON");
      return NULL;
    }
  }
  if (error != NULL && error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  return root;
}

void tbe_typed_json_free(json_value_t **value) {
  if (!value) return;
  json_free(*value);
  *value = NULL;
}

static int typed_scalar_kind_from_name(const char *name, TbeTypedKind *kind) {
  if (name == NULL || kind == NULL) return 0;
  if (strcmp(name, "bool") == 0) *kind = TBE_TYPED_BOOL;
  else if (strcmp(name, "int8_t") == 0 || strcmp(name, "int8") == 0 || strcmp(name, "i8") == 0)
    *kind = TBE_TYPED_I8;
  else if (strcmp(name, "uint8_t") == 0 || strcmp(name, "uint8") == 0 || strcmp(name, "u8") == 0 ||
           strcmp(name, "byte") == 0)
    *kind = TBE_TYPED_U8;
  else if (strcmp(name, "int16_t") == 0 || strcmp(name, "int16") == 0 || strcmp(name, "i16") == 0)
    *kind = TBE_TYPED_I16;
  else if (strcmp(name, "uint16_t") == 0 || strcmp(name, "uint16") == 0 || strcmp(name, "u16") == 0)
    *kind = TBE_TYPED_U16;
  else if (strcmp(name, "int32_t") == 0 || strcmp(name, "int32") == 0 || strcmp(name, "i32") == 0)
    *kind = TBE_TYPED_I32;
  else if (strcmp(name, "uint32_t") == 0 || strcmp(name, "uint32") == 0 || strcmp(name, "u32") == 0)
    *kind = TBE_TYPED_U32;
  else if (strcmp(name, "int64_t") == 0 || strcmp(name, "int64") == 0 || strcmp(name, "i64") == 0)
    *kind = TBE_TYPED_I64;
  else if (strcmp(name, "uint64_t") == 0 || strcmp(name, "uint64") == 0 || strcmp(name, "u64") == 0)
    *kind = TBE_TYPED_U64;
  else if (strcmp(name, "float") == 0) *kind = TBE_TYPED_F32;
  else if (strcmp(name, "double") == 0) *kind = TBE_TYPED_F64;
  else if (strcmp(name, "string") == 0) *kind = TBE_TYPED_STRING;
  else if (strcmp(name, "bytes") == 0) *kind = TBE_TYPED_BYTES;
  else if (strcmp(name, "uuid") == 0) *kind = TBE_TYPED_UUID;
  else return 0;
  return 1;
}

static int typed_named_kind_matches(DataBind *codec, const char *name, TbeTypedKind kind,
                                    TbeTypedKind wire_kind, const TbeTypedType *object_type) {
  TbeTypedKind schema_kind;
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  if (kind == TBE_TYPED_OBJECT)
    return object_type != NULL && name != NULL && strcmp(object_type->name, name) == 0;
  if (kind != TBE_TYPED_ENUM)
    return typed_scalar_kind_from_name(name, &schema_kind) && schema_kind == kind;
  if (!data_bind_schema_find_type(codec, name, &schema_type) ||
      (schema_type.kind != DATA_BIND_SCHEMA_ENUM && schema_type.kind != DATA_BIND_SCHEMA_FLAGS) ||
      !typed_scalar_kind_from_name(schema_type.underlying_type, &schema_kind))
    return 0;
  return schema_kind == wire_kind;
}

static int typed_field_schema_matches(DataBind *codec, const TbeTypedField *field,
                                      const DataBindSchemaField *schema) {
  int descriptor_optional = (field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0;
  int descriptor_offset = (field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0;
  if (field->name == NULL || schema->name == NULL || strcmp(field->name, schema->name) != 0 ||
      descriptor_optional != (schema->is_optional != 0) ||
      descriptor_offset != (schema->has_offset != 0) ||
      (descriptor_offset && field->wire_offset != schema->offset))
    return 0;
  if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0)
    return schema->is_group && field->kind == TBE_TYPED_LIST && field->object_type != NULL &&
           schema->group_type != NULL && strcmp(field->object_type->name, schema->group_type) == 0;
  if (field->kind == TBE_TYPED_MAP)
    return schema->is_map && schema->key_type != NULL && strcmp(schema->key_type, "string") == 0 &&
           typed_named_kind_matches(codec, schema->value_type, field->map_value_kind,
                                    field->map_value_wire_kind, field->map_value_type);
  if (field->kind == TBE_TYPED_LIST || field->kind == TBE_TYPED_SET ||
      field->kind == TBE_TYPED_FIXED_ARRAY) {
    const char *expected_collection = field->kind == TBE_TYPED_FIXED_ARRAY
                                          ? "array"
                                          : (field->kind == TBE_TYPED_SET ? "set" : "list");
    return schema->is_collection && schema->inner_type != NULL &&
           (schema->collection_kind == NULL ||
            strcmp(schema->collection_kind, expected_collection) == 0) &&
           (field->kind != TBE_TYPED_FIXED_ARRAY || schema->is_fixed_size) &&
           typed_named_kind_matches(codec, schema->inner_type, field->element_kind,
                                    field->element_wire_kind, field->object_type);
  }
  if (field->kind == TBE_TYPED_FIXED_BYTES)
    return schema->type != NULL && strcmp(schema->type, "bytes") == 0 && schema->is_fixed_size &&
           schema->has_size_bytes && field->fixed_count == schema->size_bytes;
  return typed_named_kind_matches(codec, schema->type, field->kind, field->wire_kind,
                                  field->object_type);
}

static int typed_field_host_extent(const TbeTypedField *field, size_t *extent) {
  if (field == NULL || extent == NULL) return 0;
  switch (field->kind) {
  case TBE_TYPED_STRING:
    *extent = sizeof(tstr);
    return 1;
  case TBE_TYPED_BYTES:
  case TBE_TYPED_LIST:
  case TBE_TYPED_SET:
  case TBE_TYPED_MAP:
    *extent = sizeof(vec_t);
    return 1;
  case TBE_TYPED_FIXED_BYTES:
    *extent = field->fixed_count;
    return 1;
  case TBE_TYPED_OBJECT:
    if (field->object_type == NULL) return 0;
    *extent = field->object_type->size;
    return 1;
  case TBE_TYPED_FIXED_ARRAY:
    return field->element_size != 0 &&
           typed_multiply_fits(field->fixed_count, field->element_size, extent);
  case TBE_TYPED_ENUM:
    *extent = typed_kind_size(typed_enum_storage_kind(field->wire_kind));
    return *extent != 0;
  default:
    *extent = typed_kind_size(field->kind);
    return *extent != 0;
  }
}

static int typed_value_host_extent(TbeTypedKind kind, TbeTypedKind wire_kind,
                                   const TbeTypedType *object_type, size_t *extent) {
  if (extent == NULL) return 0;
  if (kind == TBE_TYPED_STRING) *extent = sizeof(tstr);
  else if (kind == TBE_TYPED_BYTES) *extent = sizeof(vec_t);
  else if (kind == TBE_TYPED_OBJECT && object_type != NULL) *extent = object_type->size;
  else if (kind == TBE_TYPED_ENUM)
    *extent = typed_kind_size(typed_enum_storage_kind(wire_kind));
  else
    *extent = typed_kind_size(kind);
  return *extent != 0;
}

static int typed_field_wire_extent(const TbeTypedField *field, size_t *extent) {
  size_t element_wire_size;
  if (field == NULL || extent == NULL) return 0;
  if (field->kind == TBE_TYPED_OBJECT) {
    if (field->object_type == NULL) return 0;
    *extent = field->object_type->fixed_block_size;
    return 1;
  }
  if (field->kind == TBE_TYPED_FIXED_BYTES) {
    *extent = field->fixed_count;
    return 1;
  }
  if (field->kind == TBE_TYPED_FIXED_ARRAY) {
    if (field->element_kind == TBE_TYPED_OBJECT) {
      if (field->object_type == NULL) return 0;
      element_wire_size = field->object_type->fixed_block_size;
    } else {
      element_wire_size = typed_kind_size(
          field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind : field->element_kind);
    }
    return element_wire_size != 0 &&
           typed_multiply_fits(field->fixed_count, element_wire_size, extent);
  }
  *extent = typed_kind_size(field->kind == TBE_TYPED_ENUM ? field->wire_kind : field->kind);
  return *extent != 0;
}

static int typed_kind_owns_storage(TbeTypedKind kind) {
  return kind == TBE_TYPED_STRING || kind == TBE_TYPED_BYTES || kind == TBE_TYPED_OBJECT ||
         kind == TBE_TYPED_LIST || kind == TBE_TYPED_SET || kind == TBE_TYPED_MAP;
}

static int typed_field_owns_storage(const TbeTypedField *field) {
  if (field == NULL) return 0;
  if (field->kind == TBE_TYPED_FIXED_ARRAY) return typed_kind_owns_storage(field->element_kind);
  return typed_kind_owns_storage(field->kind);
}

static int typed_type_has_tail(const TbeTypedType *type) {
  size_t i;
  if (type == NULL) return 0;
  for (i = 0; i < type->field_count; ++i)
    if ((type->fields[i].flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) != 0) return 1;
  return 0;
}

static DataBindStatus typed_validate_descriptor_at(const TbeTypedType *type, unsigned depth,
                                                   DataBindError *error) {
  size_t i;
  if (type == NULL || type->name == NULL || type->size == 0 ||
      (type->field_count != 0 && type->fields == NULL))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed descriptor");
  if (depth > 32u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed descriptor nesting exceeds the supported limit");
  if (!typed_size_fits(type->presence_offset, type->presence_size, type->size))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed presence bitmap exceeds the host object");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t host_extent;
    size_t j;
    const TbeTypedType *nested_type = NULL;
    if (field->name == NULL || !typed_field_host_extent(field, &host_extent) ||
        !typed_size_fits(field->offset, host_extent, type->size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field exceeds the host object");
    if ((field->flags & TBE_TYPED_FIELD_OPTIONAL) != 0 &&
        (type->presence_size == 0 || field->optional_bit / 8u >= type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed optional bit exceeds the presence bitmap");
    if (typed_field_owns_storage(field) && type->presence_size != 0 &&
        typed_ranges_overlap(field->offset, host_extent, type->presence_offset,
                             type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed owning field overlaps the presence bitmap");
    for (j = 0; j < i; ++j) {
      const TbeTypedField *previous = &type->fields[j];
      size_t previous_extent;
      if (!typed_field_owns_storage(field) && !typed_field_owns_storage(previous)) continue;
      if (!typed_field_host_extent(previous, &previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, previous->name,
                           "Typed field has invalid host storage");
      if (typed_ranges_overlap(field->offset, host_extent, previous->offset, previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed host field storage overlaps owning storage");
    }
    if (field->kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
               field->kind == TBE_TYPED_SET) {
      size_t element_extent;
      if (!typed_value_host_extent(field->element_kind, field->element_wire_kind,
                                   field->object_type, &element_extent) ||
          field->element_size < element_extent)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed collection element exceeds its host storage");
      if (field->kind == TBE_TYPED_FIXED_ARRAY &&
          !typed_multiply_fits(field->fixed_count, field->element_size, &host_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed collection size exceeds the host address space");
      if (field->element_kind == TBE_TYPED_OBJECT) nested_type = field->object_type;
    } else if (field->kind == TBE_TYPED_MAP) {
      size_t value_extent;
      if (field->map_entry_size == 0 || field->element_size != field->map_entry_size ||
          !typed_size_fits(field->map_key_offset, sizeof(tstr), field->map_entry_size) ||
          !typed_value_host_extent(field->map_value_kind, field->map_value_wire_kind,
                                   field->map_value_type, &value_extent) ||
          !typed_size_fits(field->map_value_offset, value_extent, field->map_entry_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed map entry exceeds its host storage");
      if (typed_ranges_overlap(field->map_key_offset, sizeof(tstr), field->map_value_offset,
                               value_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed map key and value storage overlap");
      if (field->map_value_kind == TBE_TYPED_OBJECT) nested_type = field->map_value_type;
    }
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0 &&
        (field->kind != TBE_TYPED_LIST || field->element_kind != TBE_TYPED_OBJECT ||
         field->object_type == NULL || field->element_size != field->object_type->size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed group descriptor is not an owning record vector");
    if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0 && field->kind != TBE_TYPED_STRING &&
        field->kind != TBE_TYPED_BYTES)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed variable data must be a string or byte vector");
    if (nested_type != NULL) {
      DataBindStatus status = typed_validate_descriptor_at(nested_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_validate_descriptor(const TbeTypedType *type, DataBindError *error) {
  return typed_validate_descriptor_at(type, 0u, error);
}

static DataBindStatus typed_descriptor_boundary(const TbeTypedDescriptor *descriptor,
                                                DataBindError *error) {
  const size_t required_size =
      offsetof(TbeTypedDescriptor, type) + sizeof(descriptor->type);
  if (descriptor == NULL || descriptor->struct_size < required_size ||
      descriptor->abi_version != TBE_TYPED_DESCRIPTOR_ABI_VERSION || descriptor->type == NULL) {
    return typed_error(error, DATA_BIND_ERR_SCHEMA, NULL,
                       "Invalid or incompatible typed descriptor boundary");
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_descriptor_validate(const TbeTypedDescriptor *descriptor,
                                             DataBindError *error) {
  DataBindStatus status = typed_descriptor_boundary(descriptor, error);
  if (status != DATA_BIND_OK) return status;
  return tbe_typed_validate_descriptor(descriptor->type, error);
}

static DataBindStatus typed_validate_layout_at(const TbeTypedType *type, unsigned depth,
                                               DataBindError *error) {
  size_t i;
  DataBindStatus status = typed_validate_descriptor_at(type, depth, error);
  if (status != DATA_BIND_OK) return status;
  if (type->presence_size > type->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed presence bitmap exceeds the fixed wire block");
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    size_t wire_extent;
    size_t j;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0) {
      if (!typed_field_wire_extent(field, &wire_extent) ||
          !typed_size_fits(field->wire_offset, wire_extent, type->fixed_block_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed field exceeds the fixed wire block");
      if (field->wire_size != wire_extent)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed declared wire size does not match the field layout");
      if (typed_ranges_overlap(field->wire_offset, wire_extent, 0u, type->presence_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed field overlaps the wire presence bitmap");
      for (j = 0; j < i; ++j) {
        const TbeTypedField *previous = &type->fields[j];
        size_t previous_extent;
        if ((previous->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
        if (!typed_field_wire_extent(previous, &previous_extent))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, previous->name,
                             "Typed field has invalid wire storage");
        if (typed_ranges_overlap(field->wire_offset, wire_extent, previous->wire_offset,
                                 previous_extent))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                             "Typed fixed wire fields overlap");
      }
    }
    if (field->kind == TBE_TYPED_OBJECT) {
      if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 ||
          typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Nested binary objects must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY && field->element_kind == TBE_TYPED_OBJECT) {
      if (typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Nested binary arrays must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      if (typed_type_has_tail(field->object_type))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed group entries must have a fixed wire layout");
      status = typed_validate_layout_at(field->object_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    } else if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
               (field->flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) == 0) {
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed binary field has no wire location");
    }
  }
  return DATA_BIND_OK;
}

static int typed_supports_direct_binary(const TbeTypedType *type) {
  size_t i;
  if (type == NULL) return 0;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    if (field->kind == TBE_TYPED_OBJECT ||
        (field->kind == TBE_TYPED_FIXED_ARRAY && field->element_kind == TBE_TYPED_OBJECT) ||
        (field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      if (!typed_supports_direct_binary(field->object_type)) return 0;
    }
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
        (field->flags & (TBE_TYPED_FIELD_GROUP | TBE_TYPED_FIELD_VAR_DATA)) == 0)
      return 0;
  }
  return 1;
}

static DataBindStatus typed_validate_schema_at(DataBind *codec, const char *type_name,
                                               const TbeTypedType *type, unsigned depth,
                                               DataBindError *error) {
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  DataBindStatus status;
  size_t i;
  if (codec == NULL || type_name == NULL || type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid typed schema validation arguments");
  if (depth > 32u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed schema nesting exceeds the supported limit");
  status = typed_validate_descriptor_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  if (type->name == NULL || strcmp(type->name, type_name) != 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed descriptor name does not match the requested type");
  if (!data_bind_schema_find_type(codec, type_name, &schema_type))
    return typed_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
                       "Typed schema type was not found");
  if ((schema_type.kind != DATA_BIND_SCHEMA_MESSAGE &&
       schema_type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
       schema_type.kind != DATA_BIND_SCHEMA_GROUP) ||
      schema_type.field_count != type->field_count ||
      (schema_type.has_fixed_block_size &&
       schema_type.fixed_block_size != type->fixed_block_size) ||
      (!schema_type.has_fixed_block_size && type->fixed_block_size != 0))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Typed descriptor does not match the schema record");
  if (depth == 0u) {
    const char *byte_order = data_bind_schema_attribute_get(codec, "byte_order");
    int schema_big_endian = byte_order != NULL && strcmp(byte_order, "big") == 0;
    if ((type->wire_big_endian != 0) != schema_big_endian)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Typed descriptor byte order does not match the schema");
  }
  for (i = 0; i < type->field_count; ++i) {
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    const TbeTypedField *field = &type->fields[i];
    const TbeTypedType *nested_type = NULL;
    const char *nested_name = NULL;
    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field) ||
        !typed_field_schema_matches(codec, field, &schema_field))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field descriptor does not match the schema");
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      nested_type = field->object_type;
      nested_name = schema_field.group_type;
    } else if (field->kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
      nested_name = schema_field.type;
    } else if ((field->kind == TBE_TYPED_FIXED_ARRAY || field->kind == TBE_TYPED_LIST ||
                field->kind == TBE_TYPED_SET) &&
               field->element_kind == TBE_TYPED_OBJECT) {
      nested_type = field->object_type;
      nested_name = schema_field.inner_type;
    } else if (field->kind == TBE_TYPED_MAP && field->map_value_kind == TBE_TYPED_OBJECT) {
      nested_type = field->map_value_type;
      nested_name = schema_field.value_type;
    }
    if (nested_type != NULL) {
      status = typed_validate_schema_at(codec, nested_name, nested_type, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  if (error != NULL && error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_validate_schema(DataBind *codec, const char *type_name,
                                         const TbeTypedType *type, DataBindError *error) {
  return typed_validate_schema_at(codec, type_name, type, 0u, error);
}

static void typed_read_wire_scalar(TbeTypedKind kind, const uint8_t *source, int big_endian,
                                   void *output) {
  switch (kind) {
  case TBE_TYPED_BOOL:
    *(uint8_t *)output = (uint8_t)(tbe_wire_read_u8(source, big_endian) != 0);
    break;
  case TBE_TYPED_I8:
    *(int8_t *)output = tbe_wire_read_i8(source, big_endian);
    break;
  case TBE_TYPED_U8:
    *(uint8_t *)output = tbe_wire_read_u8(source, big_endian);
    break;
  case TBE_TYPED_I16:
    *(int16_t *)output = tbe_wire_read_i16(source, big_endian);
    break;
  case TBE_TYPED_U16:
    *(uint16_t *)output = tbe_wire_read_u16(source, big_endian);
    break;
  case TBE_TYPED_I32:
    *(int32_t *)output = tbe_wire_read_i32(source, big_endian);
    break;
  case TBE_TYPED_U32:
    *(uint32_t *)output = tbe_wire_read_u32(source, big_endian);
    break;
  case TBE_TYPED_I64:
    *(int64_t *)output = tbe_wire_read_i64(source, big_endian);
    break;
  case TBE_TYPED_U64:
    *(uint64_t *)output = tbe_wire_read_u64(source, big_endian);
    break;
  case TBE_TYPED_F32:
    *(float *)output = tbe_wire_read_f32(source, big_endian);
    break;
  case TBE_TYPED_F64:
    *(double *)output = tbe_wire_read_f64(source, big_endian);
    break;
  case TBE_TYPED_UUID:
    memcpy(((salts_uuid_t *)output)->bytes, source, SALTS_UUID_SIZE);
    break;
  default:
    break;
  }
}

static void typed_read_enum(TbeTypedKind wire_kind, const uint8_t *source, int big_endian,
                            void *output) {
  typed_read_wire_scalar(typed_enum_storage_kind(wire_kind), source, big_endian, output);
}

static DataBindStatus typed_read_fixed(const TbeTypedType *type, const uint8_t *data, size_t len,
                                       void *object, DataBindError *error) {
  size_t i;
  if (len < type->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_PARSE, type->name,
                       "Binary input is shorter than the fixed block");
  if (type->presence_size != 0)
    memcpy((uint8_t *)object + type->presence_offset, data, type->presence_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    uint8_t *output = (uint8_t *)object + field->offset;
    const uint8_t *source;
    size_t j;
    size_t element_wire_size;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    source = data + field->wire_offset;
    if (!typed_optional_present(type, object, field)) continue;
    if (field->kind == TBE_TYPED_OBJECT) {
      DataBindStatus status =
          typed_read_fixed(field->object_type, source, len - field->wire_offset, output, error);
      if (status != DATA_BIND_OK) return status;
    } else if (field->kind == TBE_TYPED_FIXED_BYTES) {
      memcpy(output, source, field->fixed_count);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      element_wire_size =
          field->element_kind == TBE_TYPED_OBJECT
              ? field->object_type->fixed_block_size
              : typed_kind_size(field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind
                                                                      : field->element_kind);
      for (j = 0; j < field->fixed_count; ++j) {
        void *element = output + j * field->element_size;
        const uint8_t *element_source = source + j * element_wire_size;
        if (field->element_kind == TBE_TYPED_OBJECT) {
          DataBindStatus status = typed_read_fixed(field->object_type, element_source,
                                                   element_wire_size, element, error);
          if (status != DATA_BIND_OK) return status;
        } else if (field->element_kind == TBE_TYPED_ENUM) {
          typed_read_enum(field->element_wire_kind, element_source, type->wire_big_endian, element);
        } else {
          typed_read_wire_scalar(field->element_kind, element_source, type->wire_big_endian,
                                 element);
        }
      }
    } else if (field->kind == TBE_TYPED_ENUM) {
      typed_read_enum(field->wire_kind, source, type->wire_big_endian, output);
    } else {
      typed_read_wire_scalar(field->kind, source, type->wire_big_endian, output);
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_read_tail(const TbeTypedType *type, const uint8_t *data, size_t len,
                                      void *object, size_t *consumed, DataBindError *error) {
  size_t cursor = type->fixed_block_size;
  size_t i;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    uint8_t *output = (uint8_t *)object + field->offset;
    int present = typed_optional_present(type, object, field);
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      vec_t *vec = (vec_t *)output;
      uint16_t block_length;
      uint16_t count;
      size_t payload_size;
      size_t host_payload_size;
      size_t j;
      if (!typed_size_fits(cursor, 4u, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary group header is truncated");
      block_length = tbe_wire_read_u16(data + cursor, type->wire_big_endian);
      count = tbe_wire_read_u16(data + cursor + 2u, type->wire_big_endian);
      cursor += 4u;
      if (block_length < field->object_type->fixed_block_size ||
          !typed_multiply_fits(count, block_length, &payload_size) ||
          !typed_size_fits(cursor, payload_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary group payload is invalid");
      if (present) {
        if (!typed_multiply_fits(count, field->element_size, &host_payload_size))
          return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                             "Typed group size exceeds the host address space");
        if (vec_resize(vec, count) != STL_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory resizing typed group");
        memset(vec->data, 0, host_payload_size);
        for (j = 0; j < count; ++j) {
          void *element = (uint8_t *)vec->data + j * field->element_size;
          DataBindStatus status = tbe_typed_init(field->object_type, element, error);
          if (status == DATA_BIND_OK)
            status = typed_read_fixed(field->object_type, data + cursor + j * block_length,
                                      block_length, element, error);
          if (status != DATA_BIND_OK) return status;
        }
      }
      cursor += payload_size;
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      uint32_t value_size;
      if (!typed_size_fits(cursor, 4u, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary variable-data header is truncated");
      value_size = tbe_wire_read_u32(data + cursor, type->wire_big_endian);
      cursor += 4u;
      if (!typed_size_fits(cursor, value_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary variable-data payload is truncated");
      if (present && field->kind == TBE_TYPED_STRING) {
        *(tstr *)output = tstr_dup_len((const char *)data + cursor, value_size);
        if (*(tstr *)output == NULL)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying typed string");
      } else if (present) {
        vec_t *vec = (vec_t *)output;
        if (vec_resize(vec, value_size) != STL_OK)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying typed bytes");
        if (value_size != 0) memcpy(vec->data, data + cursor, value_size);
      }
      cursor += value_size;
    }
  }
  if (consumed != NULL) *consumed = cursor;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_parse_binary(const TbeTypedType *type, const void *data, size_t len,
                                      void *object, DataBindError *error) {
  void *temporary;
  size_t consumed = 0;
  DataBindStatus status;
  if (type == NULL || data == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary parse arguments");
  status = typed_validate_layout_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  temporary = calloc(1, type->size);
  if (temporary == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type->name, "Out of memory creating typed object");
  status = tbe_typed_init(type, temporary, error);
  if (status == DATA_BIND_OK)
    status = typed_read_fixed(type, (const uint8_t *)data, len, temporary, error);
  if (status == DATA_BIND_OK)
    status = typed_read_tail(type, (const uint8_t *)data, len, temporary, &consumed, error);
  if (status == DATA_BIND_OK && consumed != len)
    status = typed_error(error, DATA_BIND_ERR_PARSE, type->name,
                         "Binary input contains trailing bytes");
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
    if (error != NULL && error->size >= offsetof(DataBindError, code) + sizeof(error->code))
      error->code = DATA_BIND_OK;
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

DataBindStatus tbe_typed_parse_ex(DataBind *codec, const char *type_name, const TbeTypedType *type,
                                  DataBindFormat format, const void *data, size_t len, size_t row,
                                  void *object, DataBindError *error) {
  DataBindValue *value = NULL;
  void *temporary;
  DataBindStatus status;
  if (codec == NULL || type_name == NULL || type == NULL || data == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed parse arguments");
  if (format == DATA_BIND_FORMAT_BINARY) {
    status = tbe_typed_validate_schema(codec, type_name, type, error);
    if (status != DATA_BIND_OK) return status;
    if (typed_supports_direct_binary(type))
      return tbe_typed_parse_binary(type, data, len, object, error);
    status = data_bind_parse(codec, type_name, (const uint8_t *)data, len, &value, error);
  } else if (format == DATA_BIND_FORMAT_JSON)
    status = data_bind_parse_json(codec, type_name, (const char *)data, len, &value, error);
  else if (format == DATA_BIND_FORMAT_YAML)
    status = data_bind_parse_yaml(codec, type_name, (const char *)data, len, &value, error);
  else if (format == DATA_BIND_FORMAT_CSV)
    status = data_bind_parse_csv(codec, type_name, (const char *)data, len, row, &value, error);
  else if (format == DATA_BIND_FORMAT_XML)
    status = data_bind_parse_xml(codec, type_name, (const char *)data, len, &value, error);
  else
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Unknown typed input format");
  if (status != DATA_BIND_OK) return status;
  temporary = calloc(1, type->size);
  if (temporary == NULL) {
    data_bind_value_free(value);
    return typed_error(error, DATA_BIND_ERR_OOM, type_name, "Out of memory creating typed object");
  }
  status = tbe_typed_init(type, temporary, error);
  if (status == DATA_BIND_OK) status = typed_from_value_at(type, value, temporary, "", error);
  data_bind_value_free(value);
  if (status == DATA_BIND_OK) {
    tbe_typed_clear(type, object);
    memcpy(object, temporary, type->size);
    memset(temporary, 0, type->size);
  }
  tbe_typed_clear(type, temporary);
  free(temporary);
  return status;
}

static int typed_format_from_string(const char *format, DataBindFormat *out_format) {
  if (format == NULL || out_format == NULL) return 0;
  if (strcmp(format, "bin") == 0) *out_format = DATA_BIND_FORMAT_BINARY;
  else if (strcmp(format, "json") == 0) *out_format = DATA_BIND_FORMAT_JSON;
  else if (strcmp(format, "yaml") == 0) *out_format = DATA_BIND_FORMAT_YAML;
  else if (strcmp(format, "csv") == 0) *out_format = DATA_BIND_FORMAT_CSV;
  else if (strcmp(format, "xml") == 0) *out_format = DATA_BIND_FORMAT_XML;
  else return 0;
  return 1;
}

DataBindStatus tbe_typed_parse(DataBind *codec, const char *type_name, const TbeTypedType *type,
                               const char *format, const void *data, size_t len, size_t row,
                               void *object, DataBindError *error) {
  DataBindFormat parsed_format;
  if (!typed_format_from_string(format, &parsed_format))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, format, "Unknown typed input format");
  return tbe_typed_parse_ex(codec, type_name, type, parsed_format, data, len, row, object, error);
}

DataBindStatus tbe_typed_descriptor_parse(DataBind *codec, const char *type_name,
                                          const TbeTypedDescriptor *descriptor,
                                          DataBindFormat format, const void *data, size_t len,
                                          size_t row, void *object, DataBindError *error) {
  DataBindStatus status = typed_descriptor_boundary(descriptor, error);
  if (status != DATA_BIND_OK) return status;
  return tbe_typed_parse_ex(codec, type_name, descriptor->type, format, data, len, row, object,
                            error);
}

DataBindStatus tbe_typed_serialize_ex(DataBind *codec, const char *type_name,
                                      const TbeTypedType *type, const void *object,
                                      DataBindFormat format, char **out, size_t *out_len,
                                      DataBindError *error) {
  json_value_t *json = NULL;
  DataBindObject *bound = NULL;
  DataBindStatus status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0;
  if (codec == NULL || type_name == NULL || type == NULL || object == NULL || out == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Invalid typed serialize arguments");
  if (format != DATA_BIND_FORMAT_JSON && format != DATA_BIND_FORMAT_YAML &&
      format != DATA_BIND_FORMAT_CSV && format != DATA_BIND_FORMAT_XML)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL, "Unknown typed output format");
  json = tbe_typed_to_json(type, object, error);
  if (json == NULL) {
    if (error != NULL &&
        error->size >= offsetof(DataBindError, code) + sizeof(error->code) &&
        error->code != DATA_BIND_OK)
      return error->code;
    return DATA_BIND_ERR_TYPE_MISMATCH;
  }
  status = data_bind_object_from_json_value(codec, type_name, json, &bound, error);
  (json_free(json), json = NULL);
  if (status != DATA_BIND_OK) return status;
  if (format == DATA_BIND_FORMAT_JSON)
    status = data_bind_object_serialize_json(codec, bound, out, out_len, error);
  else if (format == DATA_BIND_FORMAT_YAML)
    status = data_bind_object_serialize_yaml(codec, bound, out, out_len, error);
  else if (format == DATA_BIND_FORMAT_CSV)
    status = data_bind_object_serialize_csv(codec, bound, out, out_len, error);
  else if (format == DATA_BIND_FORMAT_XML)
    status = data_bind_object_serialize_xml(codec, bound, out, out_len, error);
  data_bind_object_free(bound);
  return status;
}

DataBindStatus tbe_typed_serialize(DataBind *codec, const char *type_name, const TbeTypedType *type,
                                   const void *object, const char *format, char **out,
                                   size_t *out_len, DataBindError *error) {
  DataBindFormat parsed_format;
  if (!typed_format_from_string(format, &parsed_format) ||
      parsed_format == DATA_BIND_FORMAT_BINARY)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, format, "Unknown typed output format");
  return tbe_typed_serialize_ex(codec, type_name, type, object, parsed_format, out, out_len, error);
}

DataBindStatus tbe_typed_descriptor_serialize(DataBind *codec, const char *type_name,
                                              const TbeTypedDescriptor *descriptor,
                                              const void *object, DataBindFormat format, char **out,
                                              size_t *out_len, DataBindError *error) {
  DataBindStatus status = typed_descriptor_boundary(descriptor, error);
  if (status != DATA_BIND_OK) return status;
  return tbe_typed_serialize_ex(codec, type_name, descriptor->type, object, format, out, out_len,
                                error);
}

static size_t typed_binary_size(const TbeTypedType *type, const void *object, int *supported) {
  size_t total = type->fixed_block_size;
  size_t i;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      const vec_t *vec = (const vec_t *)ptr;
      size_t count = typed_optional_present(type, object, field) ? vec->size : 0u;
      size_t payload_size;
      size_t field_size;
      if (field->object_type == NULL || field->object_type->fixed_block_size > UINT16_MAX ||
          count > UINT16_MAX || (count != 0 && vec->data == NULL)) {
        *supported = 0;
        return 0;
      }
      if (!typed_multiply_fits(count, field->object_type->fixed_block_size, &payload_size) ||
          !typed_add_fits(sizeof(uint16_t) * 2u, payload_size, &field_size) ||
          !typed_add_fits(total, field_size, &total)) {
        *supported = 0;
        return 0;
      }
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      size_t len = 0u;
      size_t field_size;
      if (typed_optional_present(type, object, field)) {
        len = field->kind == TBE_TYPED_STRING
                  ? (*(const tstr *)ptr ? tstr_len(*(const tstr *)ptr) : 0)
                  : ((const vec_t *)ptr)->size;
      }
      if ((field->kind == TBE_TYPED_BYTES && len != 0 &&
           ((const vec_t *)ptr)->data == NULL) ||
          len > UINT32_MAX || !typed_add_fits(sizeof(uint32_t), len, &field_size) ||
          !typed_add_fits(total, field_size, &total)) {
        *supported = 0;
        return 0;
      }
    } else if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0 &&
               field->kind != TBE_TYPED_OBJECT) {
      *supported = 0;
      return 0;
    }
  }
  return total;
}

static void typed_write_scalar(TbeTypedKind kind, uint8_t *dst, int big_endian, const void *src) {
  switch (kind) {
  case TBE_TYPED_BOOL:
  case TBE_TYPED_U8:
    tbe_wire_write_u8(dst, big_endian, *(const uint8_t *)src);
    break;
  case TBE_TYPED_I8:
    tbe_wire_write_i8(dst, big_endian, *(const int8_t *)src);
    break;
  case TBE_TYPED_U16:
    tbe_wire_write_u16(dst, big_endian, *(const uint16_t *)src);
    break;
  case TBE_TYPED_I16:
    tbe_wire_write_i16(dst, big_endian, *(const int16_t *)src);
    break;
  case TBE_TYPED_U32:
    tbe_wire_write_u32(dst, big_endian, *(const uint32_t *)src);
    break;
  case TBE_TYPED_I32:
  case TBE_TYPED_ENUM:
    tbe_wire_write_i32(dst, big_endian, *(const int32_t *)src);
    break;
  case TBE_TYPED_U64:
    tbe_wire_write_u64(dst, big_endian, *(const uint64_t *)src);
    break;
  case TBE_TYPED_I64:
    tbe_wire_write_i64(dst, big_endian, *(const int64_t *)src);
    break;
  case TBE_TYPED_F32:
    tbe_wire_write_f32(dst, big_endian, *(const float *)src);
    break;
  case TBE_TYPED_F64:
    tbe_wire_write_f64(dst, big_endian, *(const double *)src);
    break;
  case TBE_TYPED_UUID:
    memcpy(dst, ((const salts_uuid_t *)src)->bytes, SALTS_UUID_SIZE);
    break;
  default:
    break;
  }
}

static void typed_write_enum(TbeTypedKind wire_kind, uint8_t *dst, int big_endian,
                             const void *src) {
  typed_write_scalar(typed_enum_storage_kind(wire_kind), dst, big_endian, src);
}

static int typed_write_fixed(const TbeTypedType *type, const void *object, uint8_t *dst,
                             size_t size) {
  size_t i;
  if (size < type->fixed_block_size) return 0;
  if (type->presence_size != 0)
    memcpy(dst, (const uint8_t *)object + type->presence_offset, type->presence_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const uint8_t *src = (const uint8_t *)object + field->offset;
    size_t j;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    if (!typed_optional_present(type, object, field)) continue;
    if (field->kind == TBE_TYPED_OBJECT) {
      if (!typed_write_fixed(field->object_type, src, dst + field->wire_offset,
                             size - field->wire_offset))
        return 0;
    } else if (field->kind == TBE_TYPED_FIXED_BYTES) {
      memcpy(dst + field->wire_offset, src, field->fixed_count);
    } else if (field->kind == TBE_TYPED_FIXED_ARRAY) {
      for (j = 0; j < field->fixed_count; ++j) {
        if (field->element_kind == TBE_TYPED_OBJECT) {
          if (!typed_write_fixed(
                  field->object_type, src + j * field->element_size,
                  dst + field->wire_offset + j * field->object_type->fixed_block_size,
                  size - field->wire_offset - j * field->object_type->fixed_block_size))
            return 0;
        } else {
          TbeTypedKind wire_kind = field->element_kind == TBE_TYPED_ENUM ? field->element_wire_kind
                                                                         : field->element_kind;
          uint8_t *element_dst = dst + field->wire_offset + j * typed_kind_size(wire_kind);
          const void *element_src = src + j * field->element_size;
          if (field->element_kind == TBE_TYPED_ENUM)
            typed_write_enum(wire_kind, element_dst, type->wire_big_endian, element_src);
          else typed_write_scalar(wire_kind, element_dst, type->wire_big_endian, element_src);
        }
      }
    } else {
      if (field->kind == TBE_TYPED_ENUM)
        typed_write_enum(field->wire_kind, dst + field->wire_offset, type->wire_big_endian, src);
      else typed_write_scalar(field->kind, dst + field->wire_offset, type->wire_big_endian, src);
    }
  }
  return 1;
}

DataBindStatus tbe_typed_serialize_binary_into(const TbeTypedType *type, const void *object,
                                               uint8_t *output, size_t capacity, size_t *out_len,
                                               DataBindError *error) {
  size_t total;
  size_t cursor;
  size_t i;
  int supported = 1;
  if (out_len != NULL) *out_len = 0;
  if (type == NULL || object == NULL || out_len == NULL || (output == NULL && capacity != 0))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary output arguments");
  {
    DataBindStatus layout_status = typed_validate_layout_at(type, 0u, error);
    if (layout_status != DATA_BIND_OK) return layout_status;
  }
  total = typed_binary_size(type, object, &supported);
  if (!supported || total == 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Schema does not define a supported binary layout");
  *out_len = total;
  if (capacity < total)
    return typed_error(error, DATA_BIND_ERR_BUFFER_TOO_SMALL, type->name,
                       "Binary output buffer is too small");
  memset(output, 0, total);
  if (!typed_write_fixed(type, object, output, total)) {
    return typed_error(error, DATA_BIND_ERR_RUNTIME, type->name,
                       "Failed to write fixed binary fields");
  }
  cursor = type->fixed_block_size;
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const void *ptr = (const uint8_t *)object + field->offset;
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      const vec_t *vec = (const vec_t *)ptr;
      size_t count = typed_optional_present(type, object, field) ? vec->size : 0u;
      size_t j;
      tbe_wire_write_u16(output + cursor, type->wire_big_endian,
                         (uint16_t)field->object_type->fixed_block_size);
      tbe_wire_write_u16(output + cursor + 2u, type->wire_big_endian, (uint16_t)count);
      cursor += 4u;
      for (j = 0; j < count; ++j) {
        if (!typed_write_fixed(field->object_type,
                               (const uint8_t *)vec->data + j * field->element_size,
                               output + cursor, total - cursor)) {
          return typed_error(error, DATA_BIND_ERR_RUNTIME, field->name,
                             "Failed to write binary group");
        }
        cursor += field->object_type->fixed_block_size;
      }
    } else if ((field->flags & TBE_TYPED_FIELD_VAR_DATA) != 0) {
      const void *bytes;
      size_t len;
      if (!typed_optional_present(type, object, field)) {
        bytes = NULL;
        len = 0u;
      } else if (field->kind == TBE_TYPED_STRING) {
        tstr text = *(const tstr *)ptr;
        bytes = text ? text : "";
        len = text ? tstr_len(text) : 0;
      } else {
        const vec_t *vec = (const vec_t *)ptr;
        bytes = vec->data;
        len = vec->size;
      }
      tbe_wire_write_u32(output + cursor, type->wire_big_endian, (uint32_t)len);
      if (len != 0) memcpy(output + cursor + 4u, bytes, len);
      cursor += 4u + len;
    }
  }
  if (error != NULL && error->size >= offsetof(DataBindError, code) + sizeof(error->code))
    error->code = DATA_BIND_OK;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_serialize_binary(const TbeTypedType *type, const void *object,
                                          uint8_t **out, size_t *out_len, DataBindError *error) {
  uint8_t *data;
  size_t total;
  int supported = 1;
  DataBindStatus status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0;
  if (type == NULL || object == NULL || out == NULL || out_len == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, NULL,
                       "Invalid typed binary serialize arguments");
  status = typed_validate_layout_at(type, 0u, error);
  if (status != DATA_BIND_OK) return status;
  total = typed_binary_size(type, object, &supported);
  if (!supported || total == 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Schema does not define a supported binary layout");
  data = (uint8_t *)malloc(total);
  if (data == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type->name,
                       "Out of memory serializing binary object");
  status = tbe_typed_serialize_binary_into(type, object, data, total, out_len, error);
  if (status != DATA_BIND_OK) {
    free(data);
    return status;
  }
  *out = data;
  return DATA_BIND_OK;
}

void tbe_typed_serialized_free(void *data) { free(data); }
