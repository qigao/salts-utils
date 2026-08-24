#include "tbe_cbind_internal.h"

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

static tbe_cbind_status tbe_cbind_plan_build_node(
    tbe_cbind_build_context *context, const tbe_cbind_schema_model *model,
    tbe_cbind_plan *plan, const tbe_cbind_semantic_type *semantic,
    const cmeta_data_desc *native_shape, size_t depth,
    tbe_cbind_plan_node **out_node) {
  size_t node_index = (size_t)(semantic - model->types);
  tbe_cbind_plan_node *node;
  tbe_cbind_native_binding *bindings = NULL;
  size_t field_index;
  tbe_cbind_status status;
  if (depth == 0u || depth > context->options->max_depth)
    return tbe_cbind_set_error(
        context, TBE_CBIND_LIMIT_EXCEEDED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_CAPACITY_EXCEEDED, semantic->name,
        "plan nesting exceeds max_depth");
  if (node_index >= plan->node_count)
    return tbe_cbind_set_error(
        context, TBE_CBIND_SCHEMA_ERROR, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_INVALID_ARGUMENT, semantic->name,
        "semantic type index is outside the plan");
  node = &plan->nodes[node_index];
  if (node->build_state == 1u)
    return tbe_cbind_set_error(
        context, TBE_CBIND_UNSUPPORTED, TBE_CBIND_PHASE_PLAN, 0u,
        CMETA_INVALID_ARGUMENT, semantic->name,
        "recursive native struct graph is unsupported");
  if (node->build_state == 2u) {
    status = tbe_cbind_native_record_equivalent(
        context, semantic, node->native_shape, native_shape, depth);
    if (status != TBE_CBIND_OK) return status;
    *out_node = node;
    return TBE_CBIND_OK;
  }
  node->build_state = 1u;
  node->native_shape = native_shape;
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
  plan->node_count = model->type_count;
  if (plan->node_count != 0u) {
    plan->nodes = tbe_cbind_plan_alloc_array(context, plan->node_count,
                                             sizeof(*plan->nodes));
    if (plan->nodes == NULL) {
      status = tbe_cbind_plan_allocation_error(
          context, model->root->name, "plan node allocation failed");
      goto fail;
    }
  }
  status = tbe_cbind_plan_build_node(context, model, plan, model->root,
                                     native_shape, 1u, &root_node);
  if (status != TBE_CBIND_OK) goto fail;
  plan->shape = &root_node->data;
  plan->state = TBE_CBIND_PLAN_READY;
  *out = plan;
  return TBE_CBIND_OK;
fail:
  tbe_cbind_plan_release(plan);
  return status;
}

void tbe_cbind_plan_release(tbe_cbind_plan *plan) {
  tbe_cbind_allocator allocator;
  size_t node_index;
  if (plan == NULL) return;
  allocator = plan->allocator;
  if (plan->nodes != NULL) {
    for (node_index = 0u; node_index < plan->node_count; ++node_index) {
      tbe_cbind_plan_node *node = &plan->nodes[node_index];
      size_t field_index;
      if (node->layout_fields != NULL)
        for (field_index = 0u; field_index < node->shape.field_count;
             ++field_index) {
          allocator.free_fn(allocator.context,
                            (void *)node->layout_fields[field_index].name);
        }
      allocator.free_fn(allocator.context, node->data_fields);
      allocator.free_fn(allocator.context, node->layout_fields);
      allocator.free_fn(allocator.context, (void *)node->layout.name);
    }
  }
  allocator.free_fn(allocator.context, plan->nodes);
  plan->state = 0u;
  allocator.free_fn(allocator.context, plan);
}
