#include "binary_layout_lowering.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct binary_execution_node {
  databind_binary_type_layout layout;
  DataBindBinaryLayoutPlan plan;
  DataBindBinaryFieldPlan *fields;
  DataBindBinaryArrayPlan *arrays;
  const DataBindBinaryArrayPlan **array_table;
  const DataBindBinaryLayoutPlan **children;
  unsigned state;
  size_t height;
  int has_tail;
} binary_execution_node;

struct databind_binary_execution_graph {
  binary_execution_node *nodes;
  size_t node_count;
  size_t field_count;
  const DataBindBinaryLayoutPlan *root;
};

static databind_binary_layout_status lowering_fail(
    databind_binary_layout_diagnostic *diagnostic, const char *field, const char *text) {
  if (diagnostic != NULL) {
    snprintf(diagnostic->field, sizeof(diagnostic->field), "%s", field != NULL ? field : "");
    snprintf(diagnostic->text, sizeof(diagnostic->text), "%s", text);
  }
  return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
}

const char *databind_binary_execution_child_type(
    const IdlContract *contract, const char *type_name,
    const databind_binary_field_layout *field) {
  const IdlDataDecl *record = idl_contract_find_data(contract, type_name);
  size_t i;
  if (record == NULL || field == NULL ||
      (field->kind != DATABIND_BINARY_FIELD_FIXED && field->kind != DATABIND_BINARY_FIELD_CURSOR_FIXED &&
       field->kind != DATABIND_BINARY_FIELD_GROUP && field->kind != DATABIND_BINARY_FIELD_COUNTED) ||
      field->scalar_kind != DATABIND_BINARY_SCALAR_NONE)
    return NULL;
  for (i = 0u; i < record->field_count; ++i) {
    const IdlField *member = &record->fields[i];
    const IdlDataDecl *child;
    if (strcmp(member->name, field->field_id) != 0) continue;
    if (field->kind == DATABIND_BINARY_FIELD_GROUP) {
      if (member->collection_kind != IDL_COLLECTION_GROUP) return NULL;
      child = idl_contract_find_data(contract, member->inner_type);
      return child != NULL && child->kind == IDL_DATA_GROUP ? child->name : NULL;
    }
    if (field->kind == DATABIND_BINARY_FIELD_COUNTED) {
      if (field->element_scalar_kind != DATABIND_BINARY_SCALAR_NONE) return NULL;
      child = idl_contract_find_data(contract, member->collection_kind == IDL_COLLECTION_MAP ?
                                              member->value_type : member->inner_type);
    } else if (field->array_count != 0u) {
      if (member->collection_kind != IDL_COLLECTION_ARRAY) return NULL;
      child = idl_contract_find_data(contract, member->inner_type);
    } else {
      if (member->collection_kind != IDL_COLLECTION_NONE || member->length != NULL) return NULL;
      child = idl_contract_find_data(contract, member->type_name);
    }
    return child != NULL && (child->kind == IDL_DATA_COMPOSITE || child->kind == IDL_DATA_MESSAGE)
               ? child->name : NULL;
  }
  return NULL;
}

static cserde_token_kind lowering_token(databind_binary_scalar_kind kind) {
  switch (kind) {
  case DATABIND_BINARY_SCALAR_BOOL: return CSERDE_BOOL;
  case DATABIND_BINARY_SCALAR_SINT:
  case DATABIND_BINARY_SCALAR_ENUM_SINT: return CSERDE_SINT;
  case DATABIND_BINARY_SCALAR_UINT:
  case DATABIND_BINARY_SCALAR_ENUM_UINT: return CSERDE_UINT;
  case DATABIND_BINARY_SCALAR_FLOAT: return CSERDE_FLOAT;
  case DATABIND_BINARY_SCALAR_STRING: return CSERDE_STRING;
  case DATABIND_BINARY_SCALAR_BYTES: return CSERDE_BYTES;
  default: return CSERDE_NULL;
  }
}

static unsigned lowering_enum_flags(databind_binary_scalar_kind kind) {
  return kind == DATABIND_BINARY_SCALAR_ENUM_SINT || kind == DATABIND_BINARY_SCALAR_ENUM_UINT
             ? DATA_BIND_BINARY_FIELD_ENUM_BITS : 0u;
}

