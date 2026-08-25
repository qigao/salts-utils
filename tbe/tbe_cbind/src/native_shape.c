#include "tbe_cbind_internal.h"
#include "tbe_cbind_capability.h"
#include "turbo_cmeta_data.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TBE_CBIND_FIELD_END(type, member) \
  (offsetof(type, member) + sizeof(((type *)0)->member))

typedef struct tbe_cbind_native_name_slot {
  const char *name;
  size_t index;
} tbe_cbind_native_name_slot;

typedef struct tbe_cbind_native_offset_slot {
  size_t offset;
  size_t index;
  int occupied;
} tbe_cbind_native_offset_slot;

static uint64_t tbe_cbind_native_hash_name(const char *text) {
  uint64_t hash = UINT64_C(1469598103934665603);
  while (*text != '\0') {
    hash ^= (unsigned char)*text++;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static size_t tbe_cbind_native_hash_offset(size_t value) {
  value ^= value >> 16u;
  value *= (size_t)UINT32_C(0x7feb352d);
  value ^= value >> 15u;
  value *= (size_t)UINT32_C(0x846ca68b);
  value ^= value >> 16u;
  return value;
}

static int tbe_cbind_native_index_capacity(size_t count, size_t *out) {
  size_t requested;
  size_t capacity = 8u;
  if (!tbe_cbind_size_mul(count, 2u, &requested)) return 0;
  if (requested < capacity) requested = capacity;
  while (capacity < requested) {
    if (capacity > SIZE_MAX / 2u) return 0;
    capacity *= 2u;
  }
  *out = capacity;
  return 1;
}

static tbe_cbind_status tbe_cbind_native_error(
    tbe_cbind_build_context *context, tbe_cbind_status status,
    size_t field_index, cmeta_status target_status, const char *path,
    const char *message) {
  return tbe_cbind_set_error(context, status, TBE_CBIND_PHASE_NATIVE_SHAPE,
                             field_index, target_status, path, message);
}

static int tbe_cbind_native_nonempty(const char *text) {
  return text != NULL && text[0] != '\0';
}

static int tbe_cbind_native_data_prefix_safe(
    const cmeta_data_desc *value) {
  return value != NULL &&
         value->struct_size >= TBE_CBIND_FIELD_END(cmeta_data_desc, shape) &&
         value->abi_version == CMETA_DATA_DESC_ABI_VERSION &&
         tbe_cbind_native_nonempty(value->stable_id) &&
         tbe_cbind_native_nonempty(value->display_name) &&
         cmeta_data_kind_valid(value->kind) && value->storage_type != NULL;
}

static int tbe_cbind_native_struct_prefix_safe(
    const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *native_shape,
    const cmeta_data_struct_shape **out_shape,
    const cmeta_struct_desc **out_layout) {
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  size_t index;
  if (semantic == NULL || !tbe_cbind_native_data_prefix_safe(native_shape) ||
      native_shape->kind != CMETA_DATA_STRUCT ||
      native_shape->shape == NULL)
    return 0;
  shape = (const cmeta_data_struct_shape *)native_shape->shape;
  if (shape->layout == NULL ||
      (shape->field_count != 0u && shape->fields == NULL))
    return 0;
  layout = shape->layout;
  if (!tbe_cbind_native_nonempty(layout->name) ||
      (layout->field_count != 0u && layout->fields == NULL) ||
      shape->field_count != semantic->field_count ||
      layout->field_count != semantic->field_count)
    return 0;
  for (index = 0u; index < layout->field_count; ++index)
    if (!tbe_cbind_native_nonempty(layout->fields[index].name)) return 0;
  for (index = 0u; index < shape->field_count; ++index)
    if (!tbe_cbind_native_nonempty(shape->fields[index].stable_id) ||
        !tbe_cbind_native_nonempty(shape->fields[index].name) ||
        shape->fields[index].value == NULL)
      return 0;
  *out_shape = shape;
  *out_layout = layout;
  return 1;
}

static int tbe_cbind_native_type_matches(const cmeta_type_desc *left,
                                         const cmeta_type_desc *right) {
  return left != NULL && right != NULL && cmeta_type_equal(left, right) &&
         left->kind == right->kind && left->size == right->size &&
         left->align == right->align;
}

static size_t tbe_cbind_native_integer_alignment(
    const tbe_cbind_capability *capability) {
  switch (capability->bits) {
    case 8u:
      return capability->is_signed ? _Alignof(int8_t) : _Alignof(uint8_t);
    case 16u:
      return capability->is_signed ? _Alignof(int16_t) : _Alignof(uint16_t);
    case 32u:
      return capability->is_signed ? _Alignof(int32_t) : _Alignof(uint32_t);
    case 64u:
      return capability->is_signed ? _Alignof(int64_t) : _Alignof(uint64_t);
    default:
      return 0u;
  }
}

static tbe_cbind_status tbe_cbind_native_scalar_type_mismatch(
    tbe_cbind_build_context *context, size_t field_index, const char *path,
    const char *message) {
  return tbe_cbind_native_error(
      context, TBE_CBIND_TYPE_MISMATCH, field_index, CMETA_TYPE_MISMATCH,
      path, message);
}

static tbe_cbind_status tbe_cbind_native_scalar_shape_error(
    tbe_cbind_build_context *context, size_t field_index, const char *path,
    const char *message) {
  return tbe_cbind_native_error(
      context, TBE_CBIND_NATIVE_SHAPE_ERROR, field_index,
      CMETA_INVALID_ARGUMENT, path, message);
}

static const cmeta_type_desc *tbe_cbind_native_enum_storage_type(
    const tbe_cbind_capability *underlying) {
  if (underlying == NULL || underlying->kind != TBE_CBIND_SCALAR_INTEGER)
    return NULL;
  switch (underlying->bits) {
    case 8u:
      return underlying->is_signed ? &turbo_int8_cmeta_type
                                   : &turbo_uint8_cmeta_type;
    case 16u:
      return underlying->is_signed ? &turbo_int16_cmeta_type
                                   : &turbo_uint16_cmeta_type;
    case 32u:
      return underlying->is_signed ? &turbo_int32_cmeta_type
                                   : &turbo_uint32_cmeta_type;
    case 64u:
      return underlying->is_signed ? &turbo_int64_cmeta_type
                                   : &turbo_uint64_cmeta_type;
    default:
      return NULL;
  }
}

static tbe_cbind_status tbe_cbind_native_enum_callback_error(
    tbe_cbind_build_context *context, size_t field_index, const char *path,
    const char *message) {
  return tbe_cbind_native_error(
      context, TBE_CBIND_NATIVE_SHAPE_ERROR, field_index,
      CMETA_CALLBACK_ERROR, path, message);
}

static tbe_cbind_status tbe_cbind_native_enum_callbacks_match(
    tbe_cbind_build_context *context,
    const tbe_cbind_semantic_enum *semantic,
    const cmeta_data_desc *value, size_t field_index, const char *path) {
  const cmeta_data_enum_ops *ops = value->enum_ops;
  void *scratch = tbe_cbind_alloc_array(
      context, 1u, value->storage_type->size);
  tbe_cbind_status result = TBE_CBIND_OK;
  bool is_zero = false;
  size_t index;
  if (scratch == NULL) {
    return tbe_cbind_native_error(
        context, TBE_CBIND_OUT_OF_MEMORY, field_index, CMETA_OUT_OF_MEMORY,
        path, "native enum preflight scratch allocation failed");
  }
  if (cmeta_data_enum_is_zero(value, scratch, &is_zero) != CMETA_OK ||
      !is_zero) {
    result = tbe_cbind_native_enum_callback_error(
        context, field_index, path,
        "native enum zero-initialized storage is not semantic zero");
    goto cleanup;
  }
  if (cmeta_data_enum_restore_zero(value, scratch) != CMETA_OK ||
      cmeta_data_enum_restore_zero(value, scratch) != CMETA_OK) {
    result = tbe_cbind_native_enum_callback_error(
        context, field_index, path,
        "native enum restore_zero is not idempotent semantic zero");
    goto cleanup;
  }
  for (index = 0u; index < semantic->item_count; ++index) {
    int64_t observed = 0;
    if (cmeta_data_enum_assign(value, scratch,
                               semantic->items[index].value) != CMETA_OK ||
        cmeta_data_enum_read(value, scratch, &observed) != CMETA_OK ||
        observed != semantic->items[index].value ||
        cmeta_data_enum_restore_zero(value, scratch) != CMETA_OK) {
      result = tbe_cbind_native_enum_callback_error(
          context, field_index, path,
          "native enum callbacks violate assign/read/restore semantics");
      goto cleanup;
    }
  }
cleanup:
  ops->restore_zero(scratch);
  tbe_cbind_free(context, scratch);
  return result;
}

static tbe_cbind_status tbe_cbind_native_enum_matches(
    tbe_cbind_build_context *context,
    const tbe_cbind_semantic_enum *semantic,
    const cmeta_data_desc *value, size_t field_index, const char *path) {
  const cmeta_type_desc *expected_type =
      tbe_cbind_native_enum_storage_type(semantic->underlying);
  const cmeta_data_enum_shape *shape;
  const cmeta_enum_desc *meta;
  const cmeta_data_enum_ops *ops;
  size_t index;
  if (value->kind != CMETA_DATA_ENUM) {
    return tbe_cbind_native_scalar_type_mismatch(
        context, field_index, path,
        "schema enum field does not use native enum storage");
  }
  if (expected_type == NULL || CHAR_BIT != 8 ||
      !tbe_cbind_native_type_matches(value->storage_type, expected_type)) {
    return tbe_cbind_native_scalar_type_mismatch(
        context, field_index, path,
        "native enum underlying signedness width size or alignment mismatches schema");
  }
  if (value->struct_size < TBE_CBIND_FIELD_END(cmeta_data_desc, enum_ops) ||
      value->shape == NULL || value->enum_ops == NULL) {
    return tbe_cbind_native_scalar_shape_error(
        context, field_index, path,
        "native enum descriptor does not expose shape and enum ops");
  }
  shape = (const cmeta_data_enum_shape *)value->shape;
  meta = shape->meta;
  ops = value->enum_ops;
  if (meta == NULL || !tbe_cbind_native_nonempty(meta->name) ||
      (meta->count != 0u && meta->items == NULL)) {
    return tbe_cbind_native_scalar_shape_error(
        context, field_index, path, "native enum reflection is malformed");
  }
  if (ops->struct_size < TBE_CBIND_FIELD_END(cmeta_data_enum_ops,
                                             restore_zero) ||
      ops->abi_version != CMETA_DATA_ENUM_OPS_ABI_VERSION ||
      ops->storage_type == NULL || ops->is_zero == NULL ||
      ops->read == NULL || ops->assign == NULL || ops->restore_zero == NULL ||
      !tbe_cbind_native_type_matches(ops->storage_type, expected_type) ||
      cmeta_data_enum_ops_of(value) == NULL || !cmeta_data_desc_valid(value)) {
    return tbe_cbind_native_scalar_shape_error(
        context, field_index, path,
        "native enum requires complete versioned storage-matching ops");
  }
  if (strcmp(meta->name, semantic->name) != 0 ||
      meta->count != semantic->item_count) {
    return tbe_cbind_native_scalar_type_mismatch(
        context, field_index, path,
        "native enum name or item count does not match schema");
  }
  for (index = 0u; index < semantic->item_count; ++index) {
    const cmeta_enum_item_desc *native_item = &meta->items[index];
    const tbe_cbind_semantic_enum_item *item = &semantic->items[index];
    if (!tbe_cbind_native_nonempty(native_item->symbol) ||
        !tbe_cbind_native_nonempty(native_item->text)) {
      return tbe_cbind_native_scalar_shape_error(
          context, field_index, path,
          "native enum item reflection is malformed");
    }
    if (native_item->value != item->value ||
        strcmp(native_item->symbol, item->symbol) != 0 ||
        strcmp(native_item->text, item->text) != 0) {
      return tbe_cbind_native_scalar_type_mismatch(
          context, field_index, path,
          "native enum item symbol text or value does not match schema");
    }
  }
  return tbe_cbind_native_enum_callbacks_match(
      context, semantic, value, field_index, path);
}

static tbe_cbind_status tbe_cbind_native_value_matches(
    tbe_cbind_build_context *context, const tbe_cbind_semantic_field *field,
    const cmeta_data_desc *value, size_t field_index, const char *path) {
  const tbe_cbind_capability *capability = field->capability;
  if (!tbe_cbind_native_data_prefix_safe(value))
    return tbe_cbind_native_scalar_shape_error(
        context, field_index, path, "native field data descriptor is invalid");
  if (field->kind == TBE_CBIND_SEMANTIC_RECORD) {
    if (value->kind != CMETA_DATA_STRUCT)
      return tbe_cbind_native_scalar_type_mismatch(
          context, field_index, path,
          "schema record field does not use native struct storage");
    {
      const cmeta_data_struct_shape *shape;
      const cmeta_struct_desc *layout;
      if (!tbe_cbind_native_struct_prefix_safe(
              field->record_type, value, &shape, &layout) ||
          !cmeta_data_desc_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "nested native struct descriptor is malformed");
    }
    return TBE_CBIND_OK;
  }
  if (field->kind == TBE_CBIND_SEMANTIC_ENUM) {
    if (field->enum_type == NULL)
      return tbe_cbind_native_scalar_shape_error(
          context, field_index, path,
          "semantic enum metadata is missing");
    return tbe_cbind_native_enum_matches(
        context, field->enum_type, value, field_index, path);
  }
  if (field->kind != TBE_CBIND_SEMANTIC_SCALAR || capability == NULL)
    return tbe_cbind_native_scalar_shape_error(
        context, field_index, path,
        "semantic scalar capability metadata is missing");

  switch (capability->kind) {
    case TBE_CBIND_SCALAR_BOOL:
      if (value->kind != CMETA_DATA_BOOL ||
          !tbe_cbind_native_type_matches(value->storage_type,
                                         &cmeta_type_bool))
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "schema bool field does not use native bool storage");
      if (!cmeta_data_desc_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native bool data descriptor is invalid");
      return TBE_CBIND_OK;

    case TBE_CBIND_SCALAR_INTEGER: {
      const cmeta_data_integer_shape *shape =
          (const cmeta_data_integer_shape *)value->shape;
      cmeta_data_kind expected_kind = capability->is_signed
                                          ? CMETA_DATA_SINT
                                          : CMETA_DATA_UINT;
      size_t expected_align =
          tbe_cbind_native_integer_alignment(capability);
      if (value->kind != expected_kind)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "native integer signedness does not match schema");
      if (shape == NULL || shape->bits != capability->bits)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "native integer width does not match schema");
      if (CHAR_BIT != 8 || expected_align == 0u ||
          capability->bits % CHAR_BIT != 0u ||
          value->storage_type->kind != CMETA_T_INTEGER ||
          value->storage_type->size != capability->bits / CHAR_BIT ||
          value->storage_type->align != expected_align)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "native integer size or alignment does not match schema");
      if (!cmeta_data_desc_valid(value) ||
          !cmeta_type_desc_valid(value->storage_type))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native integer data descriptor is invalid");
      return TBE_CBIND_OK;
    }

    case TBE_CBIND_SCALAR_FLOAT: {
      const cmeta_data_float_shape *shape =
          (const cmeta_data_float_shape *)value->shape;
      const cmeta_type_desc *expected_type =
          capability->bits == 32u ? &cmeta_type_float
                                  : capability->bits == 64u
                                        ? &cmeta_type_double
                                        : NULL;
      if (value->kind != CMETA_DATA_FLOAT || shape == NULL ||
          shape->bits != capability->bits || expected_type == NULL ||
          !tbe_cbind_native_type_matches(value->storage_type, expected_type))
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "native floating-point storage does not match schema");
      if (!cmeta_data_desc_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native floating-point data descriptor is invalid");
      return TBE_CBIND_OK;
    }

    case TBE_CBIND_SCALAR_STRING:
      if (value->kind != CMETA_DATA_STRING)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "schema string field does not use native string storage");
      if (tbe_cbind_native_type_matches(value->storage_type,
                                        &turbo_uuid_cmeta_type))
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "schema string field cannot use UUID storage");
      if (!cmeta_data_desc_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native field data descriptor is invalid");
      if (((const cmeta_data_buffer_shape *)value->shape)->ownership ==
          CMETA_DATA_BUFFER_CUSTOM) {
        return tbe_cbind_native_error(
            context, TBE_CBIND_UNSUPPORTED, field_index, CMETA_OK, path,
            "CUSTOM string ownership is unsupported by CBind v1");
      }
      if (cmeta_data_buffer_ops_of(value) == NULL) {
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native string requires a complete matching public buffer adapter");
      }
      return TBE_CBIND_OK;

    case TBE_CBIND_SCALAR_UUID: {
      const cmeta_data_buffer_shape *shape =
          (const cmeta_data_buffer_shape *)value->shape;
      if (value->kind != CMETA_DATA_STRING ||
          !tbe_cbind_native_type_matches(value->storage_type,
                                         &turbo_uuid_cmeta_type) ||
          value->storage_type->size != TURBO_UUID_SIZE)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "schema UUID field does not use turbo_uuid_t storage");
      if (shape == NULL || shape->ownership != CMETA_DATA_BUFFER_OWNED)
        return tbe_cbind_native_scalar_type_mismatch(
            context, field_index, path,
            "native UUID adapter must own fixed UUID storage");
      if (!cmeta_data_desc_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native UUID data descriptor is invalid");
      if (!turbo_uuid_cmeta_data_valid(value))
        return tbe_cbind_native_scalar_shape_error(
            context, field_index, path,
            "native UUID requires canonical public buffer ops");
      return TBE_CBIND_OK;
    }
  }
  return tbe_cbind_native_scalar_shape_error(
      context, field_index, path, "scalar capability kind is invalid");
}

