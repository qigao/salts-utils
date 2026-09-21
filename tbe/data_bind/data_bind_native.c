#include "data_bind_native.h"

#include <salts_cmeta_data.h>

#include <float.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct NativeArena {
  unsigned char *base;
  size_t size;
  size_t offset;
} NativeArena;

typedef struct NativePlan {
  const DataBindNativeOptions *options;
  DataBindNativeDiagnostic *diagnostic;
  const cmeta_data_desc **ancestors;
  size_t nodes;
  size_t seen_peak;
} NativePlan;

typedef struct NativeDecode {
  const DataBindNativeOptions *options;
  DataBindNativeDiagnostic *diagnostic;
  cserde_reader *reader;
  size_t items;
  size_t owned_bytes;
} NativeDecode;

static void native_copy_text(char *destination, size_t capacity, const char *source) {
  size_t length;
  if (destination == NULL || capacity == 0u) return;
  if (source == NULL) {
    destination[0] = '\0';
    return;
  }
  length = strlen(source);
  if (length >= capacity) length = capacity - 1u;
  if (length != 0u) memcpy(destination, source, length);
  destination[length] = '\0';
}

static void native_reset_diagnostic(DataBindNativeDiagnostic *diagnostic) {
  if (diagnostic == NULL) return;
  diagnostic->error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic->source_status = CSERDE_OK;
}

static DataBindStatus native_fail(DataBindNativeDiagnostic *diagnostic,
                                  DataBindStatus status, cserde_status source_status,
                                  const char *path, const char *message) {
  if (diagnostic != NULL) {
    diagnostic->error.code = status;
    diagnostic->source_status = source_status;
    native_copy_text(diagnostic->error.path, sizeof(diagnostic->error.path), path);
    native_copy_text(diagnostic->error.message, sizeof(diagnostic->error.message), message);
  }
  return status;
}

static int native_size_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || right > SIZE_MAX - left) return 0;
  *out = left + right;
  return 1;
}

static int native_size_mul(size_t left, size_t right, size_t *out) {
  if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return 0;
  *out = left * right;
  return 1;
}

static int native_range_valid(const void *pointer, size_t size,
                              uintptr_t *begin, uintptr_t *end) {
  uintptr_t start;
  if (pointer == NULL && size != 0u) return 0;
  start = (uintptr_t)pointer;
  if (size > UINTPTR_MAX - start) return 0;
  if (begin != NULL) *begin = start;
  if (end != NULL) *end = start + size;
  return 1;
}

static int native_ranges_overlap(const void *left, size_t left_size,
                                 const void *right, size_t right_size) {
  uintptr_t left_begin, left_end, right_begin, right_end;
  if (!native_range_valid(left, left_size, &left_begin, &left_end) ||
      !native_range_valid(right, right_size, &right_begin, &right_end))
    return 1;
  if (left_size == 0u || right_size == 0u) return 0;
  return left_begin < right_end && right_begin < left_end;
}

static void *native_arena_alloc(NativeArena *arena, size_t size, size_t alignment) {
  uintptr_t address;
  size_t padding;
  size_t required;
  if (arena == NULL || arena->base == NULL || alignment == 0u) return NULL;
  if (arena->offset > arena->size) return NULL;
  address = (uintptr_t)(arena->base + arena->offset);
  padding = (size_t)(address % alignment);
  if (padding != 0u) padding = alignment - padding;
  if (!native_size_add(padding, size, &required) ||
      required > arena->size - arena->offset)
    return NULL;
  arena->offset += padding;
  address = (uintptr_t)(arena->base + arena->offset);
  arena->offset += size;
  return (void *)address;
}

static int native_path_join(char *out, size_t capacity,
                            const char *parent, const char *field) {
  size_t parent_length;
  size_t field_length;
  size_t required;
  if (out == NULL || capacity == 0u || field == NULL || field[0] == '\0') return 0;
  parent_length = parent == NULL ? 0u : strlen(parent);
  field_length = strlen(field);
  if (!native_size_add(parent_length, field_length, &required) ||
      (parent_length != 0u && !native_size_add(required, 1u, &required)) ||
      !native_size_add(required, 1u, &required) || required > capacity)
    return 0;
  if (parent_length != 0u) {
    memcpy(out, parent, parent_length);
    out[parent_length] = '.';
    memcpy(out + parent_length + 1u, field, field_length);
    out[parent_length + 1u + field_length] = '\0';
  } else {
    memcpy(out, field, field_length);
    out[field_length] = '\0';
  }
  return 1;
}

static int native_data_matches(const cmeta_data_desc *data,
                               const cmeta_data_desc *canonical) {
  if (data == NULL || canonical == NULL || data->kind != canonical->kind ||
      data->storage_type == NULL || canonical->storage_type == NULL ||
      data->storage_type->kind != canonical->storage_type->kind ||
      data->storage_type->size != canonical->storage_type->size ||
      data->storage_type->align != canonical->storage_type->align ||
      !cmeta_type_equal(data->storage_type, canonical->storage_type))
    return 0;

  if (data->kind == CMETA_DATA_SINT || data->kind == CMETA_DATA_UINT) {
    const cmeta_data_integer_shape *actual =
        (const cmeta_data_integer_shape *)data->shape;
    const cmeta_data_integer_shape *expected =
        (const cmeta_data_integer_shape *)canonical->shape;
    return actual != NULL && expected != NULL && actual->bits == expected->bits;
  }
  if (data->kind == CMETA_DATA_FLOAT) {
    const cmeta_data_float_shape *actual =
        (const cmeta_data_float_shape *)data->shape;
    const cmeta_data_float_shape *expected =
        (const cmeta_data_float_shape *)canonical->shape;
    return actual != NULL && expected != NULL && actual->bits == expected->bits;
  }
  return 1;
}