static databind_binary_layout_status lowering_node_plan_init(
    binary_execution_node *node, databind_binary_layout_diagnostic *diagnostic) {
  const size_t count = node->layout.field_count;
  if (count != 0u) {
    if (count > SIZE_MAX / sizeof(*node->fields) || count > SIZE_MAX / sizeof(*node->arrays) ||
        count > SIZE_MAX / sizeof(*node->array_table) || count > SIZE_MAX / sizeof(*node->children))
      return lowering_fail(diagnostic, node->layout.type_id, "Binary execution graph table size overflows");
    node->fields = (DataBindBinaryFieldPlan *)calloc(count, sizeof(*node->fields));
    node->arrays = (DataBindBinaryArrayPlan *)calloc(count, sizeof(*node->arrays));
    node->array_table = (const DataBindBinaryArrayPlan **)calloc(count, sizeof(*node->array_table));
    node->children = (const DataBindBinaryLayoutPlan **)calloc(count, sizeof(*node->children));
    if (node->fields == NULL || node->arrays == NULL || node->array_table == NULL || node->children == NULL)
      return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;
  }
  node->plan = (DataBindBinaryLayoutPlan)DATA_BIND_BINARY_LAYOUT_PLAN_INIT;
  node->plan.type_name = node->layout.type_id;
  node->plan.wire_big_endian = node->layout.wire_big_endian;
  node->plan.fixed_block_size = node->layout.fixed_block_size;
  node->plan.presence_offset = node->layout.presence_offset;
  node->plan.presence_size = node->layout.presence_size;
  node->plan.null_offset = node->layout.null_offset;
  node->plan.null_size = node->layout.null_size;
  node->plan.fields = node->fields;
  node->plan.field_count = count;
  node->plan.child_plans = node->children;
  node->plan.array_plans = node->array_table;
  return DATABIND_BINARY_LAYOUT_OK;
}

/* Each type is lowered once; linear type resolution gives O(V * (V + E))
 * control-plane time and O(V + E) retained metadata, under explicit budgets. */
