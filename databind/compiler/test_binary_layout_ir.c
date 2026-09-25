#include "binary_layout_ir.h"
#include "compiler_core.h"

#include <tinytest.h>

#include <stdlib.h>
#include <string.h>

spec("DataBind BinaryLayoutIR") {
  it("builds LoginMessage wire layout from canonical compiler IR") {
    Node *root = NULL;
    char *schema_data = NULL;
    databind_binary_type_layout layout = {0};
    databind_binary_layout_diagnostic diagnostic = {0};

    check_equal(
        tbe_compiler_parse_schema_file(
            SCHEMA_EXAMPLE_FILE, &root, &schema_data), 0);
    check_not_null(root);
    tbe_compiler_annotate_language_types(root);

    check_equal(
        databind_binary_layout_build(
            root, "LoginMessage", &layout, &diagnostic),
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
    check_equal(layout.fields[2].length_prefix_bytes, (size_t)4u);

    databind_binary_layout_destroy(&layout);
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

  it("rejects fixed fields after Binary tail fields") {
    databind_binary_field_layout fields[2] = {
        {.field_id = "payload",
         .kind = DATABIND_BINARY_FIELD_VAR_DATA,
         .length_prefix_bytes = 4u},
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