static int native_scalar_supported(const cmeta_data_desc *data) {
  if (data == NULL) return 0;
  switch (data->kind) {
    case CMETA_DATA_BOOL:
      return native_data_matches(data, &cmeta_data_bool) ||
             native_data_matches(data, &salts_bool8_cmeta_data);
    case CMETA_DATA_SINT:
      return native_data_matches(data, &cmeta_data_int) ||
             native_data_matches(data, &cmeta_data_long) ||
             native_data_matches(data, &salts_int8_cmeta_data) ||
             native_data_matches(data, &salts_int16_cmeta_data) ||
             native_data_matches(data, &salts_int32_cmeta_data) ||
             native_data_matches(data, &salts_int64_cmeta_data);
    case CMETA_DATA_UINT:
      return native_data_matches(data, &cmeta_data_size) ||
             native_data_matches(data, &salts_uint8_cmeta_data) ||
             native_data_matches(data, &salts_uint16_cmeta_data) ||
             native_data_matches(data, &salts_uint32_cmeta_data) ||
             native_data_matches(data, &salts_uint64_cmeta_data);
    case CMETA_DATA_FLOAT:
      return native_data_matches(data, &cmeta_data_float) ||
             native_data_matches(data, &cmeta_data_double);
    default:
      return 0;
  }
}

static int native_scalar_zero(const cmeta_data_desc *data, void *storage) {
  if (native_data_matches(data, &cmeta_data_bool)) {
    *(bool *)storage = false;
  } else if (native_data_matches(data, &salts_bool8_cmeta_data)) {
    *(uint8_t *)storage = 0u;
  } else if (native_data_matches(data, &cmeta_data_int)) {
    *(int *)storage = 0;
  } else if (native_data_matches(data, &cmeta_data_long)) {
    *(long *)storage = 0L;
  } else if (native_data_matches(data, &cmeta_data_size)) {
    *(size_t *)storage = 0u;
  } else if (native_data_matches(data, &salts_int8_cmeta_data)) {
    *(int8_t *)storage = INT8_C(0);
  } else if (native_data_matches(data, &salts_int16_cmeta_data)) {
    *(int16_t *)storage = INT16_C(0);
  } else if (native_data_matches(data, &salts_int32_cmeta_data)) {
    *(int32_t *)storage = INT32_C(0);
  } else if (native_data_matches(data, &salts_int64_cmeta_data)) {
    *(int64_t *)storage = INT64_C(0);
  } else if (native_data_matches(data, &salts_uint8_cmeta_data)) {
    *(uint8_t *)storage = UINT8_C(0);
  } else if (native_data_matches(data, &salts_uint16_cmeta_data)) {
    *(uint16_t *)storage = UINT16_C(0);
  } else if (native_data_matches(data, &salts_uint32_cmeta_data)) {
    *(uint32_t *)storage = UINT32_C(0);
  } else if (native_data_matches(data, &salts_uint64_cmeta_data)) {
    *(uint64_t *)storage = UINT64_C(0);
  } else if (native_data_matches(data, &cmeta_data_float)) {
    *(float *)storage = 0.0f;
  } else if (native_data_matches(data, &cmeta_data_double)) {
    *(double *)storage = 0.0;
  } else {
    return 0;
  }
  return 1;
}

static int native_scalar_is_zero(const cmeta_data_desc *data, const void *storage) {
  if (native_data_matches(data, &cmeta_data_bool))
    return *(const bool *)storage == false;
  if (native_data_matches(data, &salts_bool8_cmeta_data))
    return *(const uint8_t *)storage == 0u;
  if (native_data_matches(data, &cmeta_data_int))
    return *(const int *)storage == 0;
  if (native_data_matches(data, &cmeta_data_long))
    return *(const long *)storage == 0L;
  if (native_data_matches(data, &cmeta_data_size))
    return *(const size_t *)storage == 0u;
  if (native_data_matches(data, &salts_int8_cmeta_data))
    return *(const int8_t *)storage == INT8_C(0);
  if (native_data_matches(data, &salts_int16_cmeta_data))
    return *(const int16_t *)storage == INT16_C(0);
  if (native_data_matches(data, &salts_int32_cmeta_data))
    return *(const int32_t *)storage == INT32_C(0);
  if (native_data_matches(data, &salts_int64_cmeta_data))
    return *(const int64_t *)storage == INT64_C(0);
  if (native_data_matches(data, &salts_uint8_cmeta_data))
    return *(const uint8_t *)storage == UINT8_C(0);
  if (native_data_matches(data, &salts_uint16_cmeta_data))
    return *(const uint16_t *)storage == UINT16_C(0);
  if (native_data_matches(data, &salts_uint32_cmeta_data))
    return *(const uint32_t *)storage == UINT32_C(0);
  if (native_data_matches(data, &salts_uint64_cmeta_data))
    return *(const uint64_t *)storage == UINT64_C(0);
  if (native_data_matches(data, &cmeta_data_float))
    return *(const float *)storage == 0.0f;
  if (native_data_matches(data, &cmeta_data_double))
    return *(const double *)storage == 0.0;
  return 0;
}

static int native_signed_token(const cserde_token *token, int64_t *out) {
  if (token->kind == CSERDE_SINT) {
    *out = token->value.sint;
    return 1;
  }
  if (token->kind == CSERDE_UINT && token->value.uint <= (uint64_t)INT64_MAX) {
    *out = (int64_t)token->value.uint;
    return 1;
  }
  return 0;
}

static int native_unsigned_token(const cserde_token *token, uint64_t *out) {
  if (token->kind == CSERDE_UINT) {
    *out = token->value.uint;
    return 1;
  }
  if (token->kind == CSERDE_SINT && token->value.sint >= 0) {
    *out = (uint64_t)token->value.sint;
    return 1;
  }
  return 0;
}

