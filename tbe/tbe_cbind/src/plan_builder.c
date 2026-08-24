#include "tbe_cbind_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static tbe_cbind_status tbe_cbind_plan_allocation_error(
    tbe_cbind_build_context *context, const char *path,
    const char *message) {
  return tbe_cbind_set_error(
      context,
      context->plan_limit_hit ? TBE_CBIND_LIMIT_EXCEEDED
                              : TBE_CBIND_OUT_OF_MEMORY,
      TBE_CBIND_PHASE_PLAN, 0u,
      context->plan_limit_hit ? CMETA_CAPACITY_EXCEEDED : CMETA_OUT_OF_MEMORY,
      path, message);
}

static char *tbe_cbind_plan_strdup(tbe_cbind_build_context *context,
                                   const char *text) {
  size_t size;
  char *copy;
  if (text == NULL || !tbe_cbind_size_add(strlen(text), 1u, &size)) {
    context->plan_limit_hit = 1;
    return NULL;
  }
  copy = tbe_cbind_plan_alloc_array(context, size, sizeof(*copy));
  if (copy != NULL) memcpy(copy, text, size);
  return copy;
}

static size_t tbe_cbind_plan_hash_pair(
    size_t semantic_index, const cmeta_data_desc *native_shape) {
  uintptr_t pointer = (uintptr_t)native_shape;
  pointer ^= pointer >> 17u;
  pointer *= (uintptr_t)UINT32_C(0xed5ad4bb);
  pointer ^= pointer >> 11u;
  return (size_t)pointer ^ (semantic_index * (size_t)UINT32_C(0x9e3779b9));
}

static tbe_cbind_plan_node *tbe_cbind_plan_find_node(
    const tbe_cbind_plan *plan, size_t semantic_index,
    const cmeta_data_desc *native_shape) {
  size_t slot;
  if (plan->node_slot_count == 0u) return NULL;
  slot = tbe_cbind_plan_hash_pair(semantic_index, native_shape) &
         (plan->node_slot_count - 1u);
  while (plan->node_slots[slot] != NULL) {
    tbe_cbind_plan_node *node = plan->node_slots[slot];
    if (node->semantic_index == semantic_index &&
        node->native_shape == native_shape)
      return node;
    slot = (slot + 1u) & (plan->node_slot_count - 1u);
  }
  return NULL;
}

static tbe_cbind_status tbe_cbind_plan_reserve_node_slot(
    tbe_cbind_build_context *context, tbe_cbind_plan *plan,
    const char *path) {
  tbe_cbind_plan_node **new_slots;
  size_t requested_count;
  size_t new_capacity;
  tbe_cbind_plan_node *node;
  if (!tbe_cbind_size_add(plan->node_count, 1u, &requested_count) ||
      requested_count > plan->node_limit)
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_CAPACITY_EXCEEDED, path,
        "plan native occurrence count exceeds max_plan_bytes");
  if (plan->node_slot_count != 0u &&
      requested_count <= plan->node_slot_count / 2u)
    return TBE_CBIND_OK;
  new_capacity = plan->node_slot_count == 0u ? 8u : plan->node_slot_count;
  do {
    if (new_capacity > SIZE_MAX / 2u)
      return tbe_cbind_set_error(
          context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, 0u,
          CMETA_CAPACITY_EXCEEDED, path,
          "plan native occurrence index capacity overflow");
    new_capacity *= 2u;
  } while (requested_count > new_capacity / 2u);
  new_slots = tbe_cbind_alloc_array(
      context, new_capacity, sizeof(*new_slots));
  if (new_slots == NULL)
    return tbe_cbind_set_error(
        context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_OUT_OF_MEMORY, path,
        "plan native occurrence index allocation failed");
  for (node = plan->nodes; node != NULL; node = node->next) {
    size_t slot = tbe_cbind_plan_hash_pair(
                      node->semantic_index, node->native_shape) &
                  (new_capacity - 1u);
    while (new_slots[slot] != NULL)
      slot = (slot + 1u) & (new_capacity - 1u);
    new_slots[slot] = node;
  }
  context->allocator.free_fn(context->allocator.context, plan->node_slots);
  plan->node_slots = new_slots;
  plan->node_slot_count = new_capacity;
  return TBE_CBIND_OK;
}

