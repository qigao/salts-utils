#include "tbe_typed_internal.h"

#include "data_bind_internal.h"
#include "fmt.h"
#include "tbe_wire.h"
#include <csv_parser.h>
#include <cyaml.h>
#include <cyaml_json_adapter.h>
#include <json_parser.h>
#include <salts_cmeta_data.h>
#include <tstr.h>
#include <xml_parser/xml_parser.h>

#include <ctype.h>
#include <errno.h>
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

static json_value_t *typed_json_created(json_value_t *value, const char *path,
                                        DataBindError *error) {
  if (value == NULL)
    typed_error(error, DATA_BIND_ERR_OOM, path,
                "Out of memory creating canonical JSON value");
  return value;
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
  return typed_json_created(json_create_string_n((const char *)data, len),
                            path, error);
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

enum { TBE_TYPED_NATIVE_MAX_DEPTH = 32u };

typedef struct TypedNativeRecord {
  const cmeta_data_desc *data;
  const cmeta_data_struct_shape *shape;
  const TbeTypedType *overlay;
} TypedNativeRecord;

static int typed_nonempty(const char *text) {
  return text != NULL && text[0] != '\0';
}

static int typed_native_scalar_supported(const cmeta_data_desc *data) {
  size_t i;
  for (i = 1u;
       i < sizeof(typed_cmeta_kind_mappings) / sizeof(typed_cmeta_kind_mappings[0]);
       ++i) {
    if (typed_cmeta_scalar_matches(data, typed_cmeta_kind_mappings[i].data))
      return 1;
  }
  if (data != NULL && data->kind == CMETA_DATA_ENUM)
    return cmeta_data_enum_bits_ops_of(data) != NULL;
  if (data == NULL || cmeta_data_fixed_ops_of(data) == NULL)
    return 0;
  return typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) ||
         salts_uuid_cmeta_data_valid(data) || data->kind == CMETA_DATA_BYTES;
}