static DataBindStatus native_assign_scalar(DataBindNativeDiagnostic *diagnostic,
                                           const cmeta_data_desc *data,
                                           const cserde_token *token,
                                           void *storage, const char *path) {
  int64_t signed_value;
  uint64_t unsigned_value;
  if (native_data_matches(data, &cmeta_data_bool)) {
    if (token->kind != CSERDE_BOOL)
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Expected Boolean token");
    *(bool *)storage = token->value.boolean;
    return DATA_BIND_OK;
  }
  if (native_data_matches(data, &salts_bool8_cmeta_data)) {
    if (token->kind != CSERDE_BOOL)
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Expected Boolean token");
    *(uint8_t *)storage = token->value.boolean ? 1u : 0u;
    return DATA_BIND_OK;
  }

  if (data->kind == CMETA_DATA_SINT) {
    if (!native_signed_token(token, &signed_value))
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Expected signed integer value");
    if (native_data_matches(data, &cmeta_data_int)) {
      if (signed_value < (int64_t)INT_MIN || signed_value > (int64_t)INT_MAX) goto range;
      *(int *)storage = (int)signed_value;
    } else if (native_data_matches(data, &cmeta_data_long)) {
      if (signed_value < (int64_t)LONG_MIN || signed_value > (int64_t)LONG_MAX) goto range;
      *(long *)storage = (long)signed_value;
    } else if (native_data_matches(data, &salts_int8_cmeta_data)) {
      if (signed_value < INT8_MIN || signed_value > INT8_MAX) goto range;
      *(int8_t *)storage = (int8_t)signed_value;
    } else if (native_data_matches(data, &salts_int16_cmeta_data)) {
      if (signed_value < INT16_MIN || signed_value > INT16_MAX) goto range;
      *(int16_t *)storage = (int16_t)signed_value;
    } else if (native_data_matches(data, &salts_int32_cmeta_data)) {
      if (signed_value < INT32_MIN || signed_value > INT32_MAX) goto range;
      *(int32_t *)storage = (int32_t)signed_value;
    } else if (native_data_matches(data, &salts_int64_cmeta_data)) {
      *(int64_t *)storage = signed_value;
    } else {
      return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Unsupported signed native storage identity");
    }
    return DATA_BIND_OK;
  }

  if (data->kind == CMETA_DATA_UINT) {
    if (!native_unsigned_token(token, &unsigned_value))
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Expected unsigned integer value");
    if (native_data_matches(data, &cmeta_data_size)) {
      if (unsigned_value > (uint64_t)SIZE_MAX) goto range;
      *(size_t *)storage = (size_t)unsigned_value;
    } else if (native_data_matches(data, &salts_uint8_cmeta_data)) {
      if (unsigned_value > UINT8_MAX) goto range;
      *(uint8_t *)storage = (uint8_t)unsigned_value;
    } else if (native_data_matches(data, &salts_uint16_cmeta_data)) {
      if (unsigned_value > UINT16_MAX) goto range;
      *(uint16_t *)storage = (uint16_t)unsigned_value;
    } else if (native_data_matches(data, &salts_uint32_cmeta_data)) {
      if (unsigned_value > UINT32_MAX) goto range;
      *(uint32_t *)storage = (uint32_t)unsigned_value;
    } else if (native_data_matches(data, &salts_uint64_cmeta_data)) {
      *(uint64_t *)storage = unsigned_value;
    } else {
      return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Unsupported unsigned native storage identity");
    }
    return DATA_BIND_OK;
  }

  if (native_data_matches(data, &cmeta_data_float)) {
    if (token->kind != CSERDE_FLOAT ||
        token->value.floating > (double)FLT_MAX ||
        token->value.floating < -(double)FLT_MAX)
      goto range;
    *(float *)storage = (float)token->value.floating;
    return DATA_BIND_OK;
  }
  if (native_data_matches(data, &cmeta_data_double)) {
    if (token->kind != CSERDE_FLOAT)
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Expected floating-point token");
    *(double *)storage = token->value.floating;
    return DATA_BIND_OK;
  }

  return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                     "Unsupported native scalar descriptor");

range:
  return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                     "Numeric value is outside native storage range");
}

static DataBindStatus native_preflight(NativePlan *plan, const cmeta_data_desc *data,
                                       size_t depth, size_t active_seen,
                                       const char *path) {
  size_t i;
  if (depth == 0u || depth > plan->options->max_depth)
    return native_fail(plan->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                       "Native descriptor depth exceeds configured limit");
  if (plan->nodes == plan->options->max_items)
    return native_fail(plan->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                       "Native descriptor node count exceeds configured limit");
  ++plan->nodes;
  if (!cmeta_data_desc_valid(data))
    return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                       "Invalid canonical CMeta descriptor");

  for (i = 0u; i + 1u < depth; ++i) {
    if (plan->ancestors[i] == data)
      return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Canonical native descriptor graph contains a cycle");
  }
  plan->ancestors[depth - 1u] = data;

  if (native_scalar_supported(data)) return DATA_BIND_OK;

  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) {
    const cmeta_data_buffer_ops *ops = cmeta_data_buffer_ops_of(data);
    if (ops == NULL || ops->ownership != CMETA_DATA_BUFFER_OWNED)
      return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Direct native reader requires an owned v2 buffer provider");
    return DATA_BIND_OK;
  }

  if (data->kind == CMETA_DATA_ENUM) {
    if (cmeta_data_enum_bits_ops_of(data) == NULL)
      return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Direct native reader requires canonical enum-bits provider");
    return DATA_BIND_OK;
  }

  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    const cmeta_struct_desc *layout = shape == NULL ? NULL : shape->layout;
    size_t seen_here;
    if (shape == NULL || layout == NULL || data->storage_type == NULL ||
        layout->size != data->storage_type->size ||
        layout->align != data->storage_type->align ||
        shape->field_count != layout->field_count)
      return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Struct descriptor does not exactly describe native storage");
    if (!native_size_add(active_seen, shape->field_count, &seen_here))
      return native_fail(plan->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                         "Struct field workspace size overflow");
    if (seen_here > plan->seen_peak) plan->seen_peak = seen_here;

    for (i = 0u; i < shape->field_count; ++i) {
      const cmeta_data_field_desc *field = &shape->fields[i];
      const cmeta_field_desc *layout_field;
      char child_path[sizeof(((DataBindError *)0)->path)];
      size_t end;
      size_t j;
      if (!native_path_join(child_path, sizeof(child_path), path, field->name))
        return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                           "Native field path exceeds diagnostic capacity");
      layout_field = cmeta_struct_find_field(layout, field->name);
      if (layout_field == NULL || field->value == NULL ||
          field->value->storage_type == NULL || layout_field->type == NULL ||
          layout_field->offset != field->offset ||
          layout_field->size != field->value->storage_type->size ||
          layout_field->align != field->value->storage_type->align ||
          !cmeta_type_equal(layout_field->type, field->value->storage_type) ||
          !native_size_add(field->offset, field->value->storage_type->size, &end) ||
          end > data->storage_type->size ||
          field->value->storage_type->align == 0u ||
          field->offset % field->value->storage_type->align != 0u)
        return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, child_path,
                           "Native Struct field layout disagrees with canonical CMeta");
      for (j = 0u; j < i; ++j) {
        const cmeta_data_field_desc *previous = &shape->fields[j];
        size_t previous_end;
        if (strcmp(previous->name, field->name) == 0)
          return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, child_path,
                             "Native Struct contains duplicate field names");
        if (previous->value == NULL || previous->value->storage_type == NULL ||
            !native_size_add(previous->offset, previous->value->storage_type->size,
                             &previous_end))
          return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, child_path,
                             "Native Struct field range is invalid");
        if (field->offset < previous_end && previous->offset < end)
          return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, child_path,
                             "Native Struct fields overlap");
      }
      {
        DataBindStatus status =
            native_preflight(plan, field->value, depth + 1u, seen_here, child_path);
        if (status != DATA_BIND_OK) return status;
      }
    }
    return DATA_BIND_OK;
  }

  return native_fail(plan->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                     "Canonical descriptor kind is outside native reader v1");
}