static void tbe_cbind_plan_insert_node(tbe_cbind_plan *plan,
                                       tbe_cbind_plan_node *node) {
  size_t slot = tbe_cbind_plan_hash_pair(
                    node->semantic_index, node->native_shape) &
                (plan->node_slot_count - 1u);
  while (plan->node_slots[slot] != NULL)
    slot = (slot + 1u) & (plan->node_slot_count - 1u);
  plan->node_slots[slot] = node;
  node->next = plan->nodes;
  plan->nodes = node;
  ++plan->node_count;
}

static tbe_cbind_status tbe_cbind_plan_build_node(
    tbe_cbind_build_context *context, const tbe_cbind_schema_model *model,
    tbe_cbind_plan *plan, const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *native_shape, size_t depth,
    tbe_cbind_plan_node **out_node) {
  size_t node_index = (size_t)(semantic - model->types);
  tbe_cbind_plan_node *node;
  tbe_cbind_native_binding *bindings = NULL;
  size_t remaining_depth;
  size_t field_index;
  tbe_cbind_status status;
  if (depth == 0u || depth > context->options->max_depth ||
      !tbe_cbind_size_add(context->options->max_depth - depth, 1u,
                          &remaining_depth) ||
      semantic->height > remaining_depth)
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_CAPACITY_EXCEEDED, semantic->name,
        "plan nesting exceeds max_depth");
  if (node_index >= model->type_count)
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_INVALID_ARGUMENT, semantic->name,
        "semantic type index is outside the plan");
  node = tbe_cbind_plan_find_node(plan, node_index, native_shape);
  if (node != NULL && node->build_state == 1u)
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_INVALID_ARGUMENT, semantic->name,
        "recursive native struct graph is unsupported");
  if (node != NULL) {
    *out_node = node;
    return TBE_CBIND_OK;
  }
  status = tbe_cbind_plan_reserve_node_slot(context, plan, semantic->name);
  if (status != TBE_CBIND_OK) return status;
  node = tbe_cbind_plan_alloc_array(context, 1u, sizeof(*node));
  if (node == NULL)
    return tbe_cbind_plan_allocation_error(
        context, semantic->name, "plan occurrence node allocation failed");
  node->semantic_index = node_index;
  node->native_shape = native_shape;
  tbe_cbind_plan_insert_node(plan, node);
  node->build_state = 1u;
  if (semantic->field_count != 0u) {
    bindings = tbe_cbind_alloc_array(context, semantic->field_count,
                                     sizeof(*bindings));
    if (bindings == NULL)
      return tbe_cbind_set_error(
          context, TBE_CBIND_OUT_OF_MEMORY, TBE_CBIND_PHASE_NATIVE_SHAPE,
          0u, CMETA_OUT_OF_MEMORY, semantic->name,
          "native binding allocation failed");
  }
  status = tbe_cbind_native_bind_record(context, semantic, native_shape,
                                        bindings);
  if (status != TBE_CBIND_OK) goto cleanup;
  node->layout.name = tbe_cbind_plan_strdup(context, semantic->name);
  if (node->layout.name == NULL) {
    status = tbe_cbind_plan_allocation_error(
        context, semantic->name, "plan type name allocation failed");
    goto cleanup;
  }
  node->layout.size =
      ((const cmeta_data_struct_shape *)native_shape->shape)->layout->size;
  node->layout.align =
      ((const cmeta_data_struct_shape *)native_shape->shape)->layout->align;
  node->layout.field_count = semantic->field_count;
  node->shape.layout = &node->layout;
  node->shape.field_count = semantic->field_count;
  if (semantic->field_count != 0u) {
    node->layout_fields = tbe_cbind_plan_alloc_array(
        context, semantic->field_count, sizeof(*node->layout_fields));
    node->data_fields = tbe_cbind_plan_alloc_array(
        context, semantic->field_count, sizeof(*node->data_fields));
    if (node->layout_fields == NULL || node->data_fields == NULL) {
      status = tbe_cbind_plan_allocation_error(
          context, semantic->name, "plan field overlay allocation failed");
      goto cleanup;
    }
  }
  node->layout.fields = node->layout_fields;
  node->shape.fields = node->data_fields;
  for (field_index = 0u; field_index < semantic->field_count; ++field_index) {
    const tbe_cbind_semantic_field *field = &semantic->fields[field_index];
    tbe_cbind_plan_node *child = NULL;
    char *semantic_name = tbe_cbind_plan_strdup(context, field->semantic_name);
    char path[256];
    if (semantic_name == NULL) {
      status = tbe_cbind_plan_allocation_error(
          context, semantic->name, "plan field name allocation failed");
      goto cleanup;
    }
    node->layout_fields[field_index] = *bindings[field_index].layout_field;
    node->layout_fields[field_index].name = semantic_name;
    node->data_fields[field_index] = *bindings[field_index].data_field;
    node->data_fields[field_index].name = semantic_name;
    if (field->kind == TBE_CBIND_SEMANTIC_RECORD) {
      size_t child_depth;
      (void)snprintf(path, sizeof(path), "%s.%s", semantic->name,
                     field->name);
      if (!tbe_cbind_size_add(depth, 1u, &child_depth)) {
        status = tbe_cbind_set_error(
            context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN,
            field_index, CMETA_CAPACITY_EXCEEDED, path,
            "plan nesting depth overflow");
        goto cleanup;
      }
      status = tbe_cbind_plan_build_node(
          context, model, plan, field->record_type,
          bindings[field_index].data_field->value, child_depth, &child);
      if (status != TBE_CBIND_OK) goto cleanup;
      node->data_fields[field_index].value = &child->data;
    }
  }
  node->data.struct_size = sizeof(node->data);
  node->data.abi_version = native_shape->abi_version;
  node->data.stable_id = native_shape->stable_id;
  node->data.display_name = node->layout.name;
  node->data.kind = native_shape->kind;
  node->data.storage_type = native_shape->storage_type;
  node->data.shape = &node->shape;
  node->data.buffer_ops = NULL;
  node->build_state = 2u;
  *out_node = node;
  status = TBE_CBIND_OK;