static databind_binary_layout_status lowering_node_build(
    databind_binary_execution_graph *graph, const IdlContract *contract,
    const databind_binary_format_plan *format, const char *type_name,
    size_t depth, binary_execution_node **out,
    databind_binary_layout_diagnostic *diagnostic) {
  binary_execution_node *node;
  databind_binary_layout_status status;
  size_t index, i, count;
  if (depth >= DATA_BIND_BINARY_LAYOUT_MAX_DEPTH)
    return lowering_fail(diagnostic, type_name, "Binary execution graph exceeds its depth budget");
  for (index = 0u; index < format->type_count; ++index)
    if (format->types[index].name != NULL && strcmp(format->types[index].name, type_name) == 0) break;
  if (index == format->type_count) return DATABIND_BINARY_LAYOUT_TYPE_NOT_FOUND;
  node = &graph->nodes[index];
  if (node->state == 1u)
    return lowering_fail(diagnostic, type_name, "Binary execution graph contains a cycle");
  if (node->state == 2u) {
    *out = node;
    return DATABIND_BINARY_LAYOUT_OK;
  }
  if (format->types[index].field_count > DATABIND_BINARY_LOWERING_MAX_FIELDS - graph->field_count)
    return lowering_fail(diagnostic, type_name, "Binary execution graph exceeds its field budget");
  node->state = 1u;
  node->height = 1u;
  status = databind_binary_layout_build(contract, format, type_name, &node->layout, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) return status;
  count = node->layout.field_count;
  if (count > DATABIND_BINARY_LOWERING_MAX_FIELDS - graph->field_count)
    return lowering_fail(diagnostic, type_name, "Binary execution graph exceeds its field budget");
  graph->field_count += count;
  status = lowering_node_plan_init(node, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) return status;
  for (i = 0u; i < count; ++i) {
    const databind_binary_field_layout *field = &node->layout.fields[i];
    DataBindBinaryFieldPlan *lowered = &node->fields[i];
    const char *child_type = databind_binary_execution_child_type(contract, type_name, field);
    binary_execution_node *child = NULL;
    size_t extra_depth = field->array_count != 0u || field->kind == DATABIND_BINARY_FIELD_GROUP ||
                        field->kind == DATABIND_BINARY_FIELD_COUNTED ? 2u : 1u;
    *lowered = (DataBindBinaryFieldPlan)DATA_BIND_BINARY_FIELD_PLAN_INIT;
    lowered->field_name = field->field_id;
    lowered->token_kind = lowering_token(field->scalar_kind);
    lowered->scalar_bits = field->scalar_bits;
    lowered->wire_offset = field->wire_offset;
    lowered->wire_extent = field->wire_extent;
    lowered->optional_bit = field->optional_bit;
    lowered->nullable_bit = field->nullable_bit;
    lowered->flags = lowering_enum_flags(field->scalar_kind);
    if ((field->flags & DATABIND_BINARY_FIELD_OPTIONAL) != 0u) lowered->flags |= DATA_BIND_BINARY_FIELD_OPTIONAL;
    if ((field->flags & DATABIND_BINARY_FIELD_NULLABLE) != 0u) lowered->flags |= DATA_BIND_BINARY_FIELD_NULLABLE;
    if (child_type != NULL) {
      size_t expected_extent = field->kind == DATABIND_BINARY_FIELD_GROUP
                                   ? field->child_fixed_block_size
                                   : (field->array_count != 0u ? field->element_extent : field->wire_extent);
      status = lowering_node_build(graph, contract, format, child_type, depth + extra_depth, &child, diagnostic);
      if (status != DATABIND_BINARY_LAYOUT_OK) return status;
      if (child->has_tail || (field->kind != DATABIND_BINARY_FIELD_COUNTED &&
                             child->plan.fixed_block_size != expected_extent) ||
          child->plan.wire_big_endian != node->plan.wire_big_endian)
        return lowering_fail(diagnostic, field->field_id, "Binary fixed child extent or byte order disagrees");
      node->children[i] = &child->plan;
      if (child->height + extra_depth > node->height) node->height = child->height + extra_depth;
      lowered->token_kind = field->kind == DATABIND_BINARY_FIELD_GROUP ? CSERDE_ARRAY_BEGIN : CSERDE_MAP_BEGIN;
    }
    if (field->kind == DATABIND_BINARY_FIELD_FIXED ||
        field->kind == DATABIND_BINARY_FIELD_CURSOR_FIXED) {
      if (field->kind == DATABIND_BINARY_FIELD_CURSOR_FIXED) {
        lowered->representation = DATA_BIND_BINARY_REP_CURSOR_FIXED;
        node->has_tail = 1;
      }
      if (field->array_count != 0u) {
        DataBindBinaryArrayPlan *array = &node->arrays[i];
        *array = (DataBindBinaryArrayPlan){sizeof(*array), field->array_count, field->element_extent,
            child != NULL ? CSERDE_MAP_BEGIN : lowering_token(field->element_scalar_kind),
            field->element_scalar_bits, lowering_enum_flags(field->element_scalar_kind)};
        if (array->element_token_kind == CSERDE_NULL)
          return lowering_fail(diagnostic, field->field_id, "Binary fixed array element has no canonical token");
        lowered->token_kind = CSERDE_ARRAY_BEGIN;
        node->array_table[i] = array;
        if (node->height < 2u) node->height = 2u;
      } else if (lowered->token_kind == CSERDE_NULL ||
                 (child == NULL && field->scalar_kind != DATABIND_BINARY_SCALAR_BYTES &&
                  (field->scalar_bits == 0u || field->wire_extent != field->scalar_bits / 8u))) {
        return lowering_fail(diagnostic, field->field_id, "Binary fixed field has no canonical representation");
      }
    } else if (field->kind == DATABIND_BINARY_FIELD_COUNTED) {
      DataBindBinaryArrayPlan *array = &node->arrays[i];
      *array = (DataBindBinaryArrayPlan){sizeof(*array), 0u,
          child != NULL ? child->plan.fixed_block_size : field->element_extent,
          child != NULL ? CSERDE_MAP_BEGIN : lowering_token(field->element_scalar_kind), field->element_scalar_bits,
          lowering_enum_flags(field->element_scalar_kind)};
      if (array->element_token_kind == CSERDE_NULL)
        return lowering_fail(diagnostic, field->field_id, "Binary counted element representation is unsupported");
      lowered->token_kind = field->key_scalar_kind == DATABIND_BINARY_SCALAR_STRING ?
                                CSERDE_MAP_BEGIN : CSERDE_ARRAY_BEGIN;
      lowered->representation = DATA_BIND_BINARY_REP_COUNTED;
      lowered->tail_prefix_bytes = field->tail_prefix_bytes;
      node->array_table[i] = array;
      node->has_tail = 1;
      if (node->height < 2u) node->height = 2u;
    } else if (field->kind == DATABIND_BINARY_FIELD_GROUP) {
      if (child == NULL || child->plan.fixed_block_size == 0u || child->plan.fixed_block_size > UINT16_MAX ||
          field->tail_prefix_bytes != DATA_BIND_BINARY_GROUP_HEADER_SIZE)
        return lowering_fail(diagnostic, field->field_id, "Binary GROUP has no fixed canonical child");
      lowered->representation = DATA_BIND_BINARY_REP_GROUP;
      lowered->tail_prefix_bytes = field->tail_prefix_bytes;
      node->has_tail = 1;
    } else if (field->kind == DATABIND_BINARY_FIELD_VAR_DATA) {
      if ((lowered->token_kind != CSERDE_STRING && lowered->token_kind != CSERDE_BYTES) ||
          field->scalar_bits != 0u || field->tail_prefix_bytes != sizeof(uint32_t))
        return lowering_fail(diagnostic, field->field_id, "Binary variable field has no canonical representation");
      lowered->representation = DATA_BIND_BINARY_REP_VAR_DATA;
      lowered->tail_prefix_bytes = field->tail_prefix_bytes;
      node->has_tail = 1;
    } else {
      return lowering_fail(diagnostic, field->field_id, "Binary field representation is unsupported");
    }
  }
  if (node->height > DATA_BIND_BINARY_LAYOUT_MAX_DEPTH ||
      (node->plan.fixed_block_size == 0u && !node->has_tail))
    return lowering_fail(diagnostic, type_name, "Binary execution graph is empty or exceeds its depth budget");
  node->state = 2u;
  *out = node;
  return DATABIND_BINARY_LAYOUT_OK;
}

