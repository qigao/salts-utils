#include "binary_layout_ir.h"
#include "compiler_core.h"
#include "tbe_contract_overlay.h"

#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

spec("DataBind BinaryLayoutIR") {
  it("derives fixed scalar token representation from canonical CMeta semantics") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;

    check_equal(
        databind_compiler_parse_contract_file(
            BINARY_SCALAR_SCHEMA, &root, &contract, &schema_data), 0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    tbe_error_init(&format_error);
    check(databind_tbe_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_binary_layout_build(
            contract, &format_plan, "Scalars", &layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_OK);
    check_equal(layout.field_count, (size_t)10u);

    check_equal(layout.fields[0].scalar_kind, DATABIND_BINARY_SCALAR_BOOL);
    check_equal(layout.fields[0].scalar_bits, 8u);

    check_equal(layout.fields[1].scalar_kind, DATABIND_BINARY_SCALAR_SINT);
    check_equal(layout.fields[1].scalar_bits, 8u);

    check_equal(layout.fields[2].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[2].scalar_bits, 16u);

    check_equal(layout.fields[3].scalar_kind, DATABIND_BINARY_SCALAR_SINT);
    check_equal(layout.fields[3].scalar_bits, 32u);

    check_equal(layout.fields[4].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[4].scalar_bits, 64u);

    check_equal(layout.fields[5].scalar_kind, DATABIND_BINARY_SCALAR_FLOAT);
    check_equal(layout.fields[5].scalar_bits, 32u);

    check_equal(layout.fields[6].scalar_kind, DATABIND_BINARY_SCALAR_FLOAT);
    check_equal(layout.fields[6].scalar_bits, 64u);

    check_equal(layout.fields[7].scalar_kind,
                DATABIND_BINARY_SCALAR_ENUM_SINT);
    check_equal(layout.fields[7].scalar_bits, 16u);

    check_equal(layout.fields[8].scalar_kind,
                DATABIND_BINARY_SCALAR_ENUM_UINT);
    check_equal(layout.fields[8].scalar_bits, 8u);

    check_equal(layout.fields[9].scalar_kind, DATABIND_BINARY_SCALAR_UINT);
    check_equal(layout.fields[9].scalar_bits, 32u);
    check((layout.fields[9].flags & DATABIND_BINARY_FIELD_OPTIONAL) != 0u);
    check((layout.fields[9].flags & DATABIND_BINARY_FIELD_NULLABLE) != 0u);

    databind_binary_layout_destroy(&layout);
    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }


  it("builds LoginMessage wire layout from canonical compiler IR") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;

    check_equal(
        databind_compiler_parse_contract_file(
            SCHEMA_EXAMPLE_FILE, &root, &contract, &schema_data), 0);
    check_not_null(root);

    tbe_error_init(&format_error);
    check(databind_tbe_format_plan_build(
        contract, root, &format_plan, &format_error));
    check_equal(
        databind_binary_layout_build(
            contract, &format_plan, "LoginMessage", &layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_OK);
    check_equal(strcmp(layout.type_id, "LoginMessage"), 0);
    check_equal(layout.wire_big_endian, 0);
    check_equal(layout.fixed_block_size, (size_t)29u);
    check_equal(layout.presence_size, (size_t)0u);
    check_equal(layout.null_size, (size_t)0u);
    check_equal(layout.field_count, (size_t)3u);

    check_equal(strcmp(layout.fields[0].field_id, "header"), 0);
    check_equal(layout.fields[0].kind, DATABIND_BINARY_FIELD_FIXED);
    check_equal(layout.fields[0].wire_offset, (size_t)0u);
    check_equal(layout.fields[0].wire_extent, (size_t)13u);

    check_equal(strcmp(layout.fields[1].field_id, "pass_hash"), 0);
    check_equal(layout.fields[1].kind, DATABIND_BINARY_FIELD_FIXED);
    check_equal(layout.fields[1].wire_offset, (size_t)13u);
    check_equal(layout.fields[1].wire_extent, (size_t)16u);

    check_equal(strcmp(layout.fields[2].field_id, "username"), 0);
    check_equal(layout.fields[2].kind, DATABIND_BINARY_FIELD_VAR_DATA);
    check_equal(layout.fields[2].tail_prefix_bytes, (size_t)4u);

    databind_binary_layout_destroy(&layout);
    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects fixed wire overlap without consulting native layout") {
    databind_binary_field_layout fields[2] = {
        {.field_id = "a",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 0u,
         .wire_extent = 4u},
        {.field_id = "b",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 2u,
         .wire_extent = 4u}};
    databind_binary_type_layout layout = {
        .type_id = "Overlap",
        .fixed_block_size = 8u,
        .fields = fields,
        .field_count = 2u};
    databind_binary_layout_diagnostic diagnostic = {0};

    check_equal(
        databind_binary_layout_validate(&layout, &diagnostic),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
    check_equal(strcmp(diagnostic.field, "b"), 0);
  }

  it("keeps Binary presence/null state independent from host offsets") {
    databind_binary_field_layout field = {
        .field_id = "value",
        .kind = DATABIND_BINARY_FIELD_FIXED,
        .wire_offset = 2u,
        .wire_extent = 2u,
        .optional_bit = 0u,
        .nullable_bit = 0u,
        .flags = DATABIND_BINARY_FIELD_OPTIONAL |
                 DATABIND_BINARY_FIELD_NULLABLE};
    databind_binary_type_layout layout = {
        .type_id = "State",
        .fixed_block_size = 4u,
        .presence_offset = 0u,
        .presence_size = 1u,
        .null_offset = 1u,
        .null_size = 1u,
        .fields = &field,
        .field_count = 1u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_OK);

    field.wire_offset = 1u;
    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
  }

  it("rejects scalar wire extents that disagree with canonical token width") {
    databind_binary_field_layout field = {
        .field_id = "value",
        .kind = DATABIND_BINARY_FIELD_FIXED,
        .wire_offset = 0u,
        .wire_extent = 2u,
        .scalar_kind = DATABIND_BINARY_SCALAR_UINT,
        .scalar_bits = 32u};
    databind_binary_type_layout layout = {
        .type_id = "ScalarMismatch",
        .fixed_block_size = 2u,
        .fields = &field,
        .field_count = 1u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);

    field.wire_extent = 4u;
    layout.fixed_block_size = 4u;
    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_OK);
  }

  it("rejects fixed fields after Binary tail fields") {
    databind_binary_field_layout fields[2] = {
        {.field_id = "payload",
         .kind = DATABIND_BINARY_FIELD_VAR_DATA,
         .tail_prefix_bytes = 4u},
        {.field_id = "code",
         .kind = DATABIND_BINARY_FIELD_FIXED,
         .wire_offset = 0u,
         .wire_extent = 1u}};
    databind_binary_type_layout layout = {
        .type_id = "Ordering",
        .fixed_block_size = 1u,
        .fields = fields,
        .field_count = 2u};

    check_equal(
        databind_binary_layout_validate(&layout, NULL),
        DATABIND_BINARY_LAYOUT_INVALID_SCHEMA);
  }
}
