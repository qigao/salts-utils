#include "tbe_typed_internal.h"

#include "data_bind_binary_wire.h"
#include <salts_cmeta_data.h>
#include <tstr.h>

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
typedef union tbe_typed_max_align {
  long double long_double;
  void *pointer;
  long long integer;
} tbe_typed_max_align_t;
  #define TBE_TYPED_MAX_ALIGNOF _Alignof(tbe_typed_max_align_t)
#else
  #define TBE_TYPED_MAX_ALIGNOF _Alignof(max_align_t)
#endif

typedef struct TbeTypedCMetaKindMapping {
  const cmeta_data_desc *data;
  TbeTypedKind kind;
} TbeTypedCMetaKindMapping;

static int typed_cmeta_shape_matches(const cmeta_data_desc *data,
                                     const cmeta_data_desc *canonical) {
  if (data->kind == CMETA_DATA_SINT || data->kind == CMETA_DATA_UINT) {
    const cmeta_data_integer_shape *actual = (const cmeta_data_integer_shape *)data->shape;
    const cmeta_data_integer_shape *expected = (const cmeta_data_integer_shape *)canonical->shape;
    return actual->bits == expected->bits;
  }
  if (data->kind == CMETA_DATA_FLOAT) {
    const cmeta_data_float_shape *actual = (const cmeta_data_float_shape *)data->shape;
    const cmeta_data_float_shape *expected = (const cmeta_data_float_shape *)canonical->shape;
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

static int typed_nullable_is_null(const TbeTypedType *type,
                                  const void *object,
                                  const TbeTypedField *field) {
  const uint8_t *nulls;
  if (type == NULL || object == NULL || field == NULL ||
      (field->flags & TBE_TYPED_FIELD_NULLABLE) == 0u ||
      type->null_size == 0u)
    return 0;
  nulls = (const uint8_t *)object + type->null_offset;
  return (nulls[field->nullable_bit / 8u] &
          (uint8_t)(1u << (field->nullable_bit % 8u))) != 0u;
}

static DataBindStatus typed_init_value(TbeTypedKind kind, const TbeTypedType *object_type,
                                       void *ptr, size_t count, DataBindError *error) {
  size_t i;
  if (kind == TBE_TYPED_STRING) {
    for (i = 0; i < count; ++i)
      ((tstr *)ptr)[i] = NULL;
  } else if (kind == TBE_TYPED_BYTES) {
    for (i = 0; i < count; ++i) {
      if (vec_init_bytes(&((vec_t *)ptr)[i], sizeof(uint8_t), _Alignof(uint8_t), SIZE_MAX) !=
          STL_OK)
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
      status =
          vec_init_bytes((vec_t *)ptr,
                         field->kind == TBE_TYPED_BYTES ? sizeof(uint8_t) : field->element_size,
                         _Alignof(uint8_t), SIZE_MAX) == STL_OK
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
  int descriptor_nullable = (field->flags & TBE_TYPED_FIELD_NULLABLE) != 0;
  int descriptor_offset = (field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) != 0;
  if (field->name == NULL || schema->name == NULL || strcmp(field->name, schema->name) != 0 ||
      descriptor_optional != (schema->is_optional != 0) ||
      descriptor_nullable != (schema->is_nullable != 0) ||
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
  else if (kind == TBE_TYPED_ENUM) *extent = typed_kind_size(typed_enum_storage_kind(wire_kind));
  else *extent = typed_kind_size(kind);
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
  if (!typed_size_fits(type->null_offset, type->null_size, type->size))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed null bitmap exceeds the host object");
  if (typed_ranges_overlap(type->presence_offset, type->presence_size,
                           type->null_offset, type->null_size))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed presence and null bitmaps overlap");
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
    if ((field->flags & TBE_TYPED_FIELD_NULLABLE) != 0 &&
        (type->null_size == 0 || field->nullable_bit / 8u >= type->null_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed nullable bit exceeds the null bitmap");
    if (type->presence_size != 0 &&
        typed_ranges_overlap(field->offset, host_extent, type->presence_offset,
                             type->presence_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field overlaps the presence bitmap");
    if (type->null_size != 0 &&
        typed_ranges_overlap(field->offset, host_extent, type->null_offset,
                             type->null_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed field overlaps the null bitmap");
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

static int typed_nonempty(const char *text) { return text != NULL && text[0] != '\0'; }

static int typed_native_scalar_supported(const cmeta_data_desc *data) {
  const TbeTypedCMetaKindMapping typed_cmeta_kind_mappings[] = {
      {&cmeta_data_bool, TBE_TYPED_BOOL},        {&cmeta_data_int, TBE_TYPED_I32},
      {&cmeta_data_long, TBE_TYPED_I64},         {&cmeta_data_int8, TBE_TYPED_I8},
      {&cmeta_data_uint8, TBE_TYPED_U8},   {&cmeta_data_int16, TBE_TYPED_I16},
      {&cmeta_data_uint16, TBE_TYPED_U16}, {&cmeta_data_int32, TBE_TYPED_I32},
      {&cmeta_data_uint32, TBE_TYPED_U32}, {&cmeta_data_int64, TBE_TYPED_I64},
      {&cmeta_data_uint64, TBE_TYPED_U64}, {&cmeta_data_float, TBE_TYPED_F32},
      {&cmeta_data_double, TBE_TYPED_F64},
  };
  size_t i;
  if (data == NULL) return 0;
  for (i = 0u; i < sizeof(typed_cmeta_kind_mappings) / sizeof(typed_cmeta_kind_mappings[0]); ++i) {
    if (typed_cmeta_scalar_matches(data, typed_cmeta_kind_mappings[i].data)) return 1;
  }
  if (data->kind == CMETA_DATA_ENUM)
    return cmeta_data_enum_bits_ops_of(data) != NULL;
  if ((data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) &&
      cmeta_data_buffer_ops_of(data) != NULL)
    return 1;
  if (cmeta_data_fixed_ops_of(data) == NULL) return 0;
  return typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) ||
         salts_uuid_cmeta_data_valid(data) || data->kind == CMETA_DATA_BYTES;
}

static DataBindStatus typed_native_path(char *out, size_t capacity, const char *parent,
                                        const char *field, DataBindError *error) {
  int written;
  if (out == NULL || capacity == 0u || !typed_nonempty(field))
    return typed_error(error, DATA_BIND_ERR_SCHEMA, parent,
                       "Native CMeta field name is unavailable");
  written = typed_nonempty(parent) ? snprintf(out, capacity, "%s.%s", parent, field)
                                   : snprintf(out, capacity, "%s", field);
  if (written < 0 || (size_t)written >= capacity)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, parent,
                       "Native CMeta field path exceeds the supported length");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_record_preflight(const cmeta_data_desc *data,
                                                    const TbeTypedType *overlay,
                                                    const cmeta_data_desc **ancestors,
                                                    unsigned depth, const char *path,
                                                    TypedNativeRecord *out, DataBindError *error) {
  const size_t required_data_size = offsetof(cmeta_data_desc, shape) + sizeof(data->shape);
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  size_t i;

  if (data == NULL || overlay == NULL || data->struct_size < required_data_size ||
      data->abi_version != CMETA_DATA_DESC_ABI_VERSION || data->kind != CMETA_DATA_STRUCT)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record is unavailable");
  {
    DataBindStatus overlay_status =
        typed_validate_descriptor_at(overlay, depth, error);
    if (overlay_status != DATA_BIND_OK) return overlay_status;
  }
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
      data->storage_type->align > TBE_TYPED_MAX_ALIGNOF)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record storage is invalid");

  shape = (const cmeta_data_struct_shape *)data->shape;
  layout = shape->layout;
  if (layout == NULL || !typed_nonempty(layout->name) || layout->size == 0u ||
      layout->align == 0u || layout->size != data->storage_type->size ||
      layout->align != data->storage_type->align)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Canonical native CMeta record layout is invalid");
  if (shape->field_count != layout->field_count || shape->field_count != overlay->field_count ||
      (shape->field_count != 0u &&
       (shape->fields == NULL || layout->fields == NULL || overlay->fields == NULL)))
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
        typed_native_path(field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (!typed_nonempty(native_field->stable_id) || !typed_nonempty(layout_field->name) ||
        strcmp(layout_field->name, native_field->name) != 0 || value == NULL ||
        value->storage_type == NULL || !cmeta_type_desc_valid(value->storage_type) ||
        layout_field->type == NULL || !cmeta_type_equal(layout_field->type, value->storage_type) ||
        layout_field->size != value->storage_type->size ||
        layout_field->align != value->storage_type->align ||
        layout_field->offset != native_field->offset ||
        !typed_size_fits(native_field->offset, layout_field->size, data->storage_type->size) ||
        layout_field->align == 0u || native_field->offset % layout_field->align != 0u ||
        (wire_field->flags & TBE_TYPED_FIELD_GROUP) != 0u)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Field has no exact canonical native CMeta storage");

    if (value->kind == CMETA_DATA_STRUCT) {
      if (wire_field->nested_overlay == NULL)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Nested Struct has no schema overlay association");
      status = typed_native_record_preflight(value, wire_field->nested_overlay, ancestors,
                                             depth + 1u, field_path, NULL, error);
      if (status != DATA_BIND_OK) return status;
    } else {
      size_t fixed_extent;
      if (!cmeta_data_desc_valid(value) || !typed_native_scalar_supported(value))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Native CMeta field kind is outside this runtime slice");
      if (cmeta_data_fixed_ops_of(value) != NULL &&
          (cmeta_data_fixed_extent(value, &fixed_extent) != CMETA_OK ||
           fixed_extent != value->storage_type->size || layout_field->size != fixed_extent))
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

static DataBindStatus typed_descriptor_native_record(const TbeTypedDescriptor *descriptor,
                                                     TypedNativeRecord *out, DataBindError *error) {
  const size_t required_size =
      offsetof(TbeTypedDescriptor, native_data) + sizeof(descriptor->native_data);
  const cmeta_data_desc *ancestors[TBE_TYPED_NATIVE_MAX_DEPTH + 1u];
  const char *path;
  if (descriptor == NULL || descriptor->struct_size < required_size ||
      descriptor->abi_version != TBE_TYPED_DESCRIPTOR_ABI_VERSION || descriptor->overlay == NULL ||
      descriptor->native_data == NULL)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, NULL,
                       "Invalid or incompatible typed descriptor boundary");
  path = typed_nonempty(descriptor->overlay->name) ? descriptor->overlay->name
                                                   : descriptor->native_data->display_name;
  return typed_native_record_preflight(descriptor->native_data, descriptor->overlay, ancestors, 0u,
                                       path, out, error);
}

static DataBindStatus typed_native_init_value(const cmeta_data_desc *data, void *storage,
                                              const char *path, DataBindError *error) {
  cmeta_status status;
  if (data == NULL || storage == NULL || data->storage_type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                       "Invalid canonical native storage");
  status = cmeta_data_value_init_zero(data, storage);
  if (status == CMETA_OK) return DATA_BIND_OK;
  return typed_error(
      error,
      status == CMETA_OUT_OF_MEMORY ? DATA_BIND_ERR_OOM : DATA_BIND_ERR_SCHEMA,
      path, "Canonical CMeta lifecycle could not initialize semantic zero");
}

static DataBindStatus typed_native_clear_value(const cmeta_data_desc *data, void *storage,
                                               const char *path, DataBindError *error) {
  cmeta_status status;
  if (data == NULL || storage == NULL || data->storage_type == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, path,
                       "Invalid canonical native storage");
  status = cmeta_data_value_restore_zero(data, storage);
  if (status == CMETA_OK) return DATA_BIND_OK;
  return typed_error(
      error,
      status == CMETA_OUT_OF_MEMORY ? DATA_BIND_ERR_OOM : DATA_BIND_ERR_SCHEMA,
      path, "Canonical CMeta lifecycle could not restore semantic zero");
}

/* Numeric wire adaptation only; native storage and membership belong to
 * the canonical provider. Negative canonical bits are width-local. */
static DataBindStatus typed_native_enum_assign_number(const cmeta_data_desc *data, int input_signed,
                                                      int64_t signed_value, uint64_t unsigned_value,
                                                      void *storage, const char *path,
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
  if (cmeta_data_enum_assign_bits(data, storage, bits) == CMETA_OK) return DATA_BIND_OK;
range:
  return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                     "Enum value is outside the canonical CMeta domain");
}

DataBindStatus tbe_typed_descriptor_validate(const TbeTypedDescriptor *descriptor,
                                             DataBindError *error) {
  DataBindStatus status = typed_descriptor_native_record(descriptor, NULL, error);
  if (status != DATA_BIND_OK) return status;
  return typed_error(error, DATA_BIND_OK, NULL, NULL);
}

static DataBindStatus typed_validate_layout_at(const TbeTypedType *type, unsigned depth,
                                               DataBindError *error) {
  size_t i;
  DataBindStatus status = typed_validate_descriptor_at(type, depth, error);
  if (status != DATA_BIND_OK) return status;
  if (type->presence_size > type->fixed_block_size ||
      type->null_size > type->fixed_block_size - type->presence_size)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, type->name,
                       "Typed state bitmaps exceed the fixed wire block");
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
      if (typed_ranges_overlap(field->wire_offset, wire_extent, 0u,
                               type->presence_size + type->null_size))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed field overlaps the wire state bitmaps");
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

static int typed_native_schema_field_matches(DataBind *codec, const cmeta_data_desc *native,
                                             const TbeTypedField *wire,
                                             const DataBindSchemaField *schema) {
  int has_wire_offset;
  int descriptor_optional;
  int descriptor_nullable;
  if (native == NULL || wire == NULL || schema == NULL || !typed_nonempty(wire->name) ||
      !typed_nonempty(schema->name) || strcmp(wire->name, schema->name) != 0 ||
      (wire->flags & TBE_TYPED_FIELD_GROUP) != 0u)
    return 0;
  descriptor_optional = (wire->flags & TBE_TYPED_FIELD_OPTIONAL) != 0u;
  descriptor_nullable = (wire->flags & TBE_TYPED_FIELD_NULLABLE) != 0u;
  if (descriptor_optional != (schema->is_optional != 0) ||
      descriptor_nullable != (schema->is_nullable != 0))
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
           enum_type.kind == (domain->kind == CMETA_ENUM_FLAGS ? DATA_BIND_SCHEMA_FLAGS
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
  if (native->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(native) != NULL) {
    size_t extent;
    return typed_nonempty(schema->type) && strcmp(schema->type, "bytes") == 0 &&
           schema->is_fixed_size && schema->has_size_bytes &&
           cmeta_data_fixed_extent(native, &extent) == CMETA_OK && extent == schema->size_bytes &&
           wire->wire_kind == TBE_TYPED_FIXED_BYTES;
  }
  {
    TbeTypedKind schema_kind;
    return typed_scalar_kind_from_name(schema->type, &schema_kind) &&
           schema_kind == wire->wire_kind;
  }
}

static DataBindStatus typed_native_validate_schema_at(DataBind *codec, const char *type_name,
                                                      const cmeta_data_desc *data,
                                                      const TbeTypedType *overlay, unsigned depth,
                                                      DataBindError *error) {
  const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
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
    int schema_big_endian = byte_order != NULL && strcmp(byte_order, "big") == 0;
    if ((overlay->wire_big_endian != 0) != schema_big_endian)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, type_name,
                         "Schema overlay byte order does not match the schema");
  }
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    DataBindSchemaField schema_field = DATA_BIND_SCHEMA_FIELD_INIT;
    char field_path[sizeof(((DataBindError *)0)->path)];
    status =
        typed_native_path(field_path, sizeof(field_path), type_name, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if (!data_bind_schema_field_at(codec, type_name, i, &schema_field) ||
        !typed_native_schema_field_matches(codec, native_field->value, wire_field, &schema_field))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Canonical native field does not match its schema overlay");
    if (native_field->value->kind == CMETA_DATA_STRUCT) {
      status = typed_native_validate_schema_at(codec, schema_field.type, native_field->value,
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
    *(uint8_t *)output = (uint8_t)(data_bind_binary_wire_read_u8(source, big_endian) != 0);
    break;
  case TBE_TYPED_I8:
    *(int8_t *)output = data_bind_binary_wire_read_i8(source, big_endian);
    break;
  case TBE_TYPED_U8:
    *(uint8_t *)output = data_bind_binary_wire_read_u8(source, big_endian);
    break;
  case TBE_TYPED_I16:
    *(int16_t *)output = data_bind_binary_wire_read_i16(source, big_endian);
    break;
  case TBE_TYPED_U16:
    *(uint16_t *)output = data_bind_binary_wire_read_u16(source, big_endian);
    break;
  case TBE_TYPED_I32:
    *(int32_t *)output = data_bind_binary_wire_read_i32(source, big_endian);
    break;
  case TBE_TYPED_U32:
    *(uint32_t *)output = data_bind_binary_wire_read_u32(source, big_endian);
    break;
  case TBE_TYPED_I64:
    *(int64_t *)output = data_bind_binary_wire_read_i64(source, big_endian);
    break;
  case TBE_TYPED_U64:
    *(uint64_t *)output = data_bind_binary_wire_read_u64(source, big_endian);
    break;
  case TBE_TYPED_F32:
    *(float *)output = data_bind_binary_wire_read_f32(source, big_endian);
    break;
  case TBE_TYPED_F64:
    *(double *)output = data_bind_binary_wire_read_f64(source, big_endian);
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
  if (type->null_size != 0)
    memcpy((uint8_t *)object + type->null_offset,
           data + type->presence_size, type->null_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    uint8_t *output = (uint8_t *)object + field->offset;
    const uint8_t *source;
    size_t j;
    size_t element_wire_size;
    int present;
    int is_null;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    source = data + field->wire_offset;
    present = typed_optional_present(type, object, field);
    is_null = typed_nullable_is_null(type, object, field);
    if (!present) {
      if (is_null)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed null state is set while optional field is absent");
      continue;
    }
    if (is_null) continue;
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
    int is_null = typed_nullable_is_null(type, object, field);
    if (!present && is_null)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                         "Typed null state is set while optional field is absent");
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
      block_length = data_bind_binary_wire_read_u16(data + cursor, type->wire_big_endian);
      count = data_bind_binary_wire_read_u16(data + cursor + 2u, type->wire_big_endian);
      cursor += 4u;
      if (block_length < field->object_type->fixed_block_size ||
          !typed_multiply_fits(count, block_length, &payload_size) ||
          !typed_size_fits(cursor, payload_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary group payload is invalid");
      if (is_null && count != 0u)
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary NULL group payload must be empty");
      if (present && !is_null) {
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
      value_size = data_bind_binary_wire_read_u32(data + cursor, type->wire_big_endian);
      cursor += 4u;
      if (!typed_size_fits(cursor, value_size, len))
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary variable-data payload is truncated");
      if (is_null && value_size != 0u)
        return typed_error(error, DATA_BIND_ERR_PARSE, field->name,
                           "Binary NULL variable-data payload must be empty");
      if (present && !is_null && field->kind == TBE_TYPED_STRING) {
        *(tstr *)output = tstr_dup_len((const char *)data + cursor, value_size);
        if (*(tstr *)output == NULL)
          return typed_error(error, DATA_BIND_ERR_OOM, field->name,
                             "Out of memory copying typed string");
      } else if (present && !is_null) {
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
    status =
        typed_error(error, DATA_BIND_ERR_PARSE, type->name, "Binary input contains trailing bytes");
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

static int typed_native_wire_extent(const cmeta_data_desc *data, const TbeTypedField *wire,
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

static DataBindStatus typed_native_validate_wire(const cmeta_data_desc *data,
                                                 const TbeTypedType *overlay, const char *path,
                                                 DataBindError *error) {
  const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
  DataBindStatus layout_status;
  size_t i;
  if (overlay == NULL || overlay->fixed_block_size == 0u)
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Supported descriptor has no complete fixed wire layout");
  layout_status = typed_validate_layout_at(overlay, 0u, error);
  if (layout_status != DATA_BIND_OK) return layout_status;
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    size_t extent;
    size_t j;
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status =
        typed_native_path(field_path, sizeof(field_path), path, native_field->name, error);
    if (status != DATA_BIND_OK) return status;
    if ((wire_field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0u ||
        !typed_native_wire_extent(native_field->value, wire_field, &extent) ||
        wire_field->wire_size != extent ||
        !typed_size_fits(wire_field->wire_offset, extent, overlay->fixed_block_size))
      return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                         "Schema overlay has no exact fixed wire field");
    for (j = 0u; j < i; ++j) {
      const TbeTypedField *previous = &overlay->fields[j];
      size_t previous_extent;
      if (!typed_native_wire_extent(shape->fields[j].value, previous, &previous_extent) ||
          typed_ranges_overlap(wire_field->wire_offset, extent, previous->wire_offset,
                               previous_extent))
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Schema overlay wire fields overlap");
    }
    if (native_field->value->kind == CMETA_DATA_STRUCT) {
      status = typed_native_validate_wire(native_field->value, wire_field->nested_overlay,
                                          field_path, error);
      if (status != DATA_BIND_OK) return status;
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_read_wire_scalar(const cmeta_data_desc *data,
                                                    TbeTypedKind wire_kind, const uint8_t *source,
                                                    int big_endian, void *storage, const char *path,
                                                    DataBindError *error) {
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    if (wire_kind != TBE_TYPED_FIXED_BYTES || cmeta_data_fixed_extent(data, &extent) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes wire storage disagrees with canonical CMeta");
    if (cmeta_data_fixed_copy(data, storage, source, extent) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                         "Fixed bytes provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) && wire_kind == TBE_TYPED_BOOL) {
    uint8_t candidate = (uint8_t)(data_bind_binary_wire_read_u8(source, big_endian) != 0u);
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path, "Bool provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (salts_uuid_cmeta_data_valid(data) && wire_kind == TBE_TYPED_UUID) {
    salts_uuid_t candidate = {{0}};
    memcpy(candidate.bytes, source, SALTS_UUID_SIZE);
    if (cmeta_data_fixed_copy(data, storage, &candidate, sizeof(candidate)) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path, "UUID provider rejected wire storage");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    int64_t signed_value = 0;
    uint64_t unsigned_value = 0u;
    int input_signed = 0;
    switch (wire_kind) {
    case TBE_TYPED_I8:
      input_signed = 1;
      signed_value = data_bind_binary_wire_read_i8(source, big_endian);
      break;
    case TBE_TYPED_U8:
      unsigned_value = data_bind_binary_wire_read_u8(source, big_endian);
      break;
    case TBE_TYPED_I16:
      input_signed = 1;
      signed_value = data_bind_binary_wire_read_i16(source, big_endian);
      break;
    case TBE_TYPED_U16:
      unsigned_value = data_bind_binary_wire_read_u16(source, big_endian);
      break;
    case TBE_TYPED_I32:
      input_signed = 1;
      signed_value = data_bind_binary_wire_read_i32(source, big_endian);
      break;
    case TBE_TYPED_U32:
      unsigned_value = data_bind_binary_wire_read_u32(source, big_endian);
      break;
    case TBE_TYPED_I64:
      input_signed = 1;
      signed_value = data_bind_binary_wire_read_i64(source, big_endian);
      break;
    case TBE_TYPED_U64:
      unsigned_value = data_bind_binary_wire_read_u64(source, big_endian);
      break;
    default:
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path, "Enum wire kind is not an integer");
    }
    return typed_native_enum_assign_number(data, input_signed, signed_value, unsigned_value,
                                           storage, path, error);
  }
  if (typed_cmeta_scalar_matches(data, &cmeta_data_int8) && wire_kind == TBE_TYPED_I8)
    *(int8_t *)storage = data_bind_binary_wire_read_i8(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint8) && wire_kind == TBE_TYPED_U8)
    *(uint8_t *)storage = data_bind_binary_wire_read_u8(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int16) && wire_kind == TBE_TYPED_I16)
    *(int16_t *)storage = data_bind_binary_wire_read_i16(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint16) && wire_kind == TBE_TYPED_U16)
    *(uint16_t *)storage = data_bind_binary_wire_read_u16(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int32) && wire_kind == TBE_TYPED_I32)
    *(int32_t *)storage = data_bind_binary_wire_read_i32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint32) && wire_kind == TBE_TYPED_U32)
    *(uint32_t *)storage = data_bind_binary_wire_read_u32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int64) && wire_kind == TBE_TYPED_I64)
    *(int64_t *)storage = data_bind_binary_wire_read_i64(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint64) && wire_kind == TBE_TYPED_U64)
    *(uint64_t *)storage = data_bind_binary_wire_read_u64(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_float) && wire_kind == TBE_TYPED_F32)
    *(float *)storage = data_bind_binary_wire_read_f32(source, big_endian);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_double) && wire_kind == TBE_TYPED_F64)
    *(double *)storage = data_bind_binary_wire_read_f64(source, big_endian);
  else
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Wire scalar kind disagrees with canonical CMeta storage");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_read_fixed(const cmeta_data_desc *data,
                                              const TbeTypedType *overlay, const uint8_t *source,
                                              void *storage, const char *path,
                                              DataBindError *error) {
  const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  if (overlay->presence_size != 0u)
    memcpy((uint8_t *)storage + overlay->presence_offset,
           source, overlay->presence_size);
  if (overlay->null_size != 0u)
    memcpy((uint8_t *)storage + overlay->null_offset,
           source + overlay->presence_size, overlay->null_size);
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status =
        typed_native_path(field_path, sizeof(field_path), path, native_field->name, error);
    const int present = typed_optional_present(overlay, storage, wire_field);
    const int is_null = typed_nullable_is_null(overlay, storage, wire_field);
    if (status != DATA_BIND_OK) return status;
    if (!present) {
      if (is_null)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Canonical null state is set while optional field is absent");
      continue;
    }
    if (is_null) continue;
    if (native_field->value->kind == CMETA_DATA_STRUCT)
      status = typed_native_read_fixed(
          native_field->value, wire_field->nested_overlay, source + wire_field->wire_offset,
          (uint8_t *)storage + native_field->offset, field_path, error);
    else
      status = typed_native_read_wire_scalar(
          native_field->value, wire_field->wire_kind, source + wire_field->wire_offset,
          overlay->wire_big_endian, (uint8_t *)storage + native_field->offset, field_path, error);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_write_wire_scalar(const cmeta_data_desc *data,
                                                     TbeTypedKind wire_kind, uint8_t *destination,
                                                     int big_endian, const void *storage,
                                                     const char *path, DataBindError *error) {
  if (data->kind == CMETA_DATA_BYTES && cmeta_data_fixed_ops_of(data) != NULL) {
    size_t extent;
    void *candidate;
    cmeta_status copy_status;
    if (wire_kind != TBE_TYPED_FIXED_BYTES || cmeta_data_fixed_extent(data, &extent) != CMETA_OK)
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
    return copy_status == CMETA_OK ? DATA_BIND_OK
                                   : typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                                                 "Fixed bytes provider rejected native storage");
  }
  if (typed_cmeta_scalar_matches(data, &salts_bool8_cmeta_data) && wire_kind == TBE_TYPED_BOOL) {
    uint8_t candidate = 0u;
    if (cmeta_data_fixed_copy(data, &candidate, storage, data->storage_type->size) != CMETA_OK)
      return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                         "Bool provider rejected native storage");
    data_bind_binary_wire_write_u8(destination, big_endian, candidate);
    return DATA_BIND_OK;
  }
  if (salts_uuid_cmeta_data_valid(data) && wire_kind == TBE_TYPED_UUID) {
    salts_uuid_t candidate = {{0}};
    if (cmeta_data_fixed_copy(data, &candidate, storage, data->storage_type->size) != CMETA_OK)
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
    case TBE_TYPED_I8:
      wire_bits = 8u;
      wire_signed = 1;
      break;
    case TBE_TYPED_U8:
      wire_bits = 8u;
      wire_signed = 0;
      break;
    case TBE_TYPED_I16:
      wire_bits = 16u;
      wire_signed = 1;
      break;
    case TBE_TYPED_U16:
      wire_bits = 16u;
      wire_signed = 0;
      break;
    case TBE_TYPED_I32:
      wire_bits = 32u;
      wire_signed = 1;
      break;
    case TBE_TYPED_U32:
      wire_bits = 32u;
      wire_signed = 0;
      break;
    case TBE_TYPED_I64:
      wire_bits = 64u;
      wire_signed = 1;
      break;
    case TBE_TYPED_U64:
      wire_bits = 64u;
      wire_signed = 0;
      break;
    default:
      return typed_error(error, DATA_BIND_ERR_SCHEMA, path, "Enum wire kind is not an integer");
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
    case 8u:
      data_bind_binary_wire_write_u8(destination, big_endian, (uint8_t)bits);
      break;
    case 16u:
      data_bind_binary_wire_write_u16(destination, big_endian, (uint16_t)bits);
      break;
    case 32u:
      data_bind_binary_wire_write_u32(destination, big_endian, (uint32_t)bits);
      break;
    case 64u:
      data_bind_binary_wire_write_u64(destination, big_endian, bits);
      break;
    }
    return DATA_BIND_OK;
  enum_range:
    return typed_error(error, DATA_BIND_ERR_TYPE_MISMATCH, path,
                       "Canonical enum value exceeds its wire storage");
  }
  if (typed_cmeta_scalar_matches(data, &cmeta_data_int8) && wire_kind == TBE_TYPED_I8)
    data_bind_binary_wire_write_i8(destination, big_endian, *(const int8_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint8) && wire_kind == TBE_TYPED_U8)
    data_bind_binary_wire_write_u8(destination, big_endian, *(const uint8_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int16) && wire_kind == TBE_TYPED_I16)
    data_bind_binary_wire_write_i16(destination, big_endian, *(const int16_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint16) && wire_kind == TBE_TYPED_U16)
    data_bind_binary_wire_write_u16(destination, big_endian, *(const uint16_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int32) && wire_kind == TBE_TYPED_I32)
    data_bind_binary_wire_write_i32(destination, big_endian, *(const int32_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint32) && wire_kind == TBE_TYPED_U32)
    data_bind_binary_wire_write_u32(destination, big_endian, *(const uint32_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_int64) && wire_kind == TBE_TYPED_I64)
    data_bind_binary_wire_write_i64(destination, big_endian, *(const int64_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_uint64) && wire_kind == TBE_TYPED_U64)
    data_bind_binary_wire_write_u64(destination, big_endian, *(const uint64_t *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_float) && wire_kind == TBE_TYPED_F32)
    data_bind_binary_wire_write_f32(destination, big_endian, *(const float *)storage);
  else if (typed_cmeta_scalar_matches(data, &cmeta_data_double) && wire_kind == TBE_TYPED_F64)
    data_bind_binary_wire_write_f64(destination, big_endian, *(const double *)storage);
  else
    return typed_error(error, DATA_BIND_ERR_SCHEMA, path,
                       "Wire scalar kind disagrees with canonical CMeta storage");
  return DATA_BIND_OK;
}

static DataBindStatus typed_native_write_fixed(const cmeta_data_desc *data,
                                               const TbeTypedType *overlay, const void *storage,
                                               uint8_t *destination, const char *path,
                                               DataBindError *error) {
  const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
  size_t i;
  if (overlay->presence_size != 0u)
    memcpy(destination, (const uint8_t *)storage + overlay->presence_offset,
           overlay->presence_size);
  if (overlay->null_size != 0u)
    memcpy(destination + overlay->presence_size,
           (const uint8_t *)storage + overlay->null_offset,
           overlay->null_size);
  for (i = 0u; i < shape->field_count; ++i) {
    const cmeta_data_field_desc *native_field = &shape->fields[i];
    const TbeTypedField *wire_field = &overlay->fields[i];
    char field_path[sizeof(((DataBindError *)0)->path)];
    DataBindStatus status =
        typed_native_path(field_path, sizeof(field_path), path, native_field->name, error);
    const int present = typed_optional_present(overlay, storage, wire_field);
    const int is_null = typed_nullable_is_null(overlay, storage, wire_field);
    if (status != DATA_BIND_OK) return status;
    if (!present) {
      if (is_null)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field_path,
                           "Canonical null state is set while optional field is absent");
      continue;
    }
    if (is_null) continue;
    if (native_field->value->kind == CMETA_DATA_STRUCT)
      status = typed_native_write_fixed(native_field->value, wire_field->nested_overlay,
                                        (const uint8_t *)storage + native_field->offset,
                                        destination + wire_field->wire_offset, field_path, error);
    else
      status = typed_native_write_wire_scalar(
          native_field->value, wire_field->wire_kind, destination + wire_field->wire_offset,
          overlay->wire_big_endian, (const uint8_t *)storage + native_field->offset, field_path,
          error);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_descriptor_parse_binary(
    DataBind *codec, const char *type_name, const TbeTypedDescriptor *descriptor,
    const void *data, size_t len, void *object, DataBindError *error) {
  TypedNativeRecord native;
  void *temporary;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (codec == NULL || !typed_nonempty(type_name) || data == NULL || object == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, type_name,
                       "Invalid canonical descriptor parse arguments");
  (void)typed_error(error, DATA_BIND_OK, NULL, NULL);
  status =
      typed_native_validate_schema_at(codec, type_name, native.data, native.overlay, 0u, error);
  if (status != DATA_BIND_OK) return status;
  status = typed_native_validate_wire(native.data, native.overlay, type_name, error);
  if (status != DATA_BIND_OK) return status;
  if (len != native.overlay->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_PARSE, type_name,
                       "Binary input size does not match the fixed wire block");

  temporary = calloc(1u, native.data->storage_type->size);
  if (temporary == NULL) {
    return typed_error(error, DATA_BIND_ERR_OOM, type_name,
                       "Out of memory creating canonical native object");
  }
  status = typed_native_init_value(native.data, temporary, type_name, error);
  if (status == DATA_BIND_OK)
    status = typed_native_read_fixed(native.data, native.overlay, (const uint8_t *)data,
                                     temporary, type_name, error);
  if (status == DATA_BIND_OK) {
    memcpy(object, temporary, native.data->storage_type->size);
    status = typed_error(error, DATA_BIND_OK, NULL, NULL);
  }
  (void)typed_native_clear_value(native.data, temporary, type_name, NULL);
  free(temporary);
  return status;
}

DataBindStatus tbe_typed_descriptor_serialize_binary(const TbeTypedDescriptor *descriptor,
                                                     const void *object, uint8_t **out,
                                                     size_t *out_len, DataBindError *error) {
  TypedNativeRecord native;
  uint8_t *data;
  size_t written = 0u;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (out != NULL) *out = NULL;
  if (out_len != NULL) *out_len = 0u;
  if (object == NULL || out == NULL || out_len == NULL)
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid canonical binary serialize arguments");
  status = typed_native_validate_wire(native.data, native.overlay, native.overlay->name, error);
  if (status != DATA_BIND_OK) return status;
  data = (uint8_t *)malloc(native.overlay->fixed_block_size);
  if (data == NULL)
    return typed_error(error, DATA_BIND_ERR_OOM, native.overlay->name,
                       "Out of memory serializing canonical binary object");
  status = tbe_typed_descriptor_serialize_binary_into(
      descriptor, object, data, native.overlay->fixed_block_size, &written, error);
  if (status != DATA_BIND_OK) {
    free(data);
    return status;
  }
  *out = data;
  *out_len = written;
  return DATA_BIND_OK;
}

DataBindStatus tbe_typed_descriptor_serialize_binary_into(const TbeTypedDescriptor *descriptor,
                                                          const void *object, uint8_t *output,
                                                          size_t capacity, size_t *out_len,
                                                          DataBindError *error) {
  TypedNativeRecord native;
  uint8_t *temporary;
  DataBindStatus status = typed_descriptor_native_record(descriptor, &native, error);
  if (status != DATA_BIND_OK) return status;
  if (out_len != NULL) *out_len = 0u;
  if (object == NULL || out_len == NULL || (output == NULL && capacity != 0u))
    return typed_error(error, DATA_BIND_ERR_INVALID_ARG, native.overlay->name,
                       "Invalid canonical binary output arguments");
  status = typed_native_validate_wire(native.data, native.overlay, native.overlay->name, error);
  if (status != DATA_BIND_OK) return status;
  *out_len = native.overlay->fixed_block_size;
  if (capacity < native.overlay->fixed_block_size)
    return typed_error(error, DATA_BIND_ERR_BUFFER_TOO_SMALL, native.overlay->name,
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
    const int present = typed_optional_present(type, object, field);
    const int is_null = typed_nullable_is_null(type, object, field);
    if (!present && is_null) {
      *supported = 0;
      return 0;
    }
    if ((field->flags & TBE_TYPED_FIELD_GROUP) != 0) {
      const vec_t *vec = (const vec_t *)ptr;
      size_t count = present && !is_null ? vec->size : 0u;
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
      if (present && !is_null) {
        len = field->kind == TBE_TYPED_STRING
                  ? (*(const tstr *)ptr ? tstr_len(*(const tstr *)ptr) : 0)
                  : ((const vec_t *)ptr)->size;
      }
      if ((field->kind == TBE_TYPED_BYTES && len != 0 && ((const vec_t *)ptr)->data == NULL) ||
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
    data_bind_binary_wire_write_u8(dst, big_endian, *(const uint8_t *)src);
    break;
  case TBE_TYPED_I8:
    data_bind_binary_wire_write_i8(dst, big_endian, *(const int8_t *)src);
    break;
  case TBE_TYPED_U16:
    data_bind_binary_wire_write_u16(dst, big_endian, *(const uint16_t *)src);
    break;
  case TBE_TYPED_I16:
    data_bind_binary_wire_write_i16(dst, big_endian, *(const int16_t *)src);
    break;
  case TBE_TYPED_U32:
    data_bind_binary_wire_write_u32(dst, big_endian, *(const uint32_t *)src);
    break;
  case TBE_TYPED_I32:
  case TBE_TYPED_ENUM:
    data_bind_binary_wire_write_i32(dst, big_endian, *(const int32_t *)src);
    break;
  case TBE_TYPED_U64:
    data_bind_binary_wire_write_u64(dst, big_endian, *(const uint64_t *)src);
    break;
  case TBE_TYPED_I64:
    data_bind_binary_wire_write_i64(dst, big_endian, *(const int64_t *)src);
    break;
  case TBE_TYPED_F32:
    data_bind_binary_wire_write_f32(dst, big_endian, *(const float *)src);
    break;
  case TBE_TYPED_F64:
    data_bind_binary_wire_write_f64(dst, big_endian, *(const double *)src);
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
  if (type->null_size != 0)
    memcpy(dst + type->presence_size,
           (const uint8_t *)object + type->null_offset, type->null_size);
  for (i = 0; i < type->field_count; ++i) {
    const TbeTypedField *field = &type->fields[i];
    const uint8_t *src = (const uint8_t *)object + field->offset;
    size_t j;
    const int present = typed_optional_present(type, object, field);
    const int is_null = typed_nullable_is_null(type, object, field);
    if (!present && is_null) return 0;
    if ((field->flags & TBE_TYPED_FIELD_WIRE_OFFSET) == 0) continue;
    if (!present || is_null) continue;
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
      const int present = typed_optional_present(type, object, field);
      const int is_null = typed_nullable_is_null(type, object, field);
      size_t count;
      size_t j;
      if (!present && is_null)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed null state is set while optional field is absent");
      count = present && !is_null ? vec->size : 0u;
      data_bind_binary_wire_write_u16(output + cursor, type->wire_big_endian,
                         (uint16_t)field->object_type->fixed_block_size);
      data_bind_binary_wire_write_u16(output + cursor + 2u, type->wire_big_endian, (uint16_t)count);
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
      const int present = typed_optional_present(type, object, field);
      const int is_null = typed_nullable_is_null(type, object, field);
      if (!present && is_null)
        return typed_error(error, DATA_BIND_ERR_SCHEMA, field->name,
                           "Typed null state is set while optional field is absent");
      if (!present || is_null) {
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
      data_bind_binary_wire_write_u32(output + cursor, type->wire_big_endian, (uint32_t)len);
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
