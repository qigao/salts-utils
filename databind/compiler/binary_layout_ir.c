#include "binary_layout_ir.h"
#include "schema_cmeta.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void binary_diag(
    databind_binary_layout_diagnostic *diagnostic,
    const char *field,
    const char *text) {
  if (diagnostic == NULL) return;
  snprintf(diagnostic->field, sizeof(diagnostic->field), "%s",
           field != NULL ? field : "");
  snprintf(diagnostic->text, sizeof(diagnostic->text), "%s",
           text != NULL ? text : "");
}

static const Node *binary_find_child(const Node *map, const char *name) {
  size_t i;
  if (map == NULL || name == NULL || map->type != NODE_MAP) return NULL;
  for (i = 0u; i < map->data.map.count; ++i) {
    const Node *child = map->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static const char *binary_string_value(const Node *map, const char *name) {
  const Node *child = binary_find_child(map, name);
  return child != NULL && child->type == NODE_STRING
             ? child->data.string_val
             : NULL;
}

static int binary_has_child(const Node *map, const char *name) {
  return binary_find_child(map, name) != NULL;
}

static int binary_parse_size(const char *text, size_t *out) {
  unsigned long long parsed;
  char *end = NULL;
  if (text == NULL || text[0] == '\0' || out == NULL) return 0;
  errno = 0;
  parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' ||
      parsed > (unsigned long long)SIZE_MAX)
    return 0;
  *out = (size_t)parsed;
  return 1;
}

static char *binary_strdup(const char *text) {
  size_t size;
  char *copy;
  if (text == NULL) return NULL;
  size = strlen(text);
  if (size == SIZE_MAX) return NULL;
  copy = (char *)malloc(size + 1u);
  if (copy == NULL) return NULL;
  memcpy(copy, text, size + 1u);
  return copy;
}

static const Node *binary_find_record_in(
    const Node *root, const char *list_name, const char *type_name) {
  const Node *list = binary_find_child(root, list_name);
  size_t i;
  if (list == NULL || list->type != NODE_LIST) return NULL;
  for (i = 0u; i < list->data.list.count; ++i) {
    const Node *record = list->data.list.items[i];
    const char *name = binary_string_value(record, "name");
    if (name != NULL && strcmp(name, type_name) == 0) return record;
  }
  return NULL;
}

static const Node *binary_find_record(
    const Node *root, const char *type_name) {
  const Node *record;
  record = binary_find_record_in(root, "composites", type_name);
  if (record == NULL) record = binary_find_record_in(root, "groups", type_name);
  if (record == NULL) record = binary_find_record_in(root, "messages", type_name);
  return record;
}

static const Node *binary_find_enum(
    const Node *root, const char *type_name) {
  const Node *enums = binary_find_child(root, "enums");
  size_t i;
  if (enums == NULL || enums->type != NODE_LIST ||
      type_name == NULL || type_name[0] == '\0')
    return NULL;
  for (i = 0u; i < enums->data.list.count; ++i) {
    const Node *candidate = enums->data.list.items[i];
    const char *name = binary_string_value(candidate, "enum_name");
    if (name != NULL && strcmp(name, type_name) == 0)
      return candidate;
  }
  return NULL;
}

static int binary_scalar_bits_valid(
    databind_binary_scalar_kind kind, unsigned bits) {
  switch (kind) {
  case DATABIND_BINARY_SCALAR_NONE:
    return bits == 0u;
  case DATABIND_BINARY_SCALAR_BOOL:
    return bits == 8u;
  case DATABIND_BINARY_SCALAR_FLOAT:
    return bits == 32u || bits == 64u;
  case DATABIND_BINARY_SCALAR_SINT:
  case DATABIND_BINARY_SCALAR_UINT:
  case DATABIND_BINARY_SCALAR_ENUM_SINT:
  case DATABIND_BINARY_SCALAR_ENUM_UINT:
    return bits == 8u || bits == 16u || bits == 32u || bits == 64u;
  default:
    return 0;
  }
}

static databind_binary_layout_status binary_field_scalar_representation(
    const IdlContract *contract,
    const IdlField *typed_field,
    const Node *field_node,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  schema_cmeta_field_type semantic;
  const cmeta_data_desc *data = NULL;
  const char *field_name;
  const char *declared_type;
  unsigned bits = 0u;

  if (contract == NULL || typed_field == NULL ||
      field_node == NULL || field == NULL)
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;

  field_name = binary_string_value(field_node, "name");
  field->scalar_kind = DATABIND_BINARY_SCALAR_NONE;
  field->scalar_bits = 0u;

  if (!schema_cmeta_field_resolve(contract, typed_field, &semantic)) {
    binary_diag(diagnostic, field_name,
                "Binary scalar semantics cannot be resolved from canonical schema");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  switch (semantic.kind) {
  case CMETA_DATA_BOOL:
    field->scalar_kind = DATABIND_BINARY_SCALAR_BOOL;
    bits = 8u;
    break;

  case CMETA_DATA_SINT:
  case CMETA_DATA_UINT:
    data = semantic.data;
    if (data == NULL || data->shape == NULL) {
      binary_diag(diagnostic, field_name,
                  "Canonical integer metadata is incomplete");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    bits = ((const cmeta_data_integer_shape *)data->shape)->bits;
    field->scalar_kind =
        semantic.kind == CMETA_DATA_SINT
            ? DATABIND_BINARY_SCALAR_SINT
            : DATABIND_BINARY_SCALAR_UINT;
    break;

  case CMETA_DATA_FLOAT:
    data = semantic.data;
    if (data == NULL || data->shape == NULL) {
      binary_diag(diagnostic, field_name,
                  "Canonical float metadata is incomplete");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    bits = ((const cmeta_data_float_shape *)data->shape)->bits;
    field->scalar_kind = DATABIND_BINARY_SCALAR_FLOAT;
    break;

  case CMETA_DATA_ENUM: {
    const IdlDataDecl *enum_decl;
    const char *underlying;
    declared_type = typed_field->type_name;
    enum_decl = idl_contract_find_data(contract, declared_type);
    underlying = enum_decl != NULL ? enum_decl->underlying_type : NULL;
    data = schema_cmeta_builtin_data(underlying);
    if (data == NULL || data->shape == NULL ||
        (data->kind != CMETA_DATA_SINT &&
         data->kind != CMETA_DATA_UINT)) {
      binary_diag(diagnostic, field_name,
                  "Canonical enum Binary storage is unavailable");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    bits = ((const cmeta_data_integer_shape *)data->shape)->bits;
    field->scalar_kind =
        data->kind == CMETA_DATA_SINT
            ? DATABIND_BINARY_SCALAR_ENUM_SINT
            : DATABIND_BINARY_SCALAR_ENUM_UINT;
    break;
  }

  default:
    /*
     * Structural/custom/buffer semantics are intentionally not inferred into a
     * scalar token class. Existing layout remains valid; the first generic
     * Binary reader will fail closed on SCALAR_NONE.
     */
    return DATABIND_BINARY_LAYOUT_OK;
  }

  field->scalar_bits = bits;
  if (!binary_scalar_bits_valid(field->scalar_kind, field->scalar_bits)) {
    binary_diag(diagnostic, field_name,
                "Canonical Binary scalar width is unsupported");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }
  return DATABIND_BINARY_LAYOUT_OK;
}

static int binary_size_add(size_t left, size_t right, size_t *out) {
  if (out == NULL || right > SIZE_MAX - left) return 0;
  *out = left + right;
  return 1;
}

static int binary_ranges_overlap(
    size_t left_offset, size_t left_size,
    size_t right_offset, size_t right_size) {
  size_t left_end;
  size_t right_end;
  if (left_size == 0u || right_size == 0u) return 0;
  if (!binary_size_add(left_offset, left_size, &left_end) ||
      !binary_size_add(right_offset, right_size, &right_end))
    return 1;
  return left_offset < right_end && right_offset < left_end;
}

static int binary_field_rank(databind_binary_field_layout_kind kind) {
  switch (kind) {
    case DATABIND_BINARY_FIELD_FIXED: return 0;
    case DATABIND_BINARY_FIELD_GROUP: return 1;
    case DATABIND_BINARY_FIELD_VAR_DATA: return 2;
    default: return -1;
  }
}

void databind_binary_layout_destroy(databind_binary_type_layout *layout) {
  size_t i;
  if (layout == NULL) return;
  if (layout->fields != NULL)
    for (i = 0u; i < layout->field_count; ++i)
      free(layout->fields[i].field_id);
  free(layout->fields);
  free(layout->type_id);
  memset(layout, 0, sizeof(*layout));
}

databind_binary_layout_status databind_binary_layout_validate(
    const databind_binary_type_layout *layout,
    databind_binary_layout_diagnostic *diagnostic) {
  size_t state_extent;
  size_t i;
  int previous_rank = 0;

  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (layout == NULL || layout->type_id == NULL || layout->type_id[0] == '\0' ||
      (layout->field_count != 0u && layout->fields == NULL)) {
    binary_diag(diagnostic, NULL, "Binary layout is incomplete");
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;
  }

  if (!binary_size_add(layout->presence_size, layout->null_size, &state_extent) ||
      layout->presence_offset != 0u ||
      layout->null_offset != layout->presence_size ||
      state_extent > layout->fixed_block_size) {
    binary_diag(diagnostic, NULL, "Binary state exceeds the fixed block");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  for (i = 0u; i < layout->field_count; ++i) {
    const databind_binary_field_layout *field = &layout->fields[i];
    int rank = binary_field_rank(field->kind);
    size_t end;
    size_t j;

    if (field->field_id == NULL || field->field_id[0] == '\0' || rank < 0 ||
        (i != 0u && rank < previous_rank)) {
      binary_diag(diagnostic, field->field_id,
                  "Binary field order or identity is invalid");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    previous_rank = rank;

    if ((field->flags & DATABIND_BINARY_FIELD_OPTIONAL) != 0u &&
        (layout->presence_size == 0u ||
         field->optional_bit / 8u >= layout->presence_size)) {
      binary_diag(diagnostic, field->field_id,
                  "Optional bit is outside the Binary presence state");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    if ((field->flags & DATABIND_BINARY_FIELD_NULLABLE) != 0u &&
        (layout->null_size == 0u ||
         field->nullable_bit / 8u >= layout->null_size)) {
      binary_diag(diagnostic, field->field_id,
                  "Nullable bit is outside the Binary null state");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }

    if (field->kind == DATABIND_BINARY_FIELD_FIXED) {
      if (field->wire_extent == 0u ||
          !binary_size_add(field->wire_offset, field->wire_extent, &end) ||
          end > layout->fixed_block_size ||
          binary_ranges_overlap(field->wire_offset, field->wire_extent,
                                0u, state_extent)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary field range is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (!binary_scalar_bits_valid(
              field->scalar_kind, field->scalar_bits)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary scalar representation is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (field->scalar_kind != DATABIND_BINARY_SCALAR_NONE &&
          field->wire_extent != (size_t)(field->scalar_bits / 8u)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary scalar width disagrees with canonical semantics");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    } else if (field->kind == DATABIND_BINARY_FIELD_GROUP) {
      if (field->tail_prefix_bytes != 4u ||
          field->child_fixed_block_size == 0u ||
          field->wire_extent != 0u) {
        binary_diag(diagnostic, field->field_id,
                    "Binary group layout is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    } else if (field->kind == DATABIND_BINARY_FIELD_VAR_DATA) {
      if (field->tail_prefix_bytes != 4u ||
          field->child_fixed_block_size != 0u ||
          field->wire_extent != 0u) {
        binary_diag(diagnostic, field->field_id,
                    "Binary variable-data layout is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    }

    for (j = 0u; j < i; ++j) {
      const databind_binary_field_layout *previous = &layout->fields[j];
      if (strcmp(previous->field_id, field->field_id) == 0) {
        binary_diag(diagnostic, field->field_id,
                    "Binary field identity is duplicated");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (field->kind == DATABIND_BINARY_FIELD_FIXED &&
          previous->kind == DATABIND_BINARY_FIELD_FIXED &&
          binary_ranges_overlap(field->wire_offset, field->wire_extent,
                                previous->wire_offset,
                                previous->wire_extent)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary field ranges overlap");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    }
  }

  return DATABIND_BINARY_LAYOUT_OK;
}

static const IdlField *binary_typed_field(
    const IdlDataDecl *record, const char *name) {
  size_t i;
  if (record == NULL || name == NULL) return NULL;
  for (i = 0u; i < record->field_count; ++i)
    if (record->fields[i].name != NULL &&
        strcmp(record->fields[i].name, name) == 0)
      return &record->fields[i];
  return NULL;
}

static databind_binary_layout_status binary_build_field(
    const IdlContract *contract,
    const IdlDataDecl *typed_record,
    const Node *root,
    const Node *field_node,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  const char *name = binary_string_value(field_node, "name");
  const IdlField *typed_field =
      binary_typed_field(typed_record, name);
  const char *text;

  if (name == NULL || name[0] == '\0') {
    binary_diag(diagnostic, NULL, "Binary field name is unavailable");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }
  field->field_id = binary_strdup(name);
  if (field->field_id == NULL)
    return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;

  if (binary_has_child(field_node, "is_optional")) {
    size_t bit;
    field->flags |= DATABIND_BINARY_FIELD_OPTIONAL;
    text = binary_string_value(field_node, "optional_bit_index");
    if (!binary_parse_size(text, &bit) || bit > (size_t)UINT_MAX) {
      binary_diag(diagnostic, name, "Optional field bit is invalid");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    field->optional_bit = (unsigned)bit;
  }
  if (binary_has_child(field_node, "is_nullable")) {
    size_t bit;
    field->flags |= DATABIND_BINARY_FIELD_NULLABLE;
    text = binary_string_value(field_node, "nullable_bit_index");
    if (!binary_parse_size(text, &bit) || bit > (size_t)UINT_MAX) {
      binary_diag(diagnostic, name, "Nullable field bit is invalid");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    field->nullable_bit = (unsigned)bit;
  }

  if (binary_has_child(field_node, "is_group_field")) {
    const char *group_type = binary_string_value(field_node, "group_type");
    const Node *group = group_type != NULL
                            ? binary_find_record(root, group_type)
                            : NULL;
    field->kind = DATABIND_BINARY_FIELD_GROUP;
    field->tail_prefix_bytes = 4u;
    if (group == NULL ||
        !binary_parse_size(binary_string_value(group, "fixed_block_size"),
                           &field->child_fixed_block_size)) {
      binary_diag(diagnostic, name, "Binary group child layout is unavailable");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    return DATABIND_BINARY_LAYOUT_OK;
  }

  if (binary_has_child(field_node, "is_var_data")) {
    field->kind = DATABIND_BINARY_FIELD_VAR_DATA;
    field->tail_prefix_bytes = 4u;
    return DATABIND_BINARY_LAYOUT_OK;
  }

  field->kind = DATABIND_BINARY_FIELD_FIXED;
  if (!binary_has_child(field_node, "has_offset") ||
      !binary_parse_size(binary_string_value(field_node, "offset"),
                         &field->wire_offset)) {
    binary_diag(diagnostic, name, "Fixed Binary field has no wire offset");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  text = binary_string_value(field_node, "field_size_bytes");
  if (text == NULL) text = binary_string_value(field_node, "size_bytes");
  if (!binary_parse_size(text, &field->wire_extent) ||
      field->wire_extent == 0u) {
    binary_diag(diagnostic, name, "Fixed Binary field has no wire extent");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  return binary_field_scalar_representation(
      contract, typed_field, field_node, field, diagnostic);
}

databind_binary_layout_status databind_binary_layout_build(
    const IdlContract *contract,
    const Node *wire_ir,
    const char *type_name,
    databind_binary_type_layout *out_layout,
    databind_binary_layout_diagnostic *diagnostic) {
  databind_binary_type_layout candidate = {0};
  const IdlDataDecl *typed_record;
  const Node *record;
  const Node *fields;
  const Node *schema;
  const char *text;
  size_t i;
  databind_binary_layout_status status;

  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (contract == NULL || wire_ir == NULL ||
      type_name == NULL || type_name[0] == '\0' ||
      out_layout == NULL) {
    binary_diag(diagnostic, NULL, "Invalid Binary layout build arguments");
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;
  }

  typed_record = idl_contract_find_data(contract, type_name);
  record = binary_find_record(wire_ir, type_name);
  if (typed_record == NULL || record == NULL) {
    binary_diag(diagnostic, NULL, "Binary layout type was not found");
    return DATABIND_BINARY_LAYOUT_TYPE_NOT_FOUND;
  }
  fields = binary_find_child(record, "fields");
  if (fields == NULL || fields->type != NODE_LIST ||
      !binary_parse_size(binary_string_value(record, "fixed_block_size"),
                         &candidate.fixed_block_size)) {
    binary_diag(diagnostic, NULL, "Binary record layout is incomplete");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  candidate.type_id = binary_strdup(type_name);
  if (candidate.type_id == NULL)
    return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;

  text = binary_string_value(record, "presence_bitmap_bytes");
  if (text != NULL && !binary_parse_size(text, &candidate.presence_size))
    goto invalid;
  text = binary_string_value(record, "null_bitmap_bytes");
  if (text != NULL && !binary_parse_size(text, &candidate.null_size))
    goto invalid;
  candidate.presence_offset = 0u;
  candidate.null_offset = candidate.presence_size;

  schema = binary_find_child(wire_ir, "schema");
  text = binary_string_value(schema, "schema_wire_big_endian_value");
  candidate.wire_big_endian =
      text != NULL && strcmp(text, "0") != 0;

  candidate.field_count = fields->data.list.count;
  if (typed_record->field_count != candidate.field_count) {
    binary_diag(diagnostic, NULL,
                "Typed Contract IR and TBE wire view disagree on field count");
    goto invalid;
  }
  if (candidate.field_count != 0u) {
    candidate.fields = (databind_binary_field_layout *)calloc(
        candidate.field_count, sizeof(*candidate.fields));
    if (candidate.fields == NULL) {
      databind_binary_layout_destroy(&candidate);
      return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;
    }
  }

  for (i = 0u; i < candidate.field_count; ++i) {
    status = binary_build_field(
        contract, typed_record, wire_ir, fields->data.list.items[i],
        &candidate.fields[i], diagnostic);
    if (status != DATABIND_BINARY_LAYOUT_OK) {
      databind_binary_layout_destroy(&candidate);
      return status;
    }
  }

  status = databind_binary_layout_validate(&candidate, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) {
    databind_binary_layout_destroy(&candidate);
    return status;
  }

  *out_layout = candidate;
  return DATABIND_BINARY_LAYOUT_OK;

invalid:
  binary_diag(diagnostic, NULL, "Binary record state metadata is invalid");
  databind_binary_layout_destroy(&candidate);
  return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
}