static DataBindStatus typed_native_path(char *out, size_t capacity,
                                        const char *parent, const char *field,
                                        DataBindError *error) {
  int written;
  if (out == NULL || capacity == 0u || !typed_nonempty(field))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, parent,
                       "Native CMeta field name is unavailable");
  written = typed_nonempty(parent)
                ? snprintf(out, capacity, "%s.%s", parent, field)
                : snprintf(out, capacity, "%s", field);
  if (written < 0 || (size_t)written >= capacity)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, parent,
                       "Native CMeta field path exceeds the supported length");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_record_preflight(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const cmeta_data_desc **ancestors, unsigned depth, const char *path,
    TypedNativeRecord *out, DataBindError *error) {
  const size_t required_data_size =
      offsetof(cmeta_data_desc, shape) + sizeof(data->shape);
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  size_t i;

  if (data == NULL || overlay == NULL || data->struct_size < required_data_size ||
      data->abi_version != CMETA_DATA_DESC_ABI_VERSION ||
      data->kind != CMETA_DATA_STRUCT)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record is unavailable");
  if (depth > TBE_TYPED_NATIVE_MAX_DEPTH)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record depth exceeds 32");
  for (i = 0u; i < depth; ++i) {
    if (ancestors[i] == data)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Canonical native CMeta record graph contains a cycle");
  }
  if (data->storage_type == NULL || !cmeta_type_desc_valid(data->storage_type) ||
      data->storage_type->kind != CMETA_T_OBJECT || data->shape == NULL ||
      data->storage_type->align > _Alignof(max_align_t))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record storage is invalid");

  shape = (const cmeta_data_struct_shape *)data->shape;
  layout = shape->layout;
  if (layout == NULL || !typed_nonempty(layout->name) || layout->size == 0u ||
      layout->align == 0u || layout->size != data->storage_type->size ||
      layout->align != data->storage_type->align)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record layout is invalid");
  if (shape->field_count != layout->field_count ||
      shape->field_count != overlay->field_count ||
      (shape->field_count != 0u &&
       (shape->fields == NULL || layout->fields == NULL || overlay->fields == NULL)) ||
      overlay->presence_size != 0u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "CMeta native fields disagree with the schema overlay");

  ancestors[depth] = data;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const cmeta_field_desc *layout_field = &layout->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    const cmeta_data_desc *value = native_field->value;
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status =
        typed_native_path(field_path, sizeof(field_path), path,
                          native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (!typed_nonempty(native_field->stable_id) ||
        !typed_nonempty(layout_field->name) ||
        strcmp(layout_field->name, native_field->name) != 0 || value == NULL ||
        value->storage_type == NULL || !cmeta_type_desc_valid(value->storage_type) ||
        layout_field->type == NULL ||
        !cmeta_type_equal(layout_field->type, value->storage_type) ||
        layout_field->size != value->storage_type->size ||
        layout_field->align != value->storage_type->align ||
        layout_field->offset != native_field->offset ||
        !typed_size_fits(native_field->offset, layout_field->size,
                         data->storage_type->size) ||
        layout_field->align == 0u ||
        native_field->offset % layout_field->align != 0u ||
        (wire_field->flags &
         (TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_GROUP)) != 0u)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Field has no exact canonical native CMeta storage");

    if (value->kind == CMETA_DATA_STRUCT) {
      if (wire_field->nested_overlay == NULL)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Nested Struct has no schema overlay association");
      status = typed_native_record_preflight(
          value, wire_field->nested_overlay, ancestors, depth + 1u,
          field_path, NULL, error);
      if (status != DATA_BIND_OK) return status;
    } else {
      size_t fixed_extent;
      if (!cmeta_data_desc_valid(value) || !typed_native_scalar_supported(value))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Native CMeta field kind is outside this runtime slice");
      if (cmeta_data_fixed_ops_of(value) != NULL &&
          (cmeta_data_fixed_extent(value, &fixed_extent) != CMETA_OK ||
           fixed_extent != value->storage_type->size ||
           layout_field->size != fixed_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Fixed provider extent disagrees with native storage");
    }
  }
  if (!cmeta_data_desc_valid(data))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record descriptor is invalid");
  if (out != NULL) {
    out->data = data;
    out->shape = shape;
    out->overlay = overlay;
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_descriptor_native_record(
    const TbeTypedDescriptor *descriptor, TypedNativeRecord *out,
    DataBindError *error) {
  const size_t required_size =
      offsetof(TbeTypedDescriptor, native_data) + sizeof(descriptor->native_data);
  const cmeta_data_desc *ancestors[TBE_TYPED_NATIVE_MAX_DEPTH + 1u];
  const char *path;
  if (descriptor == NULL || descriptor->struct_size < required_size ||
      descriptor->abi_version != TBE_TYPED_DESCRIPTOR_ABI_VERSION ||
      descriptor->overlay == NULL || descriptor->native_data == NULL)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, NULL,
                       "Invalid or incompatible typed descriptor boundary");
  path = typed_nonempty(descriptor->overlay->name)
             ? descriptor->overlay->name
             : descriptor->native_data->display_name;
  return typed_native_record_preflight(descriptor->native_data,
                                       descriptor->overlay, ancestors, 0u,
                                       path, out, error);
}

static DataBindStatus typed_native_init_value(const cmeta_data_desc *data,
                                              void *storage, const char *path,
                                              DataBindError *error) {
  if (data == NULL || storage == NULL || data->storage_type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                       "Invalid canonical native storage");
  if (cmeta_data_fixed_ops_of(data) != NULL) {
    if (cmeta_data_fixed_restore_zero(data, storage) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed provider did not establish semantic zero");
    return DATA_BIND_OK;
  }
  memset(storage, 0, data->storage_type->size);
  if (data->kind == CMETA_DATA_ENUM) {
    if (cmeta_data_enum_bits_restore_zero(data, storage) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Enum provider did not establish semantic zero");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)data->shape;
    size_t i;
    for (i = 0u; i < shape->field_count; ++i) {
      char field_path[sizeof(((DataBindError *)0)->path)];
      DataBindStatus status = typed_native_path(
          field_path, sizeof(field_path), path, shape->fields[i].name, error);
      if (status == DATA_BIND_OK)
        status = typed_native_init_value(
            shape->fields[i].value,
            (uint8_t *)storage + shape->fields[i].offset,
            field_path, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_clear_value(const cmeta_data_desc *data,
                                               void *storage, const char *path,
                                               DataBindError *error) {
  if (data == NULL || storage == NULL || data->storage_type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                       "Invalid canonical native storage");
  if (cmeta_data_fixed_ops_of(data) != NULL) {
    if (cmeta_data_fixed_restore_zero(data, storage) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed provider did not restore semantic zero");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    if (cmeta_data_enum_bits_restore_zero(data, storage) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Enum provider did not restore semantic zero");
  } else if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)data->shape;
    size_t i;
    for (i = 0u; i < shape->field_count; ++i) {
      char field_path[sizeof(((DataBindError *)0)->path)];
      DataBindStatus status = typed_native_path(
          field_path, sizeof(field_path), path, shape->fields[i].name, error);
      if (status == DATA_BIND_OK)
        status = typed_native_clear_value(
            shape->fields[i].value,
            (uint8_t *)storage + shape->fields[i].offset,
            field_path, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  memset(storage, 0, data->storage_type->size);
  return DATA_BIND_OK;
}

/* Numeric wire/text adaptation only; native storage and membership belong to
 * the canonical provider. Negative canonical bits are width-local. */
static DataBindStatus typed_native_enum_assign_number(
    const cmeta_data_desc *data, int input_signed, int64_t signed_value,
    uint64_t unsigned_value, void *storage, const char *path,
    DataBindError *error) {
  const cmeta_enum_domain *domain = cmeta_data_enum_bits_ops_of(data)->domain;
  uint64_t mask = UINT64_MAX >> (64u - domain->bits);
  uint64_t bits;
  if (domain->signedness == CMETA_ENUM_SIGNED) {
    int64_t maximum = (int64_t)(mask >> 1u);
    if (input_signed) {
      if (signed_value < -maximum - 1 || signed_value > maximum) goto range;
      bits = (uint64_t)signed_value & mask;
    } else {
      if (unsigned_value > (uint64_t)maximum) goto range;
      bits = unsigned_value;
    }
  } else {
    if (input_signed && signed_value < 0) goto range;
    bits = input_signed ? (uint64_t)signed_value : unsigned_value;
    if (bits > mask) goto range;
  }
  if (cmeta_data_enum_assign_bits(data, storage, bits) == CMETA_OK)
    return DATA_BIND_OK;
range:
  return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                     "Enum value is outside the canonical CMeta domain");
}

static int64_t typed_native_enum_signed_value(const cmeta_enum_domain *domain,
                                              uint64_t bits) {
  uint64_t mask = UINT64_MAX >> (64u - domain->bits);
  if ((bits & (UINT64_C(1) << (domain->bits - 1u))) != 0u)
    return -1 - (int64_t)(mask - bits);
  return (int64_t)bits;
}

static const char *typed_json_numeric_text(const json_value_t *value,
                                           size_t *out_length) {
  const char *text = NULL;
  size_t length = 0u;
  if (value == NULL || out_length == NULL) return NULL;
  if (json_type(value) == JSON_NUMBER) {
    text = json_number_text(value, &length);
  } else if (json_type(value) == JSON_STRING) {
    text = json_string(value);
    length = json_string_len(value);
  } else {
    return NULL;
  }
  if (text == NULL || length == SIZE_MAX) return NULL;
  *out_length = length;
  return text;
}

static int typed_json_read_i64(const json_value_t *value, int64_t *out) {
  const char *text;
  uint64_t magnitude;
  int negative;
  size_t length = 0u;
  if (out == NULL) return 0;
  text = typed_json_numeric_text(value, &length);
  if (text == NULL ||
      !data_bind_internal_parse_integer_magnitude(
          text, length, (uint64_t)INT64_MAX + 1u, 1, &magnitude, &negative))
    return 0;
  if (!negative && magnitude > (uint64_t)INT64_MAX) return 0;
  *out = negative ? (magnitude == (uint64_t)INT64_MAX + 1u
                         ? INT64_MIN
                         : -(int64_t)magnitude)
                  : (int64_t)magnitude;
  return 1;
}

static int typed_json_read_u64(const json_value_t *value, uint64_t *out) {
  const char *text;
  int negative;
  size_t length = 0u;
  if (out == NULL) return 0;
  text = typed_json_numeric_text(value, &length);
  if (text == NULL ||
      !data_bind_internal_parse_integer_magnitude(
          text, length, UINT64_MAX, 0, out, &negative))
    return 0;
  return !negative;
}

static int typed_json_read_f64(const json_value_t *value, double *out) {
  const char *text;
  char *end;
  double parsed;
  size_t length = 0u;
  if (out == NULL) return 0;
  if (json_type(value) == JSON_BOOL) {
    *out = json_bool(value) ? 1.0 : 0.0;
    return 1;
  }
  text = typed_json_numeric_text(value, &length);
  if (text == NULL || length == 0u) return 0;
  errno = 0;
  parsed = strtod(text, &end);
  if (errno == ERANGE || end != text + length || !isfinite(parsed))
    return 0;
  *out = parsed;
  return 1;
}

static int typed_enum_named_bits(const cmeta_enum_domain *domain,
                                 const char *text, size_t length,
                                 uint64_t *out) {
  size_t i;
  if (domain == NULL || text == NULL || out == NULL) return 0;
  for (i = 0u; i < domain->count; ++i) {
    const cmeta_enum_bits_item *item = &domain->items[i];
    if ((strlen(item->symbol) == length &&
         memcmp(item->symbol, text, length) == 0) ||
        (strlen(item->text) == length &&
         memcmp(item->text, text, length) == 0)) {
      *out = item->bits;
      return 1;
    }
  }
  return 0;
}

static int typed_enum_flags_text(const cmeta_enum_domain *domain,
                                 const char *text, size_t length,
                                 uint64_t mask, uint64_t *out) {
  size_t position = 0u;
  uint64_t accumulated = 0u;
  int any = 0;
  while (position < length) {
    size_t start;
    uint64_t bits;
    while (position < length &&
           (text[position] == ' ' || text[position] == '\t' ||
            text[position] == '|' || text[position] == ',' ||
            text[position] == '+'))
      ++position;
    if (position == length) break;
    start = position;
    while (position < length && text[position] != ' ' &&
           text[position] != '\t' && text[position] != '|' &&
           text[position] != ',' && text[position] != '+')
      ++position;
    if (!typed_enum_named_bits(domain, text + start, position - start,
                               &bits)) {
      uint64_t magnitude;
      uint64_t maximum = domain->signedness == CMETA_ENUM_SIGNED
                             ? (mask >> 1u) + 1u
                             : mask;
      int negative;
      if (!data_bind_internal_parse_integer_magnitude(
              text + start, position - start, maximum,
              domain->signedness == CMETA_ENUM_SIGNED, &magnitude,
              &negative))
        return 0;
      if (!negative && domain->signedness == CMETA_ENUM_SIGNED &&
          magnitude > (mask >> 1u))
        return 0;
      bits = negative ? (~magnitude + 1u) & mask : magnitude;
    }
    accumulated |= bits;
    any = 1;
  }
  if (!any || accumulated > mask) return 0;
  *out = accumulated;
  return 1;
}

static int typed_json_enum_bits(const cmeta_enum_domain *domain,
                                const json_value_t *value, uint64_t *out) {
  uint64_t mask;
  size_t i;
  if (domain == NULL || value == NULL || out == NULL) return 0;
  mask = UINT64_MAX >> (64u - domain->bits);
  if (json_type(value) == JSON_STRING) {
    const char *text = json_string(value);
    size_t length = json_string_len(value);
    if (typed_enum_named_bits(domain, text, length, out)) return 1;
    if (domain->kind == CMETA_ENUM_FLAGS &&
        typed_enum_flags_text(domain, text, length, mask, out))
      return 1;
  }
  if (domain->kind == CMETA_ENUM_FLAGS && json_type(value) == JSON_ARRAY) {
    uint64_t bits = 0u;
    for (i = 0u; i < json_array_size(value); ++i) {
      uint64_t item;
      if (!typed_json_enum_bits(domain, json_array_get(value, i), &item))
        return 0;
      bits |= item;
    }
    *out = bits;
    return 1;
  }
  if (domain->signedness == CMETA_ENUM_SIGNED) {
    int64_t signed_value;
    int64_t maximum = (int64_t)(mask >> 1u);
    if (!typed_json_read_i64(value, &signed_value) ||
        signed_value < -maximum - 1 || signed_value > maximum)
      return 0;
    *out = (uint64_t)signed_value & mask;
    return 1;
  }
  if (!typed_json_read_u64(value, out) || *out > mask) return 0;
  return 1;
}

static DataBindStatus typed_native_from_json_scalar(
    const cmeta_data_desc *data, const json_value_t *value, void *storage,
    const char *path, DataBindError *error) {
  int64_t signed_value;
  uint64_t unsigned_value;
  double floating_value;
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data)) {
    uint8_t candidate;
    const char *text = json_type(value) == JSON_STRING ? json_string(value) : NULL;
    size_t text_length =
        json_type(value) == JSON_STRING ? json_string_len(value) : 0u;
    if (json_type(value) == JSON_BOOL) {
      candidate = json_bool(value) ? 1u : 0u;
    } else if (text != NULL &&
               ((text_length == 4u && memcmp(text, "true", 4u) == 0) ||
                (text_length == 3u && memcmp(text, "yes", 3u) == 0))) {
      candidate = 1u;
    } else if (text != NULL &&
               ((text_length == 5u && memcmp(text, "false", 5u) == 0) ||
                (text_length == 2u && memcmp(text, "no", 2u) == 0))) {
      candidate = 0u;
    } else if (text != NULL && text_length == 1u && text[0] == '1') {
      candidate = 1u;
    } else if (text != NULL && text_length == 1u && text[0] == '0') {
      candidate = 0u;
    } else if (json_type(value) == JSON_NUMBER &&
               typed_json_read_f64(value, &floating_value)) {
      candidate = floating_value != 0.0 ? 1u : 0u;
    } else {
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Expected Boolean value");
    }
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) !=
        CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Bool provider could not copy canonical storage");
    return DATA_BIND_OK;
  }
  if (salts_uuid_cmeta_data_valid(data)) {
    salts_uuid_t candidate = {{0}};
    if (json_type(value) != JSON_STRING ||
        salts_uuid_parse(json_string(value), &candidate) != 0)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Expected UUID value");
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) !=
        CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "UUID provider could not copy canonical storage");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    if (json_type(value) != JSON_STRING ||
        cmeta_data_fixed_extent(data, &extent) != CMETA_OK ||
        json_string_len(value) != extent)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Fixed bytes value has the wrong extent");
    if (cmeta_data_fixed_copy(data, storage, json_string(value), extent) !=
        CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes provider could not copy canonical storage");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    const cmeta_enum_domain *domain = cmeta_data_enum_bits_ops_of(data)->domain;
    uint64_t bits;
    if (!typed_json_enum_bits(domain, value, &bits) ||
        cmeta_data_enum_assign_bits(data, storage, bits) != CMETA_OK)
      goto range_error;
    return DATA_BIND_OK;
  }
  if (typed_cmeta_scalar_matches(data, &salts_int8_cmeta_data)) {
    if (!typed_json_read_i64(value, &signed_value) || signed_value < INT8_MIN ||
        signed_value > INT8_MAX)
      goto range_error;
    *(int8_t *)storage = (int8_t)signed_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_uint8_cmeta_data)) {
    if (!typed_json_read_u64(value, &unsigned_value) ||
        unsigned_value > UINT8_MAX)
      goto range_error;
    *(uint8_t *)storage = (uint8_t)unsigned_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_int16_cmeta_data)) {
    if (!typed_json_read_i64(value, &signed_value) || signed_value < INT16_MIN ||
        signed_value > INT16_MAX)
      goto range_error;
    *(int16_t *)storage = (int16_t)signed_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_uint16_cmeta_data)) {
    if (!typed_json_read_u64(value, &unsigned_value) ||
        unsigned_value > UINT16_MAX)
      goto range_error;
    *(uint16_t *)storage = (uint16_t)unsigned_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_int32_cmeta_data)) {
    if (!typed_json_read_i64(value, &signed_value) || signed_value < INT32_MIN ||
        signed_value > INT32_MAX)
      goto range_error;
    *(int32_t *)storage = (int32_t)signed_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_uint32_cmeta_data)) {
    if (!typed_json_read_u64(value, &unsigned_value) ||
        unsigned_value > UINT32_MAX)
      goto range_error;
    *(uint32_t *)storage = (uint32_t)unsigned_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_int64_cmeta_data)) {
    if (!typed_json_read_i64(value, &signed_value)) goto range_error;
    *(int64_t *)storage = signed_value;
  } else if (typed_cmeta_scalar_matches(data, &salts_uint64_cmeta_data)) {
    if (!typed_json_read_u64(value, &unsigned_value)) goto range_error;
    *(uint64_t *)storage = unsigned_value;
  } else if (typed_cmeta_scalar_matches(data, &cmeta_data_float)) {
    if (!typed_json_read_f64(value, &floating_value) ||
        floating_value < -(double)FLT_MAX || floating_value > (double)FLT_MAX)
      goto range_error;
    *(float *)storage = (float)floating_value;
  } else if (typed_cmeta_scalar_matches(data, &cmeta_data_double)) {
    if (!typed_json_read_f64(value, &floating_value)) goto range_error;
    *(double *)storage = floating_value;
  } else {
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Native CMeta scalar is outside this runtime slice");
  }
  return DATA_BIND_OK;

