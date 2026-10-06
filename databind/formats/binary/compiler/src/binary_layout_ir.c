#include "binary_layout_ir.h"
#include "schema_cmeta.h"
#include "schema_size.h"
#include <salts_cmeta_data.h>

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

static int binary_scalar_bits_valid(
    databind_binary_scalar_kind kind, unsigned bits) {
  switch (kind) {
  case DATABIND_BINARY_SCALAR_NONE:
  case DATABIND_BINARY_SCALAR_STRING:
  case DATABIND_BINARY_SCALAR_BYTES:
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
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  schema_cmeta_field_type semantic;
  const cmeta_data_desc *data = NULL;
  const char *field_name;
  const char *declared_type;
  unsigned bits = 0u;

  if (contract == NULL || typed_field == NULL || field == NULL)
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;

  field_name = typed_field->name;
  field->scalar_kind = DATABIND_BINARY_SCALAR_NONE;
  field->scalar_bits = 0u;

  if (!schema_cmeta_field_resolve(contract, typed_field, &semantic)) {
    binary_diag(diagnostic, field_name,
                "Binary scalar semantics cannot be resolved from canonical schema");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  switch (semantic.kind) {
  case CMETA_DATA_STRING:
    field->scalar_kind = DATABIND_BINARY_SCALAR_STRING;
    break;
  case CMETA_DATA_BYTES:
    field->scalar_kind = DATABIND_BINARY_SCALAR_BYTES;
    break;
  case CMETA_DATA_CUSTOM:
    if (!salts_uuid_cmeta_data_valid(semantic.data))
      return DATABIND_BINARY_LAYOUT_OK;
    field->scalar_kind = DATABIND_BINARY_SCALAR_BYTES;
    break;
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
     * Structural/custom semantics are intentionally not inferred into a
     * scalar token class. Record lowering resolves canonical Contract children;
     * other SCALAR_NONE forms remain fail-closed.
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

static IdlField binary_element_field(const IdlField *field, const char *type_name) {
  IdlField element = *field;
  element.type_name = type_name;
  element.collection_kind = IDL_COLLECTION_NONE;
  element.inner_type = NULL;
  element.key_type = NULL;
  element.value_type = NULL;
  element.length = NULL;
  element.optional = 0;
  element.nullable = 0;
  return element;
}

static databind_binary_layout_status binary_field_array_representation(
    const IdlContract *contract, const IdlField *typed_field,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  IdlField element = binary_element_field(typed_field, typed_field->inner_type);
  databind_binary_field_layout element_layout = {0};
  databind_binary_layout_status status;
  size_t count = 0u;
  if (typed_field->inner_type == NULL ||
      !schema_parse_fixed_layout_size(typed_field->length, &count) || count == 0u ||
      field->wire_extent == 0u || field->wire_extent % count != 0u) {
    binary_diag(diagnostic, typed_field->name,
                "Fixed Binary array count disagrees with its wire extent");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }
  status = binary_field_scalar_representation(contract, &element,
                                               &element_layout, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) return status;
  field->array_count = count;
  field->element_extent = field->wire_extent / count;
  field->element_scalar_kind = element_layout.scalar_kind;
  field->element_scalar_bits = element_layout.scalar_bits;
  return DATABIND_BINARY_LAYOUT_OK;
}

static databind_binary_layout_status binary_field_counted_representation(
    const IdlContract *contract, const IdlField *typed_field,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  const int map = typed_field->collection_kind == IDL_COLLECTION_MAP;
  const char *element_type = map ? typed_field->value_type : typed_field->inner_type;
  IdlField element = binary_element_field(typed_field, element_type);
  databind_binary_field_layout semantic = {0};
  databind_binary_layout_status status;
  if ((!map && typed_field->collection_kind != IDL_COLLECTION_LIST &&
               typed_field->collection_kind != IDL_COLLECTION_SET) || element_type == NULL) {
    binary_diag(diagnostic, typed_field->name, "Counted Binary field has no canonical collection element");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }
  status = binary_field_scalar_representation(contract, &element, &semantic, diagnostic);
  if (status != DATABIND_BINARY_LAYOUT_OK) return status;
  field->element_scalar_kind = semantic.scalar_kind;
  field->element_scalar_bits = semantic.scalar_bits;
  field->element_extent = semantic.scalar_bits / 8u;
  if (map) {
    IdlField key = binary_element_field(typed_field, typed_field->key_type);
    semantic = (databind_binary_field_layout){0};
    if (typed_field->key_type == NULL)
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    status = binary_field_scalar_representation(contract, &key, &semantic, diagnostic);
    if (status != DATABIND_BINARY_LAYOUT_OK) return status;
    if (semantic.scalar_kind != DATABIND_BINARY_SCALAR_STRING) {
      binary_diag(diagnostic, typed_field->name, "Counted Binary maps require canonical string keys");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    field->key_scalar_kind = semantic.scalar_kind;
    field->key_scalar_bits = semantic.scalar_bits;
  }
  return DATABIND_BINARY_LAYOUT_OK;
}

static databind_binary_layout_status binary_field_var_data_representation(
    const IdlContract *contract,
    const IdlField *typed_field,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  schema_cmeta_field_type semantic;
  const char *field_name;

  if (contract == NULL || typed_field == NULL || field == NULL)
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;

  field_name = typed_field->name;
  field->scalar_kind = DATABIND_BINARY_SCALAR_NONE;
  field->scalar_bits = 0u;

  if (!schema_cmeta_field_resolve(contract, typed_field, &semantic)) {
    binary_diag(
        diagnostic, field_name,
        "Binary VAR_DATA semantics cannot be resolved from canonical schema");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  if (semantic.kind == CMETA_DATA_STRING)
    field->scalar_kind = DATABIND_BINARY_SCALAR_STRING;
  else if (semantic.kind == CMETA_DATA_BYTES)
    field->scalar_kind = DATABIND_BINARY_SCALAR_BYTES;

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
    case DATABIND_BINARY_FIELD_FIXED:
    case DATABIND_BINARY_FIELD_COUNTED:
    case DATABIND_BINARY_FIELD_CURSOR_FIXED: return 0;
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
  int cursor_fixed = 0;

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
        (i != 0u && rank < previous_rank && !cursor_fixed &&
         field->kind != DATABIND_BINARY_FIELD_COUNTED)) {
      binary_diag(diagnostic, field->field_id,
                  "Binary field order or identity is invalid");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }
    previous_rank = rank;
    if ((field->kind == DATABIND_BINARY_FIELD_FIXED && cursor_fixed) ||
        (field->kind == DATABIND_BINARY_FIELD_CURSOR_FIXED && !cursor_fixed)) {
      binary_diag(diagnostic, field->field_id, "Binary fixed field addressing disagrees with its collection cursor");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
    }

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

    if (field->kind == DATABIND_BINARY_FIELD_FIXED || field->kind == DATABIND_BINARY_FIELD_CURSOR_FIXED) {
      if (field->wire_extent == 0u ||
          (field->kind == DATABIND_BINARY_FIELD_FIXED &&
           (!binary_size_add(field->wire_offset, field->wire_extent, &end) ||
            end > layout->fixed_block_size ||
            binary_ranges_overlap(field->wire_offset, field->wire_extent, 0u, state_extent))) ||
          (field->kind == DATABIND_BINARY_FIELD_CURSOR_FIXED && field->wire_offset != 0u)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary field range is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (field->array_count != 0u &&
          (field->scalar_kind != DATABIND_BINARY_SCALAR_NONE || field->scalar_bits != 0u ||
           field->element_extent == 0u ||
           field->array_count > SIZE_MAX / field->element_extent ||
           field->array_count * field->element_extent != field->wire_extent ||
           field->element_scalar_kind == DATABIND_BINARY_SCALAR_STRING ||
           !binary_scalar_bits_valid(field->element_scalar_kind, field->element_scalar_bits) ||
           (field->element_scalar_kind != DATABIND_BINARY_SCALAR_NONE &&
            field->element_scalar_kind != DATABIND_BINARY_SCALAR_BYTES &&
            field->element_extent != field->element_scalar_bits / 8u))) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary array representation disagrees with canonical semantics");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (!binary_scalar_bits_valid(
              field->scalar_kind, field->scalar_bits) || field->scalar_kind == DATABIND_BINARY_SCALAR_STRING) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary scalar representation is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
      if (field->scalar_kind != DATABIND_BINARY_SCALAR_NONE &&
          field->scalar_kind != DATABIND_BINARY_SCALAR_BYTES &&
          field->wire_extent != (size_t)(field->scalar_bits / 8u)) {
        binary_diag(diagnostic, field->field_id,
                    "Fixed Binary scalar width disagrees with canonical semantics");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    } else if (field->kind == DATABIND_BINARY_FIELD_COUNTED) {
      cursor_fixed = 1;
      if (field->tail_prefix_bytes != sizeof(uint32_t) || field->wire_offset != 0u ||
          field->wire_extent != 0u || field->child_fixed_block_size != 0u ||
          field->scalar_kind != DATABIND_BINARY_SCALAR_NONE || field->scalar_bits != 0u ||
          field->array_count != 0u ||
          !binary_scalar_bits_valid(field->element_scalar_kind, field->element_scalar_bits) ||
          field->element_extent != field->element_scalar_bits / 8u ||
          (field->key_scalar_kind != DATABIND_BINARY_SCALAR_NONE &&
           field->key_scalar_kind != DATABIND_BINARY_SCALAR_STRING) || field->key_scalar_bits != 0u) {
        binary_diag(diagnostic, field->field_id, "Counted Binary framing disagrees with canonical element semantics");
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
          field->wire_extent != 0u ||
          field->scalar_bits != 0u ||
          (field->scalar_kind != DATABIND_BINARY_SCALAR_STRING &&
           field->scalar_kind != DATABIND_BINARY_SCALAR_BYTES)) {
        binary_diag(diagnostic, field->field_id,
                    "Binary variable-data layout is invalid");
        return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
      }
    }
    if ((field->array_count != 0u && field->kind != DATABIND_BINARY_FIELD_FIXED &&
         field->kind != DATABIND_BINARY_FIELD_CURSOR_FIXED) ||
        (field->array_count == 0u && field->kind != DATABIND_BINARY_FIELD_COUNTED &&
         (field->element_extent != 0u || field->element_scalar_bits != 0u ||
          field->element_scalar_kind != DATABIND_BINARY_SCALAR_NONE)) ||
        (field->kind != DATABIND_BINARY_FIELD_COUNTED &&
         (field->key_scalar_kind != DATABIND_BINARY_SCALAR_NONE || field->key_scalar_bits != 0u))) {
      binary_diag(diagnostic, field->field_id, "Unexpected Binary array element metadata");
      return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
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
    const databind_binary_format_field_plan *wire_field,
    databind_binary_field_layout *field,
    databind_binary_layout_diagnostic *diagnostic) {
  const char *name;
  const IdlField *typed_field;

  if (contract == NULL || typed_record == NULL ||
      wire_field == NULL || field == NULL)
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;

  name = wire_field->name;
  typed_field = binary_typed_field(typed_record, name);
  if (name == NULL || name[0] == '\0' || typed_field == NULL) {
    binary_diag(diagnostic, name,
                "Typed Contract IR and TBE wire field disagree");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  field->field_id = binary_strdup(name);
  if (field->field_id == NULL)
    return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;

  if ((wire_field->flags & DATABIND_BINARY_FORMAT_FIELD_OPTIONAL) != 0u) {
    field->flags |= DATABIND_BINARY_FIELD_OPTIONAL;
    field->optional_bit = wire_field->optional_bit;
  }
  if ((wire_field->flags & DATABIND_BINARY_FORMAT_FIELD_NULLABLE) != 0u) {
    field->flags |= DATABIND_BINARY_FIELD_NULLABLE;
    field->nullable_bit = wire_field->nullable_bit;
  }

  switch (wire_field->kind) {
  case DATABIND_BINARY_FORMAT_FIELD_GROUP:
    field->kind = DATABIND_BINARY_FIELD_GROUP;
    field->child_fixed_block_size = wire_field->child_fixed_block_size;
    field->tail_prefix_bytes = wire_field->tail_prefix_bytes;
    return DATABIND_BINARY_LAYOUT_OK;

  case DATABIND_BINARY_FORMAT_FIELD_VAR_DATA:
    field->kind = DATABIND_BINARY_FIELD_VAR_DATA;
    field->tail_prefix_bytes = wire_field->tail_prefix_bytes;
    return binary_field_var_data_representation(
        contract, typed_field, field, diagnostic);

  case DATABIND_BINARY_FORMAT_FIELD_COUNTED:
    field->kind = DATABIND_BINARY_FIELD_COUNTED;
    field->tail_prefix_bytes = wire_field->tail_prefix_bytes;
    return binary_field_counted_representation(contract, typed_field, field, diagnostic);

  case DATABIND_BINARY_FORMAT_FIELD_CURSOR_FIXED:
  case DATABIND_BINARY_FORMAT_FIELD_FIXED:
    field->kind = wire_field->kind == DATABIND_BINARY_FORMAT_FIELD_FIXED
                      ? DATABIND_BINARY_FIELD_FIXED : DATABIND_BINARY_FIELD_CURSOR_FIXED;
    field->wire_offset = wire_field->wire_offset;
    field->wire_extent = wire_field->wire_extent;
    if (typed_field->collection_kind == IDL_COLLECTION_ARRAY)
      return binary_field_array_representation(contract, typed_field, field, diagnostic);
    return binary_field_scalar_representation(
        contract, typed_field, field, diagnostic);

  default:
    binary_diag(diagnostic, name, "Unknown TBE wire field kind");
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }
}

databind_binary_layout_status databind_binary_layout_build(
    const IdlContract *contract,
    const databind_binary_format_plan *format_plan,
    const char *type_name,
    databind_binary_type_layout *out_layout,
    databind_binary_layout_diagnostic *diagnostic) {
  databind_binary_type_layout candidate = {0};
  const IdlDataDecl *typed_record;
  const databind_binary_format_type_plan *wire_type;
  size_t i;
  databind_binary_layout_status status;

  if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
  if (contract == NULL || format_plan == NULL ||
      type_name == NULL || type_name[0] == '\0' ||
      out_layout == NULL) {
    binary_diag(diagnostic, NULL, "Invalid Binary layout build arguments");
    return DATABIND_BINARY_LAYOUT_INVALID_ARGUMENT;
  }

  typed_record = idl_contract_find_data(contract, type_name);
  wire_type = databind_binary_format_plan_find_type(format_plan, type_name);
  if (typed_record == NULL || wire_type == NULL) {
    binary_diag(diagnostic, NULL, "Binary layout type was not found");
    return DATABIND_BINARY_LAYOUT_TYPE_NOT_FOUND;
  }

  candidate.type_id = binary_strdup(type_name);
  if (candidate.type_id == NULL)
    return DATABIND_BINARY_LAYOUT_OUT_OF_MEMORY;

  candidate.fixed_block_size = wire_type->fixed_block_size;
  candidate.presence_offset = 0u;
  candidate.presence_size = wire_type->presence_size;
  candidate.null_offset = candidate.presence_size;
  candidate.null_size = wire_type->null_size;
  candidate.wire_big_endian = wire_type->wire_big_endian;
  candidate.field_count = wire_type->field_count;

  if (typed_record->field_count != candidate.field_count) {
    binary_diag(diagnostic, NULL,
                "Typed Contract IR and TBE wire plan disagree on field count");
    databind_binary_layout_destroy(&candidate);
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
  }

  if ((candidate.field_count != 0u && wire_type->fields == NULL) ||
      candidate.field_count > SIZE_MAX / sizeof(*candidate.fields)) {
    binary_diag(diagnostic, NULL, "Binary field table is invalid or exceeds its size budget");
    databind_binary_layout_destroy(&candidate);
    return DATABIND_BINARY_LAYOUT_INVALID_SCHEMA;
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
        contract, typed_record, &wire_type->fields[i],
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
}