databind_binary_layout_status databind_binary_execution_graph_build(
    const IdlContract *contract, const databind_binary_format_plan *format_plan,
    const char *type_name, databind_binary_execution_graph **out,
    databind_binary_layout_diagnostic *diagnostic) {
  databind_binary_execution_graph *graph;
  binary_execution_node *root = NULL;
  databind_binary_layout_status status;
  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (out != NULL) *out = NULL;
  if (contract == NULL || format_plan == NULL || out == NULL || type_name == NULL || type_name[0] == '\0')
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;
  if (format_plan->type_count > DATABIND_BINARY_LOWERING_MAX_TYPES ||
      format_plan->type_count > SIZE_MAX / sizeof(binary_execution_node) ||
      (format_plan->type_count != 0u && format_plan->types == NULL))
    return lowering_fail(diagnostic, type_name, "Binary execution graph has invalid type capacity");
  graph = (databind_binary_execution_graph *)calloc(1u, sizeof(*graph));
  if (graph == NULL) return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;
  graph->node_count = format_plan->type_count;
  if (graph->node_count != 0u) {
    graph->nodes = (binary_execution_node *)calloc(graph->node_count, sizeof(*graph->nodes));
    if (graph->nodes == NULL) {
      free(graph);
      return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;
    }
  }
  status = lowering_node_build(graph, contract, format_plan, type_name, 0u, &root, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) {
    databind_binary_execution_graph_destroy(graph);
    return status;
  }
  graph->root = &root->plan;
  *out = graph;
  return DATABIND_BINARY_LAYOUT_OK;
}

const DataBindBinaryLayoutPlan *databind_binary_execution_graph_root(
    const databind_binary_execution_graph *graph) {
  return graph != NULL ? graph->root : NULL;
}

void databind_binary_execution_graph_destroy(databind_binary_execution_graph *graph) {
  size_t i;
  if (graph == NULL) return;
  for (i = 0u; i < graph->node_count; ++i) {
    binary_execution_node *node = &graph->nodes[i];
    free(node->fields);
    free(node->arrays);
    free(node->array_table);
    free(node->children);
    databind_binary_layout_destroy(&node->layout);
  }
  free(graph->nodes);
  free(graph);
}