static DataBindStatus native_restore_value(const cmeta_data_desc *data,
                                                  void *storage);

static DataBindStatus native_init_value(DataBindNativeDiagnostic *diagnostic,
                                        const cmeta_data_desc *data, void *storage,
                                        const char *path) {
  if (native_scalar_supported(data)) {
    if (!native_scalar_zero(data, storage))
      return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Could not initialize native scalar semantic zero");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) {
    cmeta_status status = cmeta_data_buffer_init_zero(data, storage);
    if (status == CMETA_OK) return DATA_BIND_OK;
    return native_fail(diagnostic,
                       status == CMETA_OUT_OF_MEMORY ? DATA_BIND_ERR_OOM
                                                    : DATA_BIND_ERR_RUNTIME,
                       CSERDE_OK, path,
                       "Buffer provider could not initialize semantic zero");
  }
  if (data->kind == CMETA_DATA_ENUM) {
    if (cmeta_data_enum_bits_restore_zero(data, storage) == CMETA_OK)
      return DATA_BIND_OK;
    return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                       "Enum provider could not initialize semantic zero");
  }
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    size_t i;
    memset(storage, 0, data->storage_type->size);
    for (i = 0u; i < shape->field_count; ++i) {
      char child_path[sizeof(((DataBindError *)0)->path)];
      DataBindStatus status;
      if (!native_path_join(child_path, sizeof(child_path), path, shape->fields[i].name))
        return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                           "Native field path exceeds diagnostic capacity");
      status = native_init_value(diagnostic, shape->fields[i].value,
                                 (unsigned char *)storage + shape->fields[i].offset,
                                 child_path);
      if (status != DATA_BIND_OK) {
        DataBindStatus rollback_status = DATA_BIND_OK;
        while (i != 0u) {
          --i;
          if (native_restore_value(
                  shape->fields[i].value,
                  (unsigned char *)storage + shape->fields[i].offset) != DATA_BIND_OK)
            rollback_status = DATA_BIND_ERR_RUNTIME;
        }
        if (rollback_status != DATA_BIND_OK)
          return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                             "Native initialization rollback did not restore semantic zero");
        return status;
      }
    }
    return DATA_BIND_OK;
  }
  return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                     "Unsupported native semantic-zero initialization");
}

static DataBindStatus native_restore_value(const cmeta_data_desc *data, void *storage) {
  size_t i;
  if (native_scalar_supported(data))
    return native_scalar_zero(data, storage) ? DATA_BIND_OK : DATA_BIND_ERR_RUNTIME;
  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES)
    return cmeta_data_buffer_restore_zero(data, storage) == CMETA_OK
               ? DATA_BIND_OK
               : DATA_BIND_ERR_RUNTIME;
  if (data->kind == CMETA_DATA_ENUM)
    return cmeta_data_enum_bits_restore_zero(data, storage) == CMETA_OK
               ? DATA_BIND_OK
               : DATA_BIND_ERR_RUNTIME;
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    DataBindStatus result = DATA_BIND_OK;
    for (i = 0u; i < shape->field_count; ++i) {
      if (native_restore_value(shape->fields[i].value,
                               (unsigned char *)storage + shape->fields[i].offset) != DATA_BIND_OK)
        result = DATA_BIND_ERR_RUNTIME;
    }
    return result;
  }
  return DATA_BIND_ERR_RUNTIME;
}

static int native_value_is_zero(const cmeta_data_desc *data, const void *storage) {
  size_t i;
  if (native_scalar_supported(data)) return native_scalar_is_zero(data, storage);
  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) {
    bool zero = false;
    return cmeta_data_buffer_is_zero(data, storage, &zero) == CMETA_OK && zero;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    bool zero = false;
    return cmeta_data_enum_bits_is_zero(data, storage, &zero) == CMETA_OK && zero;
  }
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    for (i = 0u; i < shape->field_count; ++i)
      if (!native_value_is_zero(shape->fields[i].value,
                                (const unsigned char *)storage + shape->fields[i].offset))
        return 0;
    return 1;
  }
  return 0;
}

