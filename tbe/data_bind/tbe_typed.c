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