range_error:
  return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                     "Value is out of range for canonical native storage");
}

static json_value_t *typed_native_default_json(const cmeta_data_desc *data,
                                               const char *default_value) {
  if ((data->kind == CMETA_DATA_SINT || data->kind == CMETA_DATA_UINT) &&
      (strcmp(default_value, "true") == 0 ||
       strcmp(default_value, "false") == 0))
    return json_create_int64(strcmp(default_value, "true") == 0 ? 1 : 0);
  return json_create_string(default_value);
}

static DataBindStatus typed_native_from_json(
    DataBind *codec, const cmeta_data_desc *data, const TbeTypedType *overlay,
    const json_value_t *value, void *storage, const char *path,
    int invalid_scalar_uses_default, DataBindError *error) {
  const cmeta_data_struct_shape *shape;
  size_t i;
  if (codec == NULL || data == NULL || value == NULL || storage == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                       "Invalid canonical native JSON conversion");
  if (data->kind != CMETA_DATA_STRUCT)
    return typed_native_from_json_scalar(data, value, storage, path, error);
  if (overlay == NULL || json_type(value) != JSON_OBJECT)
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                       "Expected schema object");
  shape = (const cmeta_data_struct_shape *)data->shape;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    json_value_t *child = data_bind_internal_json_field_value(
        codec, overlay->name, i, value);
    json_value_t *default_value = NULL;
    int child_from_input = child != NULL;
    int has_default =
        data_bind_schema_field_at(codec, overlay->name, i, &schema_field) &&
        schema_field.has_default && schema_field.default_value != NULL;
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status = typed_native_path(
        field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (child == NULL && has_default) {
      default_value = typed_native_default_json(native_field->value,
                                                schema_field.default_value);
      if (default_value == NULL)
        return typed_error(error, DATA_BIND_ERR_OOM, field_path,
                           "Out of memory copying native field default");
      child = default_value;
    }
    if (child == NULL)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, field_path,
                         "Required field is missing");
    status = typed_native_from_json(
        codec, native_field->value,
        native_field->value->kind == CMETA_DATA_STRUCT
            ? wire_field->nested_overlay
            : NULL,
        child, (uint8_t *)storage + native_field->offset, field_path,
        invalid_scalar_uses_default, error);
    if (status == DATA_BIND_ERR_TYPE_MISMATCH && child_from_input &&
        invalid_scalar_uses_default &&
        native_field->value->kind != CMETA_DATA_STRUCT && has_default) {
      default_value = typed_native_default_json(native_field->value,
                                                schema_field.default_value);
      if (default_value == NULL)
        return typed_error(error, DATA_BIND_ERR_OOM, field_path,
                           "Out of memory copying native field default");
      (void)typed_error(error, DATA_BIND_OK, NULL, NULL);
      status = typed_native_from_json_scalar(
          native_field->value, default_value,
          (uint8_t *)storage + native_field->offset, field_path, error);
    }
    json_free(default_value);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static json_value_t *typed_native_to_json(
    DataBind *codec, const cmeta_data_desc *data, const TbeTypedType *overlay,
    const void *storage, const char *path, DataBindError *error) {
  if (data == NULL || storage == NULL) {
    typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                "Invalid canonical native object");
    return NULL;
  }
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape =
        (const cmeta_data_struct_shape *)data->shape;
    json_value_t *root = json_create_object();
    size_t i;
    if (root == NULL) {
      typed_error(error, DATA_BIND_ERR_OOM, path,
                  "Out of memory creating JSON object");
      return NULL;
    }
    for (i = 0u; i < shape->field_count; ++i) {
      const cmeta_data_field_desc *native_field = &shape->fields[i];
      const TbeTypedField *wire_field = &overlay->fields[i];
      const char *output_name = data_bind_internal_json_field_output_name(
          codec, overlay->name, i);
      char field_path[sizeof(((DataBindError *)0)->path)];
      json_value_t *child;
      if (output_name == NULL) {
        json_free(root);
        typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                    "Canonical native field has no schema output name");
        return NULL;
      }
      if (typed_native_path(field_path, sizeof(field_path), path,
                            native_field->name, error) != DATA_BIND_OK) {
        json_free(root);
        return NULL;
      }
      child = typed_native_to_json(
          codec, native_field->value,
          native_field->value->kind == CMETA_DATA_STRUCT
              ? wire_field->nested_overlay
              : NULL,
          (const uint8_t *)storage + native_field->offset,
          field_path, error);
      if (child == NULL) {
        json_free(root);
        return NULL;
      }
      if (!json_object_add_checked(root, output_name, child)) {
        json_free(child);
        json_free(root);
        typed_error(error, DATA_BIND_ERR_OOM, field_path,
                    "Out of memory adding canonical native field");
        return NULL;
      }
    }
    return root;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    const cmeta_enum_domain *domain = cmeta_data_enum_bits_ops_of(data)->domain;
    uint64_t bits;
    if (cmeta_data_enum_read_bits(data, storage, &bits) != CMETA_OK) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Canonical enum provider could not read storage");
      return NULL;
    }
    return typed_json_created(
        domain->signedness == CMETA_ENUM_SIGNED
            ? json_create_int64(typed_native_enum_signed_value(domain, bits))
            : json_create_uint64(bits),
        path, error);
  }
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data)) {
    uint8_t candidate = 0u;
    if (cmeta_data_fixed_copy(data, &candidate, storage,
                              data->storage_type->size) != CMETA_OK) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Canonical Bool provider rejected native storage");
      return NULL;
    }
    return typed_json_created(json_create_bool(candidate != 0u), path, error);
  }
  if (salts_uuid_cmeta_data_valid(data)) {
    salts_uuid_t candidate = { {0} };
    char text[SALTS_UUID_STRING_SIZE];
    if (cmeta_data_fixed_copy(data, &candidate, storage,
                              data->storage_type->size) != CMETA_OK ||
        salts_uuid_format(&candidate, text, sizeof(text)) != SALTS_OK) {
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Canonical UUID provider rejected native storage");
      return NULL;
    }
    return typed_json_created(json_create_string(text), path, error);
  }
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    void *candidate;
    json_value_t *json;
    if (cmeta_data_fixed_extent(data, &extent) != CMETA_OK) {
      typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                  "Fixed bytes provider has no exact extent");
      return NULL;
    }
    candidate = calloc(1u, extent);
    if (candidate == NULL) {
      typed_error(error, DATA_BIND_ERR_OOM, path,
                  "Out of memory validating fixed bytes storage");
      return NULL;
    }
    if (cmeta_data_fixed_copy(data, candidate, storage, extent) != CMETA_OK) {
      (void)cmeta_data_fixed_restore_zero(data, candidate);
      free(candidate);
      typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                  "Fixed bytes provider rejected native storage");
      return NULL;
    }
    json = typed_bytes_json((const uint8_t *)candidate, extent, path, error);
    (void)cmeta_data_fixed_restore_zero(data, candidate);
    free(candidate);
    return json;
  }
  if (typed_cmeta_scalar_matches(data, &salts_int8_cmeta_data))
    return typed_json_created(json_create_int64(*(const int8_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_uint8_cmeta_data))
    return typed_json_created(json_create_int64(*(const uint8_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_int16_cmeta_data))
    return typed_json_created(json_create_int64(*(const int16_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_uint16_cmeta_data))
    return typed_json_created(json_create_int64(*(const uint16_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_int32_cmeta_data))
    return typed_json_created(json_create_int64(*(const int32_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_uint32_cmeta_data))
    return typed_json_created(json_create_int64(*(const uint32_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_int64_cmeta_data))
    return typed_json_created(json_create_int64(*(const int64_t *)storage), path,
                              error);
  if (typed_cmeta_scalar_matches(data, &salts_uint64_cmeta_data))
    return typed_json_created(json_create_uint64(*(const uint64_t *)storage),
                              path, error);
  if (typed_cmeta_scalar_matches(data, &cmeta_data_float)) {
    float value = *(const float *)storage;
    if (isfinite(value))
      return typed_json_created(json_create_number(value), path, error);
  } else if (typed_cmeta_scalar_matches(data, &cmeta_data_double)) {
    double value = *(const double *)storage;
    if (isfinite(value))
      return typed_json_created(json_create_number(value), path, error);
  }
  typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
              "Canonical native scalar cannot be represented as JSON");
  return NULL;
}

enum { TBE_TYPED_TEXT_MAX_PATH = 255u };

static int typed_text_append(tstr *out, const char *text, size_t length) {
  tstr next;
  if (out == NULL || *out == NULL || (text == NULL && length != 0u)) return 0;
  next = tstr_cat_len(*out, text, length);
  if (next == NULL) return 0;
  *out = next;
  return 1;
}

static int typed_csv_append_field(tstr *out, const char *text, size_t length) {
  size_t index;
  size_t start = 0u;
  int quoted = 0;
  if (out == NULL || *out == NULL || (text == NULL && length != 0u)) return 0;
  if ((length != 0u &&
       (text[0] == ' ' || text[0] == '\t' || text[length - 1u] == ' ' ||
        text[length - 1u] == '\t')) ||
      memchr(text, ',', length) != NULL || memchr(text, '"', length) != NULL ||
      memchr(text, '\r', length) != NULL || memchr(text, '\n', length) != NULL)
    quoted = 1;
  if (!quoted) return typed_text_append(out, text, length);
  if (!typed_text_append(out, "\"", 1u)) return 0;
  for (index = 0u; index < length; ++index) {
    if (text[index] != '"') continue;
    if (!typed_text_append(out, text + start, index - start) ||
        !typed_text_append(out, "\"\"", 2u))
      return 0;
    start = index + 1u;
  }
  return typed_text_append(out, text + start, length - start) &&
         typed_text_append(out, "\"", 1u);
}

static int typed_csv_path_component_valid(const char *name, size_t length) {
  if (name == NULL || length == 0u || memchr(name, '\0', length) != NULL ||
      memchr(name, '.', length) != NULL || memchr(name, '[', length) != NULL)
    return 0;
  return vstr_utf8_valid(vstr_from_buf(name, length));
}

static tstr typed_csv_child_path(const tstr prefix, const char *name,
                                 size_t name_length, DataBindStatus *status) {
  size_t prefix_length = prefix != NULL ? tstr_len(prefix) : 0u;
  size_t separator_length = prefix_length != 0u ? 1u : 0u;
  tstr path;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (!typed_csv_path_component_valid(name, name_length) ||
      prefix_length > TBE_TYPED_TEXT_MAX_PATH - separator_length ||
      name_length > TBE_TYPED_TEXT_MAX_PATH - prefix_length - separator_length)
    return NULL;
  path = prefix != NULL ? tstr_clone(prefix) : tstr_new();
  if (path == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  if ((separator_length != 0u && !typed_text_append(&path, ".", 1u)) ||
      !typed_text_append(&path, name, name_length)) {
    tstr_free(path);
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return path;
}

static DataBindStatus typed_csv_scalar_text(const json_value_t *value,
                                            const char **text,
                                            size_t *length) {
  if (value == NULL || text == NULL || length == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (json_type(value) == JSON_STRING) {
    *text = json_string(value);
    *length = json_string_len(value);
    if (*text == NULL || memchr(*text, '\0', *length) != NULL ||
        !vstr_utf8_valid(vstr_from_buf(*text, *length)))
      return DATA_BIND_ERR_TYPE_MISMATCH;
    return DATA_BIND_OK;
  }
  if (json_type(value) == JSON_NUMBER) {
    *text = json_number_text(value, length);
    return *text != NULL ? DATA_BIND_OK : DATA_BIND_ERR_TYPE_MISMATCH;
  }
  if (json_type(value) == JSON_BOOL) {
    *text = json_bool(value) ? "true" : "false";
    *length = json_bool(value) ? 4u : 5u;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

static DataBindStatus typed_csv_flatten_json(
    const json_value_t *value, const tstr path, tstr *headers, tstr *row,
    size_t *cell_count, unsigned depth) {
  size_t index;
  if (value == NULL || headers == NULL || row == NULL || cell_count == NULL)
    return DATA_BIND_ERR_INVALID_ARG;
  if (json_type(value) == JSON_OBJECT) {
    if (depth > TBE_TYPED_NATIVE_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;
    if (json_object_size(value) == 0u) return DATA_BIND_ERR_TYPE_MISMATCH;
    for (index = 0u; index < json_object_size(value); ++index) {
      const char *name = json_object_key(value, index);
      size_t name_length = json_object_key_len(value, index);
      DataBindStatus status;
      tstr child_path = typed_csv_child_path(path, name, name_length, &status);
      if (child_path == NULL) return status;
      status = typed_csv_flatten_json(json_object_value(value, index), child_path,
                                      headers, row, cell_count, depth + 1u);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  }
  {
    const char *text;
    size_t length;
    DataBindStatus status = typed_csv_scalar_text(value, &text, &length);
    if (status != DATA_BIND_OK || path == NULL || tstr_empty(path))
      return status != DATA_BIND_OK ? status : DATA_BIND_ERR_TYPE_MISMATCH;
    if ((*cell_count != 0u &&
         (!typed_text_append(headers, ",", 1u) ||
          !typed_text_append(row, ",", 1u))) ||
        !typed_csv_append_field(headers, path, tstr_len(path)) ||
        !typed_csv_append_field(row, text, length))
      return DATA_BIND_ERR_OOM;
    ++*cell_count;
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_json_serialize_csv(
    const json_value_t *json, char **out, size_t *out_length,
    const char *path, DataBindError *error) {
  tstr headers = tstr_new();
  tstr row = tstr_new();
  tstr csv = NULL;
  size_t cell_count = 0u;
  DataBindStatus status;
  if (headers == NULL || row == NULL) {
    tstr_free(headers);
    tstr_free(row);
    return typed_error(error, DATA_BIND_ERR_OOM, path,
                       "Out of memory creating native CSV rows");
  }
  status = typed_csv_flatten_json(json, NULL, &headers, &row, &cell_count, 0u);
  if (status == DATA_BIND_OK && cell_count == 0u)
    status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (status == DATA_BIND_OK) {
    csv = headers;
    headers = NULL;
    if (!typed_text_append(&csv, "\r\n", 2u) ||
        !typed_text_append(&csv, row, tstr_len(row)) ||
        !typed_text_append(&csv, "\r\n", 2u))
      status = DATA_BIND_ERR_OOM;
  }
  if (status == DATA_BIND_OK) {
    *out = tstr_to_cstr(csv);
    if (*out == NULL)
      status = DATA_BIND_ERR_OOM;
    else if (out_length != NULL)
      *out_length = tstr_len(csv);
  }
  tstr_free(headers);
  tstr_free(row);
  tstr_free(csv);
  if (status != DATA_BIND_OK)
    return typed_error(error, status, path,
                       status == DATA_BIND_ERR_OOM
                           ? "Out of memory serializing canonical native CSV"
                           : "Canonical native object cannot be represented as CSV");
  return typed_error(error, DATA_BIND_OK, NULL, NULL);
}

static int typed_csv_join_path(char *out, size_t capacity,
                               const char *prefix, const char *name) {
  int written;
  if (out == NULL || capacity == 0u || name == NULL) return 0;
  written = prefix != NULL && prefix[0] != '\0'
                ? snprintf(out, capacity, "%s.%s", prefix, name)
                : snprintf(out, capacity, "%s", name);
  return written > 0 && (size_t)written < capacity;
}

static int typed_csv_has_path(const csv_doc_t *document, const char *path) {
  size_t column;
  if (document == NULL || path == NULL) return 0;
  for (column = 0u; column < csv_column_count(document); ++column) {
    const char *header = csv_header_get(document, column);
    if (header != NULL &&
        data_bind_internal_csv_header_matches_path(header, path))
      return 1;
  }
  return 0;
}

static DataBindStatus typed_csv_native_json(
    DataBind *codec, const cmeta_data_desc *data, const TbeTypedType *overlay,
    const csv_doc_t *document, size_t row, const char *prefix,
    json_value_t **out, const char *type_name, DataBindError *error) {
  const cmeta_data_struct_shape *shape;
  json_value_t *object;
  size_t field_index;
  if (codec == NULL || data == NULL || overlay == NULL || document == NULL ||
      out == NULL || data->kind != CMETA_DATA_STRUCT)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical native CSV conversion");
  object = json_create_object();
  if (object == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                       "Out of memory creating native CSV object");
  shape = (const cmeta_data_struct_shape *)data->shape;
  for (field_index = 0u; field_index < shape->field_count; ++field_index) {
    const cmeta_data_field_desc *native_field = &shape->fields[field_index];
    const TbeTypedField *wire_field = &overlay->fields[field_index];
    const char *output_name = data_bind_internal_json_field_output_name(
        codec, overlay->name, field_index);
    size_t input_count = data_bind_internal_field_input_name_count(
        codec, overlay->name, field_index);
    json_value_t *child = NULL;
    size_t input_index;
    if (output_name == NULL || input_count == 0u) {
      json_free(object);
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Canonical CSV field has no schema name");
    }
    for (input_index = 0u; input_index < input_count; ++input_index) {
      const char *input_name = data_bind_internal_field_input_name_at(
          codec, overlay->name, field_index, input_index);
      char field_path[TBE_TYPED_TEXT_MAX_PATH + 1u];
      DataBindStatus status;
      if (!typed_csv_join_path(field_path, sizeof(field_path), prefix,
                               input_name)) {
        json_free(object);
        return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                           "Canonical CSV field path is too long");
      }
      if (native_field->value->kind == CMETA_DATA_STRUCT) {
        if (!typed_csv_has_path(document, field_path)) continue;
        status = typed_csv_native_json(
            codec, native_field->value, wire_field->nested_overlay, document,
            row, field_path, &child, type_name, error);
        if (status != DATA_BIND_OK) {
          json_free(object);
          return status;
        }
      } else {
        size_t column;
        const char *cell;
        size_t cell_length;
        if (!data_bind_internal_csv_find_path_column(document, field_path,
                                                     &column))
          continue;
        cell = csv_get(document, row, column);
        cell_length = csv_get_len(document, row, column);
        if (cell == NULL) {
          json_free(object);
          return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name,
                             "CSV row does not match its header");
        }
        if (cell_length != 0u) {
          child = json_create_string_n(cell, cell_length);
          if (child == NULL) {
            json_free(object);
            return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                               "Out of memory copying native CSV cell");
          }
        }
      }
      break;
    }
    if (child != NULL && !json_object_add_checked(object, output_name, child)) {
      json_free(child);
      json_free(object);
      return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                         "Out of memory adding native CSV field");
    }
  }
  *out = object;
  return DATA_BIND_OK;
}

static DataBindStatus typed_csv_parse_json(
    DataBind *codec, const cmeta_data_desc *data,
    const TbeTypedType *overlay, const char *text, size_t length, size_t row,
    json_value_t **out, const char *type_name, DataBindError *error) {
  csv_options_t options = CSV_OPTIONS_DEFAULT;
  csv_doc_t *document;
  DataBindStatus status;
  options.has_header = true;
  document = csv_parse_opts(text, length, &options);
  if (document == NULL)
    return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                       "CSV parse failed");
  if (row >= csv_row_count(document)) {
    csv_free(document);
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, type_name,
                       "CSV row is out of range");
  }
  status = typed_csv_native_json(codec, data, overlay, document, row, NULL,
                                 out, type_name, error);
  csv_free(document);
  return status == DATA_BIND_OK ? typed_error(error, DATA_BIND_OK, NULL, NULL)
                                : status;
}

static salts_xml_node typed_xml_child_named(salts_xml_node parent,
                                             const char *name) {
  size_t index;
  salts_xml_node missing = {0};
  if (!parent.impl || name == NULL) return missing;
  for (index = 0u; index < salts_xml_node_child_count(parent); ++index) {
    salts_xml_node child = salts_xml_node_child_at(parent, index);
    salts_xml_string_view child_name;
    if (salts_xml_node_type(child) != SALTS_XML_ELEMENT) continue;
    child_name = salts_xml_node_display_name(child);
    if (child_name.data != NULL && child_name.size == strlen(name) &&
        memcmp(child_name.data, name, child_name.size) == 0)
      return child;
  }
  return missing;
}

static int typed_xml_attribute_named(salts_xml_node parent, const char *name,
                                     salts_xml_string_view *out) {
  size_t index;
  size_t name_length;
  if (!parent.impl || name == NULL || out == NULL) return 0;
  name_length = strlen(name);
  for (index = 0u; index < salts_xml_node_attribute_count(parent); ++index) {
    salts_xml_attribute attribute = salts_xml_node_attribute_at(parent, index);
    salts_xml_string_view qualified =
        salts_xml_attribute_qualified_name(attribute);
    salts_xml_string_view local = salts_xml_attribute_local_name(attribute);
    if ((qualified.data != NULL && qualified.size == name_length &&
         memcmp(qualified.data, name, name_length) == 0) ||
        (local.data != NULL && local.size == name_length &&
         memcmp(local.data, name, name_length) == 0)) {
      *out = salts_xml_attribute_value(attribute);
      return 1;
    }
  }
  return 0;
}

static DataBindStatus typed_xml_native_json(
    DataBind *codec, const cmeta_data_desc *data, const TbeTypedType *overlay,
    salts_xml_node node, json_value_t **out, unsigned depth,
    const char *type_name, DataBindError *error) {
  const cmeta_data_struct_shape *shape;
  json_value_t *object;
  size_t field_index;
  if (codec == NULL || data == NULL || !node.impl || out == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical native XML conversion");
  if (data->kind != CMETA_DATA_STRUCT) {
    salts_xml_string_view text = salts_xml_node_text_view(node);
    if (text.size == 0u && data->kind != CMETA_DATA_BYTES) {
      *out = NULL;
      return DATA_BIND_OK;
    }
    *out = json_create_string_n(text.data != NULL ? text.data : "", text.size);
    return *out != NULL
               ? DATA_BIND_OK
               : typed_error(error, DATA_BIND_ERR_OOM, type_name,
                             "Out of memory copying native XML text");
  }
  if (depth > TBE_TYPED_NATIVE_MAX_DEPTH)
    return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                       "XML native object exceeds the supported depth");
  if (overlay == NULL)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Native XML struct has no schema overlay");
  object = json_create_object();
  if (object == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                       "Out of memory creating native XML object");
  shape = (const cmeta_data_struct_shape *)data->shape;
  for (field_index = 0u; field_index < shape->field_count; ++field_index) {
    const cmeta_data_field_desc *native_field = &shape->fields[field_index];
    const TbeTypedField *wire_field = &overlay->fields[field_index];
    const char *output_name = data_bind_internal_json_field_output_name(
        codec, overlay->name, field_index);
    size_t input_count = data_bind_internal_field_input_name_count(
        codec, overlay->name, field_index);
    salts_xml_node child_node = {0};
    salts_xml_string_view attribute_text = {0};
    int has_attribute = 0;
    json_value_t *child = NULL;
    size_t input_index;
    DataBindStatus status;
    if (output_name == NULL || input_count == 0u) {
      json_free(object);
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Canonical XML field has no schema name");
    }
    for (input_index = 0u; input_index < input_count; ++input_index) {
      const char *input_name = data_bind_internal_field_input_name_at(
          codec, overlay->name, field_index, input_index);
      child_node = typed_xml_child_named(node, input_name);
      if (child_node.impl) break;
      if (native_field->value->kind != CMETA_DATA_STRUCT &&
          typed_xml_attribute_named(node, input_name, &attribute_text)) {
        has_attribute = 1;
        break;
      }
    }
    if (!child_node.impl && !has_attribute) continue;
    if (child_node.impl)
      status = typed_xml_native_json(
          codec, native_field->value,
          native_field->value->kind == CMETA_DATA_STRUCT
              ? wire_field->nested_overlay
              : NULL,
          child_node, &child, depth + 1u, type_name, error);
    else if (attribute_text.size == 0u &&
             native_field->value->kind != CMETA_DATA_BYTES)
      status = DATA_BIND_OK;
    else {
      child = json_create_string_n(
          attribute_text.data != NULL ? attribute_text.data : "",
          attribute_text.size);
      status = child != NULL
                   ? DATA_BIND_OK
                   : typed_error(error, DATA_BIND_ERR_OOM, type_name,
                                 "Out of memory copying native XML attribute");
    }
    if (status != DATA_BIND_OK) {
      json_free(object);
      return status;
    }
    if (child != NULL && !json_object_add_checked(object, output_name, child)) {
      json_free(child);
      json_free(object);
      return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                         "Out of memory adding native XML field");
    }
  }
  *out = object;
  return DATA_BIND_OK;
}

static DataBindStatus typed_xml_parse_json(
    DataBind *codec, const cmeta_data_desc *data,
    const TbeTypedType *overlay, const char *text, size_t length,
    json_value_t **out, const char *type_name, DataBindError *error) {
  salts_xml_document document = {0};
  salts_xml_node root;
  DataBindStatus status;
  if (salts_xml_parse(&document, text, length, NULL, NULL) != SALTS_XML_OK)
    return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                       "XML parse failed");
  root = salts_xml_document_root(&document);
  status = typed_xml_native_json(codec, data, overlay, root, out, 0u,
                                 type_name, error);
  salts_xml_document_destroy(&document);
  return status == DATA_BIND_OK ? typed_error(error, DATA_BIND_OK, NULL, NULL)
                                : status;
}

static int typed_xml_name_valid(const char *name, size_t length) {
  size_t index;
  const unsigned char *bytes = (const unsigned char *)name;
  if (name == NULL || length == 0u || strlen(name) != length ||
      !(isalpha(bytes[0]) || bytes[0] == '_' || bytes[0] == ':'))
    return 0;
  for (index = 1u; index < length; ++index)
    if (!(isalnum(bytes[index]) || bytes[index] == '_' || bytes[index] == ':' ||
          bytes[index] == '-' || bytes[index] == '.'))
      return 0;
  return 1;
}

static DataBindStatus typed_json_to_xml(const json_value_t *value,
                                        salts_xml_node node,
                                        unsigned depth) {
  size_t index;
  if (value == NULL || !node.impl) return DATA_BIND_ERR_INVALID_ARG;
  if (json_type(value) == JSON_OBJECT) {
    if (depth > TBE_TYPED_NATIVE_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;
    for (index = 0u; index < json_object_size(value); ++index) {
      const char *name = json_object_key(value, index);
      size_t name_length = json_object_key_len(value, index);
      salts_xml_node child = {0};
      salts_xml_status xml_status;
      DataBindStatus status;
      if (!typed_xml_name_valid(name, name_length))
        return DATA_BIND_ERR_TYPE_MISMATCH;
      xml_status = salts_xml_node_add_element(node, name, &child);
      if (xml_status != SALTS_XML_OK)
        return xml_status == SALTS_XML_ALLOCATION_FAILED
                   ? DATA_BIND_ERR_OOM
                   : DATA_BIND_ERR_TYPE_MISMATCH;
      status = typed_json_to_xml(json_object_value(value, index), child,
                                 depth + 1u);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  }
  {
    const char *text;
    size_t length;
    salts_xml_status xml_status;
    if (json_type(value) == JSON_STRING) {
      text = json_string(value);
      length = json_string_len(value);
      if (text == NULL || memchr(text, '\0', length) != NULL ||
          !vstr_utf8_valid(vstr_from_buf(text, length)))
        return DATA_BIND_ERR_TYPE_MISMATCH;
    } else if (json_type(value) == JSON_NUMBER) {
      text = json_number_text(value, &length);
      if (text == NULL) return DATA_BIND_ERR_TYPE_MISMATCH;
    } else if (json_type(value) == JSON_BOOL) {
      text = json_bool(value) ? "true" : "false";
      length = json_bool(value) ? 4u : 5u;
    } else {
      return DATA_BIND_ERR_TYPE_MISMATCH;
    }
    if (strlen(text) != length) return DATA_BIND_ERR_TYPE_MISMATCH;
    xml_status = salts_xml_node_set_text(node, text);
    return xml_status == SALTS_XML_OK
               ? DATA_BIND_OK
               : (xml_status == SALTS_XML_ALLOCATION_FAILED
                      ? DATA_BIND_ERR_OOM
                      : DATA_BIND_ERR_TYPE_MISMATCH);
  }
}

static DataBindStatus typed_json_serialize_xml(
    const char *type_name, const json_value_t *json, char **out,
    size_t *out_length, DataBindError *error) {
  salts_xml_document document = {0};
  salts_xml_node root;
  DataBindStatus status;
  if (!typed_xml_name_valid(type_name, strlen(type_name)))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Canonical native type is not a valid XML name");
  if (salts_xml_document_create(&document, type_name) != SALTS_XML_OK)
    return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                       "Unable to create canonical native XML document");
  root = salts_xml_document_root(&document);
  status = typed_json_to_xml(json, root, 0u);
  if (status == DATA_BIND_OK) {
    *out = salts_xml_document_serialize(&document, out_length);
    if (*out == NULL) status = DATA_BIND_ERR_OOM;
  }
  salts_xml_document_destroy(&document);
  if (status != DATA_BIND_OK)
    return typed_error(error, status, type_name,
                       status == DATA_BIND_ERR_OOM
                           ? "Out of memory serializing canonical native XML"
                           : "Canonical native object cannot be represented as XML");
  return typed_error(error, DATA_BIND_OK, NULL, NULL);
}

static DataBindStatus typed_text_parse_json(
    DataBind *codec, const cmeta_data_desc *data,
    const TbeTypedType *overlay, DataBindFormat format, const char *text,
    size_t length, size_t row, json_value_t **out, const char *type_name,
    DataBindError *error) {
  if (format == DATA_BIND_FORMAT_JSON) {
    *out = json_parse(text, length);
    return *out != NULL
               ? typed_error(error, DATA_BIND_OK, NULL, NULL)
               : typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                             "JSON parse failed");
  }
  if (format == DATA_BIND_FORMAT_YAML) {
    cyaml_doc_t *document = cyaml_parse(text, length, NULL, NULL);
    if (document == NULL)
      return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                         "YAML parse failed");
    *out = json_value_from_cyaml(document);
    cyaml_free(document);
    return *out != NULL
               ? typed_error(error, DATA_BIND_OK, NULL, NULL)
               : typed_error(
                     error, DATA_BIND_ERR_TYPE_MISMATCH, type_name,
                     "YAML value cannot be represented as JSON-compatible data");
  }
  if (format == DATA_BIND_FORMAT_CSV)
    return typed_csv_parse_json(codec, data, overlay, text, length, row, out,
                                type_name, error);
  if (format == DATA_BIND_FORMAT_XML)
    return typed_xml_parse_json(codec, data, overlay, text, length, out,
                                type_name, error);
  return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                     "Unknown canonical descriptor input format");
}

static DataBindStatus typed_json_serialize_text(
    DataBindFormat format, const char *type_name, const json_value_t *json,
    char **out, size_t *out_length, DataBindError *error) {
  if (format == DATA_BIND_FORMAT_JSON) {
    *out = json_serialize(json, out_length);
    return *out != NULL
               ? typed_error(error, DATA_BIND_OK, NULL, NULL)
               : typed_error(error, DATA_BIND_ERR_OOM, type_name,
                             "Out of memory serializing canonical native JSON");
  }
  if (format == DATA_BIND_FORMAT_YAML) {
    cyaml_doc_t *document = cyaml_doc_from_json_value(json);
    if (document == NULL)
      return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                         "Unable to construct native YAML document");
    *out = cyaml_emit(document, NULL, out_length);
    cyaml_free(document);
    return *out != NULL
               ? typed_error(error, DATA_BIND_OK, NULL, NULL)
               : typed_error(error, DATA_BIND_ERR_OOM, type_name,
                             "Out of memory serializing canonical native YAML");
  }
  if (format == DATA_BIND_FORMAT_CSV)
    return typed_json_serialize_csv(json, out, out_length, type_name, error);
  if (format == DATA_BIND_FORMAT_XML)
    return typed_json_serialize_xml(type_name, json, out, out_length, error);
  return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                     "Unknown canonical descriptor output format");
}

DataBindStatus tbe_typed_descriptor_validate(const TbeTypedDescriptor *descriptor,
                                             DataBindError *error) {
  DataBindStatus status = typed_descriptor_native_record(descriptor, NULL, error);
  if (status != DATA_BIND_OK) return status;
  return typed_error(error, DATA_BIND_OK, NULL, NULL);
}

DataBindStatus tbe_typed_descriptor_init(const TbeTypedDescriptor *descriptor,
                                         void *object, DataBindError *error) {
  TypedNativeRecord native;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid typed object");
  status = typed_native_init_value(native.data, object, native.overlay->name,
                                   error);
  return status == DATA_BIND_OK ? typed_error(error, DATA_BIND_OK, NULL, NULL)
                                : status;
}

DataBindStatus tbe_typed_descriptor_clear(const TbeTypedDescriptor *descriptor,
                                          void *object, DataBindError *error) {
  TypedNativeRecord native;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid typed object");
  status = typed_native_clear_value(native.data, object, native.overlay->name,
                                    error);
  return status == DATA_BIND_OK ? typed_error(error, DATA_BIND_OK, NULL, NULL)
                                : status;
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

static int typed_native_schema_field_matches(
    DataBind *codec, const cmeta_data_desc *native,
    const TbeTypedField *wire, const DataBindSchemaField *schema) {
  int has_wire_offset;
  if (native == NULL || wire == NULL || schema == NULL ||
      !typed_nonempty(wire->name) || !typed_nonempty(schema->name) ||
      strcmp(wire->name, schema->name) != 0 || schema->is_optional ||
      (wire->flags & (TBE_TYPED_FIELD_OPTIONAL | TBE_TYPED_FIELD_GROUP)) != 0u)
    return 0;
  has_wire_offset = (wire->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0;
  if (has_wire_offset != (schema->has_offset != 0) ||
      (has_wire_offset && wire->wire_offset != schema->offset))
    return 0;
  if (native->kind == CMETA_DATA_STRUCT) {
    return !schema->is_collection && !schema->is_map && !schema->is_group &&
           wire->nested_overlay != NULL && typed_nonempty(schema->type) &&
           typed_nonempty(wire->nested_overlay->name) &&
           strcmp(wire->nested_overlay->name, schema->type) == 0;
  }
  if (native->kind == CMETA_DATA_ENUM) {
    DataBindSchemaType enum_type = DATA_BIND_SCHEMA_TYPE_INIT;
    const cmeta_enum_domain *domain = cmeta_data_enum_bits_ops_of(native)->domain;
    TbeTypedKind underlying;
    return typed_nonempty(schema->type) &&
           data_bind_schema_find_type(codec, schema->type, &enum_type) &&
           enum_type.kind == (domain->kind == CMETA_ENUM_FLAGS
                                  ? DATA_BIND_SCHEMA_FLAGS
                                  : DATA_BIND_SCHEMA_ENUM) &&
           typed_scalar_kind_from_name(enum_type.underlying_type, &underlying) &&
           underlying == wire->wire_kind;
  }
  if (typed_cmeta_scalar_matches(native, &salts_bool8_cmeta_data))
    return typed_nonempty(schema->type) && strcmp(schema->type, "bool") == 0 &&
           wire->wire_kind == TBE_TYPED_BOOL;
  if (salts_uuid_cmeta_data_valid(native))
    return typed_nonempty(schema->type) && strcmp(schema->type, "uuid") == 0 &&
           wire->wire_kind == TBE_TYPED_UUID;
  if (native->kind == CMETA_DATA_BYTES &&
      cmeta_data_fixed_ops_of(native) != NULL) {
    size_t extent;
    return typed_nonempty(schema->type) && strcmp(schema->type, "bytes") == 0 &&
           schema->is_fixed_size && schema->has_size_bytes &&
           cmeta_data_fixed_extent(native, &extent) == CMETA_OK &&
           extent == schema->size_bytes && wire->wire_kind == TBE_TYPED_FIXED_BYTES;
  }
  {
    TbeTypedKind schema_kind;
    return typed_scalar_kind_from_name(schema->type, &schema_kind) &&
           schema_kind == wire->wire_kind;
  }
}

static DataBindStatus typed_native_validate_schema_at(
    DataBind *codec, const char *type_name, const cmeta_data_desc *data,
    const TbeTypedType *overlay, unsigned depth, DataBindError *error) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  DataBindSchemaType schema_type = DATA_BIND_SCHEMA_TYPE_INIT;
  DataBindStatus status;
  size_t i;
  if (codec == NULL || !typed_nonempty(type_name) || overlay == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical schema validation arguments");
  if (depth > TBE_TYPED_NATIVE_MAX_DEPTH)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Canonical schema nesting exceeds 32");
  if (!typed_nonempty(overlay->name) || strcmp(overlay->name, type_name) != 0)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Schema overlay name does not match the requested type");
  if (!data_bind_schema_find_type(codec, type_name, &schema_type))
    return typed_error(error, DATA_BIND_ERR_TYPE_NOT_FOUND, type_name,
                       "Canonical schema type was not found");
  if ((schema_type.kind != DATA_BIND_SCHEMA_MESSAGE &&
       schema_type.kind != DATA_BIND_SCHEMA_COMPOSITE &&
       schema_type.kind != DATA_BIND_SCHEMA_GROUP) ||
      schema_type.field_count != shape->field_count ||
      (schema_type.has_fixed_block_size &&
       schema_type.fixed_block_size != overlay->fixed_block_size) ||
      (!schema_type.has_fixed_block_size && overlay->fixed_block_size != 0u))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                       "Schema overlay does not match the schema record");
  if (depth == 0u) {
    const char *byte_order = data_bind_schema_attribute_get(codec, "byte_order");
    int schema_big_endian =
        byte_order != NULL && strcmp(byte_order, "big") == 0;
    if ((overlay->wire_big_endian != 0) != schema_big_endian)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Schema overlay byte order does not match the schema");
  }
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    char field_path[sizeof(((DataBindError *)0)->path)];
    status = typed_native_path(field_path, sizeof(field_path), type_name,
                               native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field) ||
        !typed_native_schema_field_matches(codec, native_field->value,
                                           wire_field, &schema_field))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Canonical native field does not match its schema overlay");
    if (native_field->value->kind == CMETA_DATA_STRUCT) {
      status = typed_native_validate_schema_at(
          codec, schema_field.type, native_field->value,
          wire_field->nested_overlay, depth + 1u, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
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

static int typed_native_wire_extent(const cmeta_data_desc *data,
                                    const TbeTypedField *wire,
                                    size_t *extent) {
  if (data == NULL || wire == NULL || extent == NULL) return 0;
  if (data->kind == CMETA_DATA_STRUCT) {
    if (wire->nested_overlay == NULL) return 0;
    *extent = wire->nested_overlay->fixed_block_size;
    return *extent != 0u;
  }
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL)
    return cmeta_data_fixed_extent(data, extent) == CMETA_OK && *extent != 0u;
  *extent = typed_kind_size(wire->wire_kind);
  return *extent != 0u;
}

static DataBindStatus typed_native_validate_wire(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const char *path, DataBindError *error) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  if (overlay->presence_size != 0u || overlay->fixed_block_size == 0u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Supported descriptor has no complete fixed wire layout");
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    size_t extent;
    size_t j;
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status = typed_native_path(
        field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if ((wire_field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0u ||
        !typed_native_wire_extent(native_field->value, wire_field, &extent) ||
        wire_field->wire_size != extent ||
        !typed_size_fits(wire_field->wire_offset, extent,
                         overlay->fixed_block_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Schema overlay has no exact fixed wire field");
    for (j = 0u; j < i; ++j) {
      const TbeTypedField *previous = &overlay->fields[j];
      size_t previous_extent;
      if (!typed_native_wire_extent(shape->fields[j].value, previous,
                                    &previous_extent) ||
          typed_ranges_overlap(wire_field->wire_offset, extent,
                               previous->wire_offset, previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Schema overlay wire fields overlap");
    }
    if (native_field->value->kind == CMETA_DATA_STRUCT) {
      status = typed_native_validate_wire(native_field->value,
                                          wire_field->nested_overlay,
                                          field_path, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_read_wire_scalar(
    const cmeta_data_desc *data, TbeTypedKind wire_kind,
    const uint8_t *source, int big_endian, void *storage,
    const char *path, DataBindError *error) {
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    if (wire_kind != TBE_TYPED_FIXED_BYTES ||
        cmeta_data_fixed_extent(data, &extent) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes wire storage disagrees with canonical CMeta");
    if (cmeta_data_fixed_copy(data, storage, source, extent) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) &&
      wire_kind == TBE_TYPED_BOOL) {
    uint8_t candidate = (uint8_t)(tbe_wire_read_u8(source, big_endian) != 0u);
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Bool provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (salts_uuid_cmeta_data_valid(data) && wire_kind == TBE_TYPED_UUID) {
    salts_uuid_t candidate = { {0} };
    memcpy(candidate.bytes, source, SALTS_UUID_SIZE);
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "UUID provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0u;
    int input_signed = 0;
    switch (wire_kind) {
    case TBE_TYPED_I8: input_signed = 1; signed_value = tbe_wire_read_i8(source, big_endian); break;
    case TBE_TYPED_U8: unsigned_value = tbe_wire_read_u8(source, big_endian); break;
    case TBE_TYPED_I16: input_signed = 1; signed_value = tbe_wire_read_i16(source, big_endian); break;
    case TBE_TYPED_U16: unsigned_value = tbe_wire_read_u16(source, big_endian); break;
    case TBE_TYPED_I32: input_signed = 1; signed_value = tbe_wire_read_i32(source, big_endian); break;
    case TBE_TYPED_U32: unsigned_value = tbe_wire_read_u32(source, big_endian); break;
    case TBE_TYPED_I64: input_signed = 1; signed_value = tbe_wire_read_i64(source, big_endian); break;
    case TBE_TYPED_U64: unsigned_value = tbe_wire_read_u64(source, big_endian); break;
    default:
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Enum wire kind is not an integer");
    }
    return typed_native_enum_assign_number(data, input_signed, signed_value,
                                            unsigned_value, storage, path, error);
  }
  if (typed_cmeta_scalar_matches(data, &salts_int8_cmeta_data) &&
      wire_kind == TBE_TYPED_I8)
    *(int8_t *)storage = tbe_wire_read_i8(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_uint8_cmeta_data) &&
           wire_kind == TBE_TYPED_U8)
    *(uint8_t *)storage = tbe_wire_read_u8(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_int16_cmeta_data) &&
           wire_kind == TBE_TYPED_I16)
    *(int16_t *)storage = tbe_wire_read_i16(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_uint16_cmeta_data) &&
           wire_kind == TBE_TYPED_U16)
    *(uint16_t *)storage = tbe_wire_read_u16(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_int32_cmeta_data) &&
           wire_kind == TBE_TYPED_I32)
    *(int32_t *)storage = tbe_wire_read_i32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_uint32_cmeta_data) &&
           wire_kind == TBE_TYPED_U32)
    *(uint32_t *)storage = tbe_wire_read_u32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_int64_cmeta_data) &&
           wire_kind == TBE_TYPED_I64)
    *(int64_t *)storage = tbe_wire_read_i64(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &salts_uint64_cmeta_data) &&
           wire_kind == TBE_TYPED_U64)
    *(uint64_t *)storage = tbe_wire_read_u64(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_float) &&
           wire_kind == TBE_TYPED_F32)
    *(float *)storage = tbe_wire_read_f32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_double) &&
           wire_kind == TBE_TYPED_F64)
    *(double *)storage = tbe_wire_read_f64(source, big_endian);
  else
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Wire scalar kind disagrees with canonical CMeta storage");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_read_fixed(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const uint8_t *source, void *storage, const char *path,
    DataBindError *error) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status = typed_native_path(
        field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (native_field->value->kind == CMETA_DATA_STRUCT)
      status = typed_native_read_fixed(
          native_field->value, wire_field->nested_overlay,
          source + wire_field->wire_offset,
          (uint8_t *)storage + native_field->offset, field_path, error);
    else
      status = typed_native_read_wire_scalar(
          native_field->value, wire_field->wire_kind,
          source + wire_field->wire_offset, overlay->wire_big_endian,
          (uint8_t *)storage + native_field->offset, field_path, error);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_write_wire_scalar(
    const cmeta_data_desc *data, TbeTypedKind wire_kind, uint8_t *destination,
    int big_endian, const void *storage, const char *path,
    DataBindError *error) {
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    void *candidate;
    cmeta_status copy_status;
    if (wire_kind != TBE_TYPED_FIXED_BYTES ||
        cmeta_data_fixed_extent(data, &extent) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes wire storage disagrees with canonical CMeta");
    candidate = calloc(1u, extent);
    if (candidate == NULL)
      return typed_error(error, DATA_BIND_ERR_OOM, path,
                         "Out of memory validating fixed bytes storage");
    copy_status = cmeta_data_fixed_copy(data, candidate, storage, extent);
    if (copy_status == CMETA_OK) memcpy(destination, candidate, extent);
    (void)cmeta_data_fixed_restore_zero(data, candidate);
    free(candidate);
    return copy_status == CMETA_OK
               ? DATA_BIND_OK
               : typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                             "Fixed bytes provider rejected native storage");
  }
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) &&
      wire_kind == TBE_TYPED_BOOL) {
    uint8_t candidate = 0u;
    if (cmeta_data_fixed_copy(data, &candidate, storage,
                              data->storage_type->size) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Bool provider rejected native storage");
    tbe_wire_write_u8(destination, big_endian, candidate);
    return DATA_BIND_OK;
  }
  if (salts_uuid_cmeta_data_valid(data) && wire_kind == TBE_TYPED_UUID) {
    salts_uuid_t candidate = { {0} };
    if (cmeta_data_fixed_copy(data, &candidate, storage,
                              data->storage_type->size) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "UUID provider rejected native storage");
    memcpy(destination, candidate.bytes, SALTS_UUID_SIZE);
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    const cmeta_enum_domain *domain = cmeta_data_enum_bits_ops_of(data)->domain;
    uint64_t bits;
    uint64_t wire_mask;
    unsigned wire_bits;
    int wire_signed;
    if (cmeta_data_enum_read_bits(data, storage, &bits) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Canonical enum provider could not read storage");
    switch (wire_kind) {
    case TBE_TYPED_I8: wire_bits = 8u; wire_signed = 1; break;
    case TBE_TYPED_U8: wire_bits = 8u; wire_signed = 0; break;
    case TBE_TYPED_I16: wire_bits = 16u; wire_signed = 1; break;
    case TBE_TYPED_U16: wire_bits = 16u; wire_signed = 0; break;
    case TBE_TYPED_I32: wire_bits = 32u; wire_signed = 1; break;
    case TBE_TYPED_U32: wire_bits = 32u; wire_signed = 0; break;
    case TBE_TYPED_I64: wire_bits = 64u; wire_signed = 1; break;
    case TBE_TYPED_U64: wire_bits = 64u; wire_signed = 0; break;
    default:
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Enum wire kind is not an integer");
    }
    wire_mask = UINT64_MAX >> (64u - wire_bits);
    if (domain->signedness == CMETA_ENUM_SIGNED &&
        (bits & (UINT64_C(1) << (domain->bits - 1u))) != 0u) {
      uint64_t magnitude = (UINT64_MAX >> (64u - domain->bits)) - bits + 1u;
      if (!wire_signed || magnitude > (wire_mask >> 1u) + 1u) goto enum_range;
      bits = ~(magnitude - 1u) & wire_mask;
    } else if (bits > (wire_signed ? wire_mask >> 1u : wire_mask)) {
      goto enum_range;
    }
    switch (wire_bits) {
    case 8u: tbe_wire_write_u8(destination, big_endian, (uint8_t)bits); break;
    case 16u: tbe_wire_write_u16(destination, big_endian, (uint16_t)bits); break;
    case 32u: tbe_wire_write_u32(destination, big_endian, (uint32_t)bits); break;
    case 64u: tbe_wire_write_u64(destination, big_endian, bits); break;
    }
    return DATA_BIND_OK;
enum_range:
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                       "Canonical enum value exceeds its wire storage");
  }
  if (typed_cmeta_scalar_matches(data, &salts_int8_cmeta_data) &&
      wire_kind == TBE_TYPED_I8)
    tbe_wire_write_i8(destination, big_endian, *(const int8_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_uint8_cmeta_data) &&
           wire_kind == TBE_TYPED_U8)
    tbe_wire_write_u8(destination, big_endian, *(const uint8_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_int16_cmeta_data) &&
           wire_kind == TBE_TYPED_I16)
    tbe_wire_write_i16(destination, big_endian, *(const int16_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_uint16_cmeta_data) &&
           wire_kind == TBE_TYPED_U16)
    tbe_wire_write_u16(destination, big_endian, *(const uint16_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_int32_cmeta_data) &&
           wire_kind == TBE_TYPED_I32)
    tbe_wire_write_i32(destination, big_endian, *(const int32_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_uint32_cmeta_data) &&
           wire_kind == TBE_TYPED_U32)
    tbe_wire_write_u32(destination, big_endian, *(const uint32_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_int64_cmeta_data) &&
           wire_kind == TBE_TYPED_I64)
    tbe_wire_write_i64(destination, big_endian, *(const int64_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &salts_uint64_cmeta_data) &&
           wire_kind == TBE_TYPED_U64)
    tbe_wire_write_u64(destination, big_endian, *(const uint64_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_float) &&
           wire_kind == TBE_TYPED_F32)
    tbe_wire_write_f32(destination, big_endian, *(const float *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_double) &&
           wire_kind == TBE_TYPED_F64)
    tbe_wire_write_f64(destination, big_endian, *(const double *)storage);
  else
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Wire scalar kind disagrees with canonical CMeta storage");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_write_fixed(
    const cmeta_data_desc *data, const TbeTypedType *overlay,
    const void *storage, uint8_t *destination, const char *path,
    DataBindError *error) {
  const cmeta_data_struct_shape *shape =
      (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status = typed_native_path(
        field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (native_field->value->kind == CMETA_DATA_STRUCT)
      status = typed_native_write_fixed(
          native_field->value, wire_field->nested_overlay,
          (const uint8_t *)storage + native_field->offset,
          destination + wire_field->wire_offset, field_path, error);
    else
      status = typed_native_write_wire_scalar(
          native_field->value, wire_field->wire_kind,
          destination + wire_field->wire_offset, overlay->wire_big_endian,
          (const uint8_t *)storage + native_field->offset,
          field_path, error);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_descriptor_parse(DataBind *codec, const char *type_name,
                                          const TbeTypedDescriptor *descriptor,
                                          DataBindFormat format, const void *data, size_t len,
                                          size_t row, void *object, DataBindError *error) {
  TypedNativeRecord native;
  json_value_t *json = NULL;
  void *temporary;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (codec == NULL || !typed_nonempty(type_name) || data == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical descriptor parse arguments");
  (void)typed_error(error, DATA_BIND_OK, NULL, NULL);
  status = typed_native_validate_schema_at(codec, type_name, native.data,
                                           native.overlay, 0u, error);
  if (status != DATA_BIND_OK) return status;
  if (format == DATA_BIND_FORMAT_BINARY) {
    status = typed_native_validate_wire(native.data, native.overlay,
                                        type_name, error);
    if (status != DATA_BIND_OK) return status;
    if (len != native.overlay->fixed_block_size)
      return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                         "Binary input size does not match the fixed wire block");
  } else {
    status = typed_text_parse_json(codec, native.data, native.overlay, format,
                                   (const char *)data, len, row, &json,
                                   type_name, error);
  }
  if (status != DATA_BIND_OK) return status;

  temporary = calloc(1u, native.data->storage_type->size);
  if (temporary == NULL) {
    json_free(json);
    return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                       "Out of memory creating canonical native object");
  }
  status = typed_native_init_value(native.data, temporary, type_name, error);
  if (status == DATA_BIND_OK) {
    if (format == DATA_BIND_FORMAT_BINARY)
      status = typed_native_read_fixed(native.data, native.overlay,
                                       (const uint8_t *)data, temporary,
                                       type_name, error);
    else
      status = typed_native_from_json(codec, native.data, native.overlay, json,
                                      temporary, type_name,
                                      format == DATA_BIND_FORMAT_CSV ||
                                          format == DATA_BIND_FORMAT_XML,
                                      error);
  }
  json_free(json);
  if (status == DATA_BIND_OK) {
    memcpy(object, temporary, native.data->storage_type->size);
    status = typed_error(error, DATA_BIND_OK, NULL, NULL);
  }
  (void)typed_native_clear_value(native.data, temporary, type_name, NULL);
  free(temporary);
  return status;
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
  TypedNativeRecord native;
  json_value_t *json;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0u;
  if (codec == NULL || !typed_nonempty(type_name) || object == NULL || out == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical descriptor serialize arguments");
  (void)typed_error(error, DATA_BIND_OK, NULL, NULL);
  if (format != DATA_BIND_FORMAT_JSON && format != DATA_BIND_FORMAT_YAML &&
      format != DATA_BIND_FORMAT_CSV && format != DATA_BIND_FORMAT_XML)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Unknown canonical descriptor output format");
  status = typed_native_validate_schema_at(codec, type_name, native.data,
                                           native.overlay, 0u, error);
  if (status != DATA_BIND_OK) return status;
  json = typed_native_to_json(codec, native.data, native.overlay, object,
                              type_name, error);
  if (json == NULL) {
    if (error != NULL &&
        error->size >= offsetof(DataBindError, code) + sizeof(error->code) &&
        error->code != DATA_BIND_OK)
      return error->code;
    return DATA_BIND_ERR_TYPE_MISMATCH;
  }
  status = typed_json_serialize_text(format, type_name, json, out, out_len,
                                     error);
  json_free(json);
  return status;
}

DataBindStatus tbe_typed_descriptor_serialize_binary(
    const TbeTypedDescriptor *descriptor, const void *object, uint8_t **out,
    size_t *out_len, DataBindError *error) {
  TypedNativeRecord native;
  uint8_t *data;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0u;
  if (object == NULL || out == NULL || out_len == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid canonical binary serialize arguments");
  status = typed_native_validate_wire(native.data, native.overlay,
                                      native.overlay->name, error);
  if (status != DATA_BIND_OK) return status;
  data = (uint8_t *)malloc(native.overlay->fixed_block_size);
  if (data == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, native.overlay->name,
                       "Out of memory serializing canonical binary object");
  status = tbe_typed_descriptor_serialize_binary_into(
      descriptor, object, data, native.overlay->fixed_block_size, out_len,
      error);
  if (status != DATA_BIND_OK) {
    free(data);
    return status;
  }
  *out = data;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_descriptor_serialize_binary_into(
    const TbeTypedDescriptor *descriptor, const void *object, uint8_t *output,
    size_t capacity, size_t *out_len, DataBindError *error) {
  TypedNativeRecord native;
  uint8_t *temporary;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (out_len != NULL) *out_len = 0u;
  if (object == NULL || out_len == NULL || (output == NULL && capacity != 0u))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid canonical binary output arguments");
  status = typed_native_validate_wire(native.data, native.overlay,
                                      native.overlay->name, error);
  if (status != DATA_BIND_OK) return status;
  *out_len = native.overlay->fixed_block_size;
  if (capacity < native.overlay->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_BUFFER_TOO_SMALL,
                       native.overlay->name,
                       "Canonical binary output buffer is too small");
  temporary = (uint8_t *)calloc(1u, native.overlay->fixed_block_size);
  if (temporary == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, native.overlay->name,
                       "Out of memory staging canonical binary output");
  status = typed_native_write_fixed(native.data, native.overlay, object, temporary,
                                    native.overlay->name, error);
  if (status == DATA_BIND_OK) {
    memcpy(output, temporary, native.overlay->fixed_block_size);
    status = typed_error(error, DATA_BIND_OK, NULL, NULL);
  }
  free(temporary);
  return status;
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