static DataBindStatus native_publish_value(DataBindNativeDiagnostic *diagnostic,
                                           const cmeta_data_desc *data,
                                           void *destination, void *source,
                                           const char *path) {
  size_t i;
  if (native_scalar_supported(data)) {
    memcpy(destination, source, data->storage_type->size);
    (void)native_scalar_zero(data, source);
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) {
    if (cmeta_data_buffer_move(data, destination, source) != CMETA_OK)
      return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                         "Buffer provider violated no-fail move contract");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_ENUM) {
    uint64_t bits = 0u;
    if (cmeta_data_enum_read_bits(data, source, &bits) != CMETA_OK ||
        cmeta_data_enum_assign_bits(data, destination, bits) != CMETA_OK)
      return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                         "Enum provider could not publish canonical bits");
    if (cmeta_data_enum_bits_restore_zero(data, source) != CMETA_OK)
      return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                         "Enum staging storage did not restore semantic zero");
    return DATA_BIND_OK;
  }
  if (data->kind == CMETA_DATA_STRUCT) {
    const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
    for (i = 0u; i < shape->field_count; ++i) {
      char child_path[sizeof(((DataBindError *)0)->path)];
      DataBindStatus status;
      if (!native_path_join(child_path, sizeof(child_path), path, shape->fields[i].name))
        return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                           "Native field path changed after preflight");
      status = native_publish_value(
          diagnostic, shape->fields[i].value,
          (unsigned char *)destination + shape->fields[i].offset,
          (unsigned char *)source + shape->fields[i].offset, child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  }
  return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                     "Unsupported publication kind after preflight");
}

static DataBindStatus native_reader_failure(NativeDecode *decode, cserde_status source_status,
                                            const char *path) {
  DataBindStatus status;
  const char *message;
  switch (source_status) {
    case CSERDE_DONE:
    case CSERDE_UNEXPECTED_END:
      status = DATA_BIND_ERR_PARSE;
      message = "Reader ended before one complete value";
      break;
    case CSERDE_INVALID_TOKEN:
      status = DATA_BIND_ERR_PARSE;
      message = "Reader produced an invalid token";
      break;
    case CSERDE_VALUE_OUT_OF_RANGE:
      status = DATA_BIND_ERR_TYPE_MISMATCH;
      message = "Reader value is outside source range";
      break;
    case CSERDE_LIMIT_EXCEEDED:
      status = DATA_BIND_ERR_LIMIT;
      message = "Reader source limit was exceeded";
      break;
    case CSERDE_UNSUPPORTED:
      status = DATA_BIND_ERR_SCHEMA;
      message = "Reader source does not support the requested value";
      break;
    case CSERDE_SOURCE_ERROR:
    case CSERDE_SINK_ERROR:
      status = DATA_BIND_ERR_IO;
      message = "Reader source reported an I/O failure";
      break;
    case CSERDE_INVALID_ARGUMENT:
    case CSERDE_INVALID_STATE:
    case CSERDE_CALLBACK_ERROR:
    default:
      status = DATA_BIND_ERR_RUNTIME;
      message = "Reader source entered an invalid runtime state";
      break;
  }
  return native_fail(decode->diagnostic, status, source_status, path, message);
}

static DataBindStatus native_next(NativeDecode *decode, cserde_token *token,
                                  const char *path) {
  cserde_status status = cserde_reader_next(decode->reader, token);
  if (status == CSERDE_OK) return DATA_BIND_OK;
  return native_reader_failure(decode, status, path);
}

static const cmeta_data_field_desc *native_field_from_key(
    const cmeta_data_struct_shape *shape, const cserde_slice *key, size_t *index) {
  size_t i;
  for (i = 0u; i < shape->field_count; ++i) {
    size_t length = strlen(shape->fields[i].name);
    if (length == key->size &&
        (length == 0u || memcmp(shape->fields[i].name, key->data, length) == 0)) {
      if (index != NULL) *index = i;
      return &shape->fields[i];
    }
  }
  return NULL;
}

static DataBindStatus native_decode_value(NativeDecode *decode,
                                          const cmeta_data_desc *data,
                                          void *storage, size_t depth,
                                          const char *path, NativeArena *scratch);