tbe_cbind_status tbe_cbind_native_bind_record(
    tbe_cbind_build_context *context, const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *native_shape, tbe_cbind_native_binding *bindings) {
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  tbe_cbind_native_name_slot *name_slots = NULL;
  tbe_cbind_native_offset_slot *offset_slots = NULL;
  const cmeta_data_field_desc **data_by_layout = NULL;
  size_t capacity = 0u;
  size_t index;
  tbe_cbind_status status = TBE_CBIND_OK;
  if (semantic == NULL || !tbe_cbind_native_struct_prefix_safe(
                              semantic, native_shape, &shape, &layout)) {
    return tbe_cbind_native_error(
        context, TBE_CBIND_NATIVE_SHAPE_ERROR, 0u, CMETA_INVALID_ARGUMENT,
        semantic != NULL ? semantic->name : NULL,
        "native root must be a valid public CMeta struct descriptor");
  }
  if (!cmeta_data_desc_valid(native_shape) ||
      !cmeta_type_desc_valid(native_shape->storage_type) ||
      native_shape->storage_type->kind != CMETA_T_OBJECT ||
      layout->size != native_shape->storage_type->size ||
      layout->align != native_shape->storage_type->align) {
    return tbe_cbind_native_error(
        context, TBE_CBIND_NATIVE_SHAPE_ERROR, 0u, CMETA_TYPE_MISMATCH,
        semantic->name,
        "native layout, storage type, or field count is inconsistent");
  }
  if (semantic->field_count == 0u) return TBE_CBIND_OK;
  if (!tbe_cbind_native_index_capacity(semantic->field_count, &capacity))
    return tbe_cbind_native_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, 0u, CMETA_CAPACITY_EXCEEDED,
        semantic->name, "native index size overflow");
  name_slots = tbe_cbind_alloc_array(context, capacity, sizeof(*name_slots));
  offset_slots = tbe_cbind_alloc_array(context, capacity, sizeof(*offset_slots));
  data_by_layout = tbe_cbind_alloc_array(context, semantic->field_count,
                                         sizeof(*data_by_layout));
  if (name_slots == NULL || offset_slots == NULL || data_by_layout == NULL) {
    status = tbe_cbind_native_error(
        context, TBE_CBIND_OUT_OF_MEMORY, 0u, CMETA_OUT_OF_MEMORY,
        semantic->name, "native index allocation failed");
    goto cleanup;
  }
  for (index = 0u; index < layout->field_count; ++index) {
    const cmeta_field_desc *field = &layout->fields[index];
    size_t end;
    size_t name_slot;
    size_t offset_slot;
    if (field->name == NULL || !tbe_cbind_c_identifier_valid(field->name) ||
        !cmeta_type_desc_valid(field->type) || field->size != field->type->size ||
        field->align != field->type->align || field->align == 0u ||
        field->offset % field->align != 0u ||
        !tbe_cbind_size_add(field->offset, field->size, &end) ||
        end > layout->size) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_INVALID_ARGUMENT, semantic->name,
          "native reflected field layout is malformed");
      goto cleanup;
    }
    name_slot = (size_t)tbe_cbind_native_hash_name(field->name) &
                (capacity - 1u);
    while (name_slots[name_slot].name != NULL) {
      if (strcmp(name_slots[name_slot].name, field->name) == 0) {
        status = tbe_cbind_native_error(
            context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
            CMETA_INVALID_ARGUMENT, semantic->name,
            "native reflected member names are duplicated");
        goto cleanup;
      }
      name_slot = (name_slot + 1u) & (capacity - 1u);
    }
    name_slots[name_slot].name = field->name;
    name_slots[name_slot].index = index;
    offset_slot = tbe_cbind_native_hash_offset(field->offset) & (capacity - 1u);
    while (offset_slots[offset_slot].occupied) {
      if (offset_slots[offset_slot].offset == field->offset) {
        status = tbe_cbind_native_error(
            context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
            CMETA_INVALID_ARGUMENT, semantic->name,
            "native reflected member offsets are duplicated");
        goto cleanup;
      }
      offset_slot = (offset_slot + 1u) & (capacity - 1u);
    }
    offset_slots[offset_slot].occupied = 1;
    offset_slots[offset_slot].offset = field->offset;
    offset_slots[offset_slot].index = index;
  }
  for (index = 0u; index < shape->field_count; ++index) {
    const cmeta_data_field_desc *field = &shape->fields[index];
    size_t slot = tbe_cbind_native_hash_offset(field->offset) & (capacity - 1u);
    while (offset_slots[slot].occupied &&
           offset_slots[slot].offset != field->offset)
      slot = (slot + 1u) & (capacity - 1u);
    if (!offset_slots[slot].occupied ||
        data_by_layout[offset_slots[slot].index] != NULL) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_INVALID_ARGUMENT, semantic->name,
          "native data field offset is missing or duplicated");
      goto cleanup;
    }
    data_by_layout[offset_slots[slot].index] = field;
  }
  for (index = 0u; index < semantic->field_count; ++index) {
    const tbe_cbind_semantic_field *field = &semantic->fields[index];
    char path[256];
    size_t slot = (size_t)tbe_cbind_native_hash_name(field->native_name) &
                  (capacity - 1u);
    size_t layout_index;
    (void)snprintf(path, sizeof(path), "%s.%s", semantic->name, field->name);
    while (name_slots[slot].name != NULL &&
           strcmp(name_slots[slot].name, field->native_name) != 0)
      slot = (slot + 1u) & (capacity - 1u);
    if (name_slots[slot].name == NULL) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_INVALID_ARGUMENT, path, "native member selected by c was not found");
      goto cleanup;
    }
    layout_index = name_slots[slot].index;
    if (data_by_layout[layout_index] == NULL) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_INVALID_ARGUMENT, path,
          "native data field selected by c was not found");
      goto cleanup;
    }
    status = tbe_cbind_native_value_matches(
        context, field, data_by_layout[layout_index]->value, index, path);
    if (status != TBE_CBIND_OK) goto cleanup;
    if (!tbe_cbind_native_type_matches(
            layout->fields[layout_index].type,
            data_by_layout[layout_index]->value->storage_type)) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_TYPE_MISMATCH, path,
          "native reflected and data storage types disagree");
      goto cleanup;
    }
    bindings[index].layout_field = &layout->fields[layout_index];
    bindings[index].data_field = data_by_layout[layout_index];
  }
cleanup:
  tbe_cbind_free(context, data_by_layout);
  tbe_cbind_free(context, offset_slots);
  tbe_cbind_free(context, name_slots);
  return status;
}

#undef TBE_CBIND_FIELD_END