cleanup:
  tbe_cbind_free(context, bindings);
  return status;
}

tbe_cbind_status tbe_cbind_plan_build(
    tbe_cbind_build_context *context, const tbe_cbind_schema_model *model,
    const cmeta_data_desc *native_shape, tbe_cbind_plan **out) {
  tbe_cbind_plan *plan;
  tbe_cbind_plan_node *root_node = NULL;
  tbe_cbind_status status;
  *out = NULL;
  plan = tbe_cbind_plan_alloc_array(context, 1u, sizeof(*plan));
  if (plan == NULL)
    return tbe_cbind_plan_allocation_error(
        context, model->root->name, "plan allocation failed");
  plan->allocator = context->allocator;
  plan->node_limit = context->options->max_plan_bytes /
                     sizeof(tbe_cbind_plan_node);
  status = tbe_cbind_plan_build_node(context, model, plan, model->root,
                                     native_shape, 1u, &root_node);
  if (status != TBE_CBIND_OK) goto fail;
  plan->shape = &root_node->data;
  plan->state = TBE_CBIND_PLAN_READY;
  context->allocator.free_fn(context->allocator.context, plan->node_slots);
  plan->node_slots = NULL;
  plan->node_slot_count = 0u;
  *out = plan;
  return TBE_CBIND_OK;
fail:
  tbe_cbind_plan_release(plan);
  return status;
}

void tbe_cbind_plan_release(tbe_cbind_plan *plan) {
  tbe_cbind_allocator allocator;
  tbe_cbind_plan_node *node;
  if (plan == NULL) return;
  allocator = plan->allocator;
  allocator.free_fn(allocator.context, plan->node_slots);
  while (plan->nodes != NULL) {
    size_t field_index;
    node = plan->nodes;
    plan->nodes = node->next;
    if (node->layout_fields != NULL)
      for (field_index = 0u; field_index < node->shape.field_count;
           ++field_index)
        allocator.free_fn(allocator.context,
                          (void *)node->layout_fields[field_index].name);
    allocator.free_fn(allocator.context, node->data_fields);
    allocator.free_fn(allocator.context, node->layout_fields);
    allocator.free_fn(allocator.context, (void *)node->layout.name);
    allocator.free_fn(allocator.context, node);
  }
  plan->state = 0u;
  allocator.free_fn(allocator.context, plan);
}