static DataBindStatus native_decode_struct(NativeDecode *decode,
                                           const cmeta_data_desc *data,
                                           void *storage, size_t depth,
                                           const char *path, NativeArena *scratch) {
  const cmeta_data_struct_shape *shape = (const cmeta_data_struct_shape *)data->shape;
  cserde_token token;
  unsigned char *seen;
  size_t mark = scratch->offset;
  size_t i;
  DataBindStatus status = native_next(decode, &token, path);
  if (status != DATA_BIND_OK) return status;
  if (token.kind != CSERDE_MAP_BEGIN)
    return native_fail(decode->diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                       "Expected map token for native Struct");

  seen = (unsigned char *)native_arena_alloc(scratch, shape->field_count, 1u);
  if (shape->field_count != 0u && seen == NULL)
    return native_fail(decode->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                       "Workspace is too small for Struct field tracking");
  if (shape->field_count != 0u) memset(seen, 0, shape->field_count);

  for (;;) {
    size_t field_index = 0u;
    const cmeta_data_field_desc *field;
    char child_path[sizeof(((DataBindError *)0)->path)];
    status = native_next(decode, &token, path);
    if (status != DATA_BIND_OK) {
      scratch->offset = mark;
      return status;
    }
    if (token.kind == CSERDE_MAP_END) break;
    if (token.kind != CSERDE_STRING) {
      scratch->offset = mark;
      return native_fail(decode->diagnostic, DATA_BIND_ERR_PARSE, CSERDE_OK, path,
                         "Native Struct map key must be a string");
    }
    field = native_field_from_key(shape, &token.value.slice, &field_index);
    if (field == NULL) {
      scratch->offset = mark;
      return native_fail(decode->diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Unknown native Struct field");
    }
    if (!native_path_join(child_path, sizeof(child_path), path, field->name)) {
      scratch->offset = mark;
      return native_fail(decode->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                         "Native field path exceeds diagnostic capacity");
    }
    if (seen[field_index] != 0u) {
      scratch->offset = mark;
      return native_fail(decode->diagnostic, DATA_BIND_ERR_PARSE, CSERDE_OK, child_path,
                         "Duplicate native Struct field");
    }
    status = native_decode_value(decode, field->value,
                                 (unsigned char *)storage + field->offset,
                                 depth + 1u, child_path, scratch);
    if (status != DATA_BIND_OK) {
      scratch->offset = mark;
      return status;
    }
    seen[field_index] = 1u;
  }

  for (i = 0u; i < shape->field_count; ++i) {
    if (seen[i] == 0u) {
      if (!native_path_join((char[sizeof(((DataBindError *)0)->path)]){0},
                            sizeof(((DataBindError *)0)->path), path,
                            shape->fields[i].name)) {
        scratch->offset = mark;
        return native_fail(decode->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                           "Native field path exceeds diagnostic capacity");
      }
      {
        char missing_path[sizeof(((DataBindError *)0)->path)];
        (void)native_path_join(missing_path, sizeof(missing_path), path, shape->fields[i].name);
        scratch->offset = mark;
        return native_fail(decode->diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK,
                           missing_path, "Missing required native Struct field");
      }
    }
  }
  scratch->offset = mark;
  return DATA_BIND_OK;
}

static uint64_t native_enum_width_mask(uint8_t bits) {
  return bits == 64u ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
}

static int native_enum_slice_equal(const cserde_slice *slice, const char *text) {
  size_t length;
  if (slice == NULL || text == NULL) return 0;
  length = strlen(text);
  return slice->size == length &&
         (length == 0u || memcmp(slice->data, text, length) == 0);
}

static DataBindStatus native_enum_bits_from_token(
    DataBindNativeDiagnostic *diagnostic, const cmeta_data_desc *data,
    const cserde_token *token, uint64_t *out, const char *path) {
  const cmeta_data_enum_bits_ops *ops = cmeta_data_enum_bits_ops_of(data);
  const cmeta_enum_domain *domain;
  uint64_t mask;
  size_t i;

  if (ops == NULL || ops->domain == NULL || token == NULL || out == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                       "Canonical enum provider is unavailable");
  domain = ops->domain;
  mask = native_enum_width_mask(domain->bits);

  if (token->kind == CSERDE_STRING) {
    for (i = 0u; i < domain->count; ++i) {
      const cmeta_enum_bits_item *item = &domain->items[i];
      if (native_enum_slice_equal(&token->value.slice, item->symbol) ||
          native_enum_slice_equal(&token->value.slice, item->text)) {
        *out = item->bits;
        return DATA_BIND_OK;
      }
    }
    return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                       "Enum string is not declared by the canonical domain");
  }

  if (token->kind == CSERDE_UINT) {
    if ((token->value.uint & ~mask) != 0u)
      return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         "Unsigned enum value exceeds canonical width");
    *out = token->value.uint;
    return DATA_BIND_OK;
  }

  if (token->kind == CSERDE_SINT) {
    int64_t value = token->value.sint;
    if (domain->signedness == CMETA_ENUM_UNSIGNED) {
      if (value < 0 || ((uint64_t)value & ~mask) != 0u)
        return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                           "Signed enum value is outside unsigned canonical width");
      *out = (uint64_t)value;
      return DATA_BIND_OK;
    }
    if (domain->bits != 64u) {
      int64_t limit = INT64_C(1) << (domain->bits - 1u);
      if (value < -limit || value >= limit)
        return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                           "Signed enum value exceeds canonical width");
    }
    *out = ((uint64_t)value) & mask;
    return DATA_BIND_OK;
  }

  return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                     "Expected integer or string token for canonical enum");
}

static DataBindStatus native_assign_enum(
    DataBindNativeDiagnostic *diagnostic, const cmeta_data_desc *data,
    const cserde_token *token, void *storage, const char *path) {
  uint64_t bits = 0u;
  cmeta_status status;
  DataBindStatus converted =
      native_enum_bits_from_token(diagnostic, data, token, &bits, path);
  if (converted != DATA_BIND_OK) return converted;

  status = cmeta_data_enum_assign_bits(data, storage, bits);
  if (status == CMETA_OK) return DATA_BIND_OK;
  if (status == CMETA_INVALID_ARGUMENT)
    return native_fail(diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                       "Enum value is not declared by the canonical domain");
  if (status == CMETA_CALLBACK_ERROR)
    return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                       "Enum provider failed canonical assignment");
  return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, path,
                     "Enum provider rejected canonical assignment");
}

