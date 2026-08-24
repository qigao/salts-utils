#include "tbe_cbind_internal.h"

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

static int tbe_cbind_native_struct_prefix_safe(
    const cmeta_data_desc *native_shape,
    const cmeta_data_struct_shape **out_shape,
    const cmeta_struct_desc **out_layout) {
  const cmeta_data_struct_shape *shape;
  const cmeta_struct_desc *layout;
  size_t index;
  if (native_shape == NULL ||
      native_shape->struct_size < TBE_CBIND_FIELD_END(cmeta_data_desc, shape) ||
      native_shape->abi_version != CMETA_DATA_DESC_ABI_VERSION ||
      !tbe_cbind_native_nonempty(native_shape->stable_id) ||
      !tbe_cbind_native_nonempty(native_shape->display_name) ||
      native_shape->kind != CMETA_DATA_STRUCT ||
      native_shape->storage_type == NULL || native_shape->shape == NULL)
    return 0;
  shape = (const cmeta_data_struct_shape *)native_shape->shape;
  if (shape->layout == NULL ||
      (shape->field_count != 0u && shape->fields == NULL))
    return 0;
  layout = shape->layout;
  if (!tbe_cbind_native_nonempty(layout->name) ||
      (layout->field_count != 0u && layout->fields == NULL) ||
      shape->field_count > layout->field_count)
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

static tbe_cbind_status tbe_cbind_native_value_matches(
    tbe_cbind_build_context *context, const tbe_cbind_semantic_field *field,
    const cmeta_data_desc *value, size_t field_index, const char *path) {
  const cmeta_data_integer_shape *integer_shape;
  const cmeta_data_float_shape *float_shape;
  const cmeta_type_desc *canonical_type = NULL;
  cmeta_data_kind canonical_kind = CMETA_DATA_CUSTOM;
  unsigned int canonical_bits = 0u;
  if (!cmeta_data_desc_valid(value))
    return tbe_cbind_native_error(
        context, TBE_CBIND_NATIVE_SHAPE_ERROR, field_index,
        CMETA_INVALID_ARGUMENT, path, "native field data descriptor is invalid");
  switch (field->kind) {
    case TBE_CBIND_SEMANTIC_INT32:
      canonical_kind = CMETA_DATA_SINT;
      canonical_type = &cmeta_type_int;
      canonical_bits = 32u;
      break;
    case TBE_CBIND_SEMANTIC_INT64:
      canonical_kind = CMETA_DATA_SINT;
      canonical_type = &cmeta_type_long;
      canonical_bits = 64u;
      break;
    case TBE_CBIND_SEMANTIC_UINT64:
      canonical_kind = CMETA_DATA_UINT;
      canonical_type = &cmeta_type_size;
      canonical_bits = 64u;
      break;
    case TBE_CBIND_SEMANTIC_FLOAT:
      canonical_kind = CMETA_DATA_FLOAT;
      canonical_type = &cmeta_type_float;
      canonical_bits = 32u;
      break;
    case TBE_CBIND_SEMANTIC_DOUBLE:
      canonical_kind = CMETA_DATA_FLOAT;
      canonical_type = &cmeta_type_double;
      canonical_bits = 64u;
      break;
    case TBE_CBIND_SEMANTIC_STRING:
      if (value->kind != CMETA_DATA_STRING)
        return tbe_cbind_native_error(
            context, TBE_CBIND_TYPE_MISMATCH, field_index,
            CMETA_TYPE_MISMATCH, path,
            "schema string field does not use native string storage");
      if (((const cmeta_data_buffer_shape *)value->shape)->ownership ==
          CMETA_DATA_BUFFER_CUSTOM) {
        return tbe_cbind_native_error(
            context, TBE_CBIND_UNSUPPORTED, field_index, CMETA_OK, path,
            "CUSTOM string ownership is unsupported by CBind v1");
      }
      if (cmeta_data_buffer_ops_of(value) == NULL) {
        return tbe_cbind_native_error(
            context, TBE_CBIND_NATIVE_SHAPE_ERROR, field_index,
            CMETA_INVALID_ARGUMENT, path,
            "native string requires a complete matching public buffer adapter");
      }
      return TBE_CBIND_OK;
    case TBE_CBIND_SEMANTIC_RECORD:
      if (value->kind != CMETA_DATA_STRUCT)
        return tbe_cbind_native_error(
            context, TBE_CBIND_TYPE_MISMATCH, field_index,
            CMETA_TYPE_MISMATCH, path,
            "schema record field does not use native struct storage");
      return TBE_CBIND_OK;
  }
  if (canonical_type == NULL || canonical_bits % CHAR_BIT != 0u ||
      canonical_type->size != canonical_bits / CHAR_BIT ||
      value->kind != canonical_kind ||
      !tbe_cbind_native_type_matches(value->storage_type, canonical_type)) {
    return tbe_cbind_native_error(
        context, TBE_CBIND_TYPE_MISMATCH, field_index, CMETA_TYPE_MISMATCH,
        path, "native scalar storage does not match the v1 canonical type");
  }
  if (canonical_kind == CMETA_DATA_FLOAT) {
    float_shape = (const cmeta_data_float_shape *)value->shape;
    if (float_shape == NULL || float_shape->bits != canonical_bits)
      return tbe_cbind_native_error(
          context, TBE_CBIND_TYPE_MISMATCH, field_index, CMETA_TYPE_MISMATCH,
          path, "native floating-point width does not match schema");
  } else {
    integer_shape = (const cmeta_data_integer_shape *)value->shape;
    if (integer_shape == NULL || integer_shape->bits != canonical_bits)
      return tbe_cbind_native_error(
          context, TBE_CBIND_TYPE_MISMATCH, field_index, CMETA_TYPE_MISMATCH,
          path, "native integer width does not match schema");
  }
  return TBE_CBIND_OK;
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
  if (semantic == NULL ||
      !tbe_cbind_native_struct_prefix_safe(native_shape, &shape, &layout)) {
    return tbe_cbind_native_error(
        context, TBE_CBIND_NATIVE_SHAPE_ERROR, 0u, CMETA_INVALID_ARGUMENT,
        semantic != NULL ? semantic->name : NULL,
        "native root must be a valid public CMeta struct descriptor");
  }
  if (!cmeta_data_desc_valid(native_shape) ||
      !cmeta_type_desc_valid(native_shape->storage_type) ||
      native_shape->storage_type->kind != CMETA_T_OBJECT ||
      layout->size != native_shape->storage_type->size ||
      layout->align != native_shape->storage_type->align ||
      shape->field_count != semantic->field_count ||
      layout->field_count != semantic->field_count) {
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
    if (data_by_layout[layout_index] == NULL ||
        !tbe_cbind_native_type_matches(
            layout->fields[layout_index].type,
            data_by_layout[layout_index]->value->storage_type)) {
      status = tbe_cbind_native_error(
          context, TBE_CBIND_NATIVE_SHAPE_ERROR, index,
          CMETA_TYPE_MISMATCH, path,
          "native reflected and data storage types disagree");
      goto cleanup;
    }
    status = tbe_cbind_native_value_matches(
        context, field, data_by_layout[layout_index]->value, index, path);
    if (status != TBE_CBIND_OK) goto cleanup;
    bindings[index].layout_field = &layout->fields[layout_index];
    bindings[index].data_field = data_by_layout[layout_index];
  }
cleanup:
  tbe_cbind_free(context, data_by_layout);
  tbe_cbind_free(context, offset_slots);
  tbe_cbind_free(context, name_slots);
  return status;
}

static tbe_cbind_status tbe_cbind_native_contract_mismatch(
    tbe_cbind_build_context *context, const char *path) {
  return tbe_cbind_set_error(
      context, TBE_CBIND_TYPE_MISMATCH, TBE_CBIND_PHASE_PLAN, 0u,
      CMETA_TYPE_MISMATCH, path,
      "one schema type maps to inconsistent native descriptor contracts");
}

static int tbe_cbind_native_buffer_contract_equal(
    const cmeta_data_desc *left, const cmeta_data_desc *right) {
  const cmeta_data_buffer_shape *left_shape =
      (const cmeta_data_buffer_shape *)left->shape;
  const cmeta_data_buffer_shape *right_shape =
      (const cmeta_data_buffer_shape *)right->shape;
  const cmeta_data_buffer_ops *left_ops = cmeta_data_buffer_ops_of(left);
  const cmeta_data_buffer_ops *right_ops = cmeta_data_buffer_ops_of(right);
  return left_ops != NULL && right_ops != NULL &&
         left_shape->ownership == right_shape->ownership &&
         left_ops->abi_version == right_ops->abi_version &&
         left_ops->ownership == right_ops->ownership &&
         tbe_cbind_native_type_matches(left_ops->storage_type,
                                       right_ops->storage_type) &&
         left_ops->is_zero == right_ops->is_zero &&
         left_ops->assign == right_ops->assign &&
         left_ops->restore_zero == right_ops->restore_zero;
}

static int tbe_cbind_native_scalar_contract_equal(
    const tbe_cbind_semantic_field *semantic, const cmeta_data_desc *left,
    const cmeta_data_desc *right) {
  if (left->kind != right->kind ||
      !tbe_cbind_native_type_matches(left->storage_type,
                                     right->storage_type))
    return 0;
  if (semantic->kind == TBE_CBIND_SEMANTIC_STRING)
    return tbe_cbind_native_buffer_contract_equal(left, right);
  if (semantic->kind == TBE_CBIND_SEMANTIC_FLOAT ||
      semantic->kind == TBE_CBIND_SEMANTIC_DOUBLE) {
    const cmeta_data_float_shape *left_shape =
        (const cmeta_data_float_shape *)left->shape;
    const cmeta_data_float_shape *right_shape =
        (const cmeta_data_float_shape *)right->shape;
    return left_shape->bits == right_shape->bits;
  }
  {
    const cmeta_data_integer_shape *left_shape =
        (const cmeta_data_integer_shape *)left->shape;
    const cmeta_data_integer_shape *right_shape =
        (const cmeta_data_integer_shape *)right->shape;
    return left_shape->bits == right_shape->bits;
  }
}

tbe_cbind_status tbe_cbind_native_record_equivalent(
    tbe_cbind_build_context *context, const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *left, const cmeta_data_desc *right, size_t depth) {
  tbe_cbind_native_binding *left_bindings = NULL;
  tbe_cbind_native_binding *right_bindings = NULL;
  const cmeta_data_struct_shape *left_shape;
  const cmeta_data_struct_shape *right_shape;
  size_t index;
  tbe_cbind_status status;
  if (left == right) return TBE_CBIND_OK;
  if (depth == 0u || depth > context->options->max_depth)
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_CAPACITY_EXCEEDED, semantic->name,
        "native contract comparison exceeds max_depth");
  if (semantic->field_count != 0u) {
    left_bindings = tbe_cbind_alloc_array(
        context, semantic->field_count, sizeof(*left_bindings));
    right_bindings = tbe_cbind_alloc_array(
        context, semantic->field_count, sizeof(*right_bindings));
    if (left_bindings == NULL || right_bindings == NULL) {
      status = tbe_cbind_set_error(
          context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_PLAN, 0u,
          CMETA_OUT_OF_MEMORY, semantic->name,
          "native contract comparison allocation failed");
      goto cleanup;
    }
  }
  status = tbe_cbind_native_bind_record(context, semantic, left,
                                         left_bindings);
  if (status != TBE_CBIND_OK) goto cleanup;
  status = tbe_cbind_native_bind_record(context, semantic, right,
                                         right_bindings);
  if (status != TBE_CBIND_OK) goto cleanup;
  left_shape = (const cmeta_data_struct_shape *)left->shape;
  right_shape = (const cmeta_data_struct_shape *)right->shape;
  if (!tbe_cbind_native_type_matches(left->storage_type,
                                     right->storage_type) ||
      left_shape->layout->size != right_shape->layout->size ||
      left_shape->layout->align != right_shape->layout->align) {
    status = tbe_cbind_native_contract_mismatch(context, semantic->name);
    goto cleanup;
  }
  for (index = 0u; index < semantic->field_count; ++index) {
    const tbe_cbind_semantic_field *field = &semantic->fields[index];
    const cmeta_field_desc *left_layout = left_bindings[index].layout_field;
    const cmeta_field_desc *right_layout = right_bindings[index].layout_field;
    const cmeta_data_desc *left_value = left_bindings[index].data_field->value;
    const cmeta_data_desc *right_value =
        right_bindings[index].data_field->value;
    char path[256];
    (void)snprintf(path, sizeof(path), "%s.%s", semantic->name, field->name);
    if (left_layout->offset != right_layout->offset ||
        left_layout->size != right_layout->size ||
        left_layout->align != right_layout->align ||
        !tbe_cbind_native_type_matches(left_layout->type,
                                       right_layout->type)) {
      status = tbe_cbind_native_contract_mismatch(context, path);
      goto cleanup;
    }
    if (field->kind == TBE_CBIND_SEMANTIC_RECORD) {
      size_t child_depth;
      if (!tbe_cbind_size_add(depth, 1u, &child_depth)) {
        status = tbe_cbind_set_error(
            context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, index,
            CMETA_CAPACITY_EXCEEDED, path,
            "native contract comparison depth overflow");
        goto cleanup;
      }
      status = tbe_cbind_native_record_equivalent(
          context, field->record_type, left_value, right_value, child_depth);
      if (status != TBE_CBIND_OK) goto cleanup;
    } else if (!tbe_cbind_native_scalar_contract_equal(field, left_value,
                                                        right_value)) {
      status = tbe_cbind_native_contract_mismatch(context, path);
      goto cleanup;
    }
  }
  status = TBE_CBIND_OK;
cleanup:
  tbe_cbind_free(context, right_bindings);
  tbe_cbind_free(context, left_bindings);
  return status;
}

#undef TBE_CBIND_FIELD_END