static DataBindStatus native_decode_value(NativeDecode *decode,
                                          const cmeta_data_desc *data,
                                          void *storage, size_t depth,
                                          const char *path, NativeArena *scratch) {
  cserde_token token;
  DataBindStatus status;
  if (depth == 0u || depth > decode->options->max_depth)
    return native_fail(decode->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                       "Decoded value depth exceeds configured limit");
  if (decode->items == decode->options->max_items)
    return native_fail(decode->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                       "Decoded value item count exceeds configured limit");
  ++decode->items;

  if (data->kind == CMETA_DATA_STRUCT)
    return native_decode_struct(decode, data, storage, depth, path, scratch);

  status = native_next(decode, &token, path);
  if (status != DATA_BIND_OK) return status;

  if (native_scalar_supported(data))
    return native_assign_scalar(decode->diagnostic, data, &token, storage, path);

  if (data->kind == CMETA_DATA_ENUM)
    return native_assign_enum(decode->diagnostic, data, &token, storage, path);

  if (data->kind == CMETA_DATA_STRING || data->kind == CMETA_DATA_BYTES) {
    cmeta_status buffer_status;
    size_t remaining;
    cserde_token_kind expected =
        data->kind == CMETA_DATA_STRING ? CSERDE_STRING : CSERDE_BYTES;
    if (token.kind != expected)
      return native_fail(decode->diagnostic, DATA_BIND_ERR_TYPE_MISMATCH, CSERDE_OK, path,
                         data->kind == CMETA_DATA_STRING
                             ? "Expected string token for owned native buffer"
                             : "Expected bytes token for owned native buffer");
    if (decode->owned_bytes > decode->options->max_owned_bytes)
      return native_fail(decode->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                         "Owned payload accounting exceeded configured limit");
    remaining = decode->options->max_owned_bytes - decode->owned_bytes;
    if (token.value.slice.size > remaining)
      return native_fail(decode->diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, path,
                         "Owned payload exceeds configured byte limit");
    buffer_status = cmeta_data_buffer_assign(
        data, storage, token.value.slice.data, token.value.slice.size, remaining);
    if (buffer_status != CMETA_OK) {
      DataBindStatus mapped = DATA_BIND_ERR_RUNTIME;
      if (buffer_status == CMETA_CAPACITY_EXCEEDED)
        mapped = DATA_BIND_ERR_LIMIT;
      else if (buffer_status == CMETA_OUT_OF_MEMORY)
        mapped = DATA_BIND_ERR_OOM;
      else if (buffer_status == CMETA_TYPE_MISMATCH ||
               buffer_status == CMETA_INVALID_ARGUMENT)
        mapped = DATA_BIND_ERR_TYPE_MISMATCH;
      return native_fail(decode->diagnostic, mapped, CSERDE_OK, path,
                         "Owned buffer provider rejected reader payload");
    }
    decode->owned_bytes += token.value.slice.size;
    return DATA_BIND_OK;
  }

  return native_fail(decode->diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, path,
                     "Unsupported native reader descriptor after preflight");
}

static int native_diagnostic_header_valid(const DataBindNativeDiagnostic *diagnostic) {
  if (diagnostic == NULL) return 1;
  if (diagnostic->size < offsetof(DataBindNativeDiagnostic, abi_version) +
                             sizeof(diagnostic->abi_version))
    return 0;
  return diagnostic->size >= sizeof(DataBindNativeDiagnostic) &&
         diagnostic->abi_version == DATA_BIND_NATIVE_ABI_VERSION;
}

static DataBindStatus native_lifecycle_preflight(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic, const char **out_path) {
  NativeArena arena;
  NativePlan plan;
  const cmeta_data_desc **ancestors;
  size_t ancestor_bytes;
  const char *root_path;
  DataBindStatus status;

  if (out_path != NULL) *out_path = NULL;
  if (!native_diagnostic_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  if (options == NULL ||
      options->size < offsetof(DataBindNativeOptions, abi_version) +
                          sizeof(options->abi_version))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native options record is missing its ABI header");
  if (options->size < sizeof(DataBindNativeOptions) ||
      options->abi_version != DATA_BIND_NATIVE_ABI_VERSION)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native options ABI is incompatible");
  if (shape == NULL || destination == NULL || options->workspace == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native lifecycle arguments must be non-null");
  if (!cmeta_data_desc_valid(shape))
    return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, NULL,
                       "Invalid canonical CMeta root descriptor");
  if (shape->storage_type == NULL || !cmeta_type_desc_valid(shape->storage_type))
    return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, NULL,
                       "Native root descriptor has no valid storage type");
  root_path = shape->display_name;

  if (destination_bytes < shape->storage_type->size ||
      shape->storage_type->align == 0u ||
      (uintptr_t)destination % shape->storage_type->align != 0u)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Destination storage size or alignment is invalid");
  if (!native_range_valid(options->workspace, options->workspace_bytes, NULL, NULL) ||
      !native_range_valid(destination, destination_bytes, NULL, NULL))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native workspace or destination range overflows address space");
  if (native_ranges_overlap(options->workspace, options->workspace_bytes,
                            destination, destination_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native workspace overlaps destination storage");
  if (diagnostic != NULL &&
      (native_ranges_overlap(diagnostic, sizeof(*diagnostic),
                             options->workspace, options->workspace_bytes) ||
       native_ranges_overlap(diagnostic, sizeof(*diagnostic),
                             destination, destination_bytes)))
    return DATA_BIND_ERR_INVALID_ARG;
  if (native_ranges_overlap(options, sizeof(*options),
                            options->workspace, options->workspace_bytes) ||
      native_ranges_overlap(options, sizeof(*options), destination, destination_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native control records alias mutable lifecycle storage");

  native_reset_diagnostic(diagnostic);
  if (options->max_depth == 0u || options->max_items == 0u)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Native depth and item budgets must be nonzero");

  arena.base = (unsigned char *)options->workspace;
  arena.size = options->workspace_bytes;
  arena.offset = 0u;
  if (!native_size_mul(options->max_depth, sizeof(*ancestors), &ancestor_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Native depth workspace size overflow");
  ancestors = (const cmeta_data_desc **)native_arena_alloc(
      &arena, ancestor_bytes, _Alignof(const cmeta_data_desc *));
  if (ancestors == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Workspace cannot hold descriptor traversal state");

  memset(&plan, 0, sizeof(plan));
  plan.options = options;
  plan.diagnostic = diagnostic;
  plan.ancestors = ancestors;
  status = native_preflight(&plan, shape, 1u, 0u, root_path);
  if (status != DATA_BIND_OK) return status;
  if (out_path != NULL) *out_path = root_path;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_native_init(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic) {
  const char *root_path = NULL;
  DataBindStatus status = native_lifecycle_preflight(
      options, shape, destination, destination_bytes, diagnostic, &root_path);
  if (status != DATA_BIND_OK) return status;

  status = native_init_value(diagnostic, shape, destination, root_path);
  if (status != DATA_BIND_OK) return status;
  if (!native_value_is_zero(shape, destination)) {
    (void)native_restore_value(shape, destination);
    return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, root_path,
                       "Native initialization did not establish semantic zero");
  }
  native_reset_diagnostic(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_native_clear(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic) {
  const char *root_path = NULL;
  DataBindStatus status = native_lifecycle_preflight(
      options, shape, destination, destination_bytes, diagnostic, &root_path);
  if (status != DATA_BIND_OK) return status;

  status = native_restore_value(shape, destination);
  if (status != DATA_BIND_OK || !native_value_is_zero(shape, destination))
    return native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, root_path,
                       "Native clear did not restore semantic zero");
  native_reset_diagnostic(diagnostic);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_native_decode(
    const DataBindNativeOptions *options, const cmeta_data_desc *shape,
    cserde_reader *reader, void *destination, size_t destination_bytes,
    DataBindNativeDiagnostic *diagnostic) {
  NativeArena arena;
  NativeArena scratch;
  NativePlan plan;
  NativeDecode decode;
  void *temporary;
  const cmeta_data_desc **ancestors;
  size_t ancestor_bytes;
  size_t scratch_offset;
  const char *root_path;
  DataBindStatus status;

  if (!native_diagnostic_header_valid(diagnostic)) return DATA_BIND_ERR_INVALID_ARG;
  if (options == NULL ||
      options->size < offsetof(DataBindNativeOptions, abi_version) +
                          sizeof(options->abi_version))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native options record is missing its ABI header");
  if (options->size < sizeof(DataBindNativeOptions) ||
      options->abi_version != DATA_BIND_NATIVE_ABI_VERSION)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native options ABI is incompatible");
  if (shape == NULL || reader == NULL || destination == NULL || options->workspace == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, NULL,
                       "Native decode arguments must be non-null");
  if (!cmeta_data_desc_valid(shape))
    return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, NULL,
                       "Invalid canonical CMeta root descriptor");
  if (shape->storage_type == NULL || !cmeta_type_desc_valid(shape->storage_type))
    return native_fail(diagnostic, DATA_BIND_ERR_SCHEMA, CSERDE_OK, NULL,
                       "Native root descriptor has no valid storage type");
  root_path = shape->display_name;

  if (destination_bytes < shape->storage_type->size ||
      shape->storage_type->align == 0u ||
      (uintptr_t)destination % shape->storage_type->align != 0u)
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Destination storage size or alignment is invalid");
  if (!native_range_valid(options->workspace, options->workspace_bytes, NULL, NULL) ||
      !native_range_valid(destination, destination_bytes, NULL, NULL))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native workspace or destination range overflows address space");
  if (native_ranges_overlap(options->workspace, options->workspace_bytes,
                            destination, destination_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native workspace overlaps destination storage");
  if (diagnostic != NULL &&
      (native_ranges_overlap(diagnostic, sizeof(*diagnostic),
                             options->workspace, options->workspace_bytes) ||
       native_ranges_overlap(diagnostic, sizeof(*diagnostic),
                             destination, destination_bytes)))
    return DATA_BIND_ERR_INVALID_ARG;
  if (native_ranges_overlap(options, sizeof(*options),
                            options->workspace, options->workspace_bytes) ||
      native_ranges_overlap(options, sizeof(*options), destination, destination_bytes) ||
      native_ranges_overlap(reader, sizeof(*reader),
                            options->workspace, options->workspace_bytes) ||
      native_ranges_overlap(reader, sizeof(*reader), destination, destination_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Native control records alias mutable decode storage");

  native_reset_diagnostic(diagnostic);

  if (options->max_depth == 0u || options->max_items == 0u)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Native depth and item budgets must be nonzero");

  arena.base = (unsigned char *)options->workspace;
  arena.size = options->workspace_bytes;
  arena.offset = 0u;
  temporary = native_arena_alloc(&arena, shape->storage_type->size,
                                 shape->storage_type->align);
  if (temporary == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Workspace cannot hold temporary native root");
  if (!native_size_mul(options->max_depth, sizeof(*ancestors), &ancestor_bytes))
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Native depth workspace size overflow");
  ancestors = (const cmeta_data_desc **)native_arena_alloc(
      &arena, ancestor_bytes, _Alignof(const cmeta_data_desc *));
  if (ancestors == NULL)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Workspace cannot hold descriptor traversal state");

  memset(&plan, 0, sizeof(plan));
  plan.options = options;
  plan.diagnostic = diagnostic;
  plan.ancestors = ancestors;
  status = native_preflight(&plan, shape, 1u, 0u, root_path);
  if (status != DATA_BIND_OK) return status;
  if (plan.seen_peak > arena.size - arena.offset)
    return native_fail(diagnostic, DATA_BIND_ERR_LIMIT, CSERDE_OK, root_path,
                       "Workspace cannot hold Struct field tracking");
  scratch_offset = arena.offset;

  if (!native_value_is_zero(shape, destination))
    return native_fail(diagnostic, DATA_BIND_ERR_INVALID_ARG, CSERDE_OK, root_path,
                       "Destination is not in canonical semantic-zero state");

  status = native_init_value(diagnostic, shape, temporary, root_path);
  if (status != DATA_BIND_OK) return status;

  scratch = arena;
  scratch.offset = scratch_offset;
  memset(&decode, 0, sizeof(decode));
  decode.options = options;
  decode.diagnostic = diagnostic;
  decode.reader = reader;
  status = native_decode_value(&decode, shape, temporary, 1u, root_path, &scratch);
  if (status == DATA_BIND_OK) {
    status = native_publish_value(diagnostic, shape, destination, temporary, root_path);
    if (status != DATA_BIND_OK &&
        native_restore_value(shape, destination) != DATA_BIND_OK)
      status = native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, root_path,
                           "Destination rollback did not restore semantic zero");
  }

  if (native_restore_value(shape, temporary) != DATA_BIND_OK && status == DATA_BIND_OK)
    status = native_fail(diagnostic, DATA_BIND_ERR_RUNTIME, CSERDE_OK, root_path,
                         "Temporary native storage did not restore semantic zero");
  if (status == DATA_BIND_OK) native_reset_diagnostic(diagnostic);
  return status;
}
