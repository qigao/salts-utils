#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_multitu_fixture.h"
#include "tbe_cbind_test_fixtures.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static tbe_cbind_status create_one_with_options(
    const char *schema, size_t schema_size, const char *type_name,
    size_t type_name_size, const tbe_cbind_plan_options *options,
    tbe_cbind_plan_error *error) {
  tbe_cbind_plan *plan = (tbe_cbind_plan *)(uintptr_t)1u;
  tbe_cbind_status status = tbe_cbind_plan_create_from_text(
      schema, schema_size, type_name, type_name_size,
      &tbe_cbind_test_one_data, options, &plan, error);
  check_null(plan);
  return status;
}

static tbe_cbind_status create_one(const char *schema, const char *type_name,
                                   tbe_cbind_plan_error *error) {
  tbe_cbind_plan_options options;
  tbe_cbind_plan_options_init(&options);
  return create_one_with_options(schema, strlen(schema), type_name,
                                 strlen(type_name), &options, error);
}

typedef struct scalar_acceptance_case {
  const char *spelling;
  const cmeta_data_desc *value;
} scalar_acceptance_case;

static tbe_cbind_status create_scalar_slot(const char *spelling,
                                           const cmeta_data_desc *value,
                                           tbe_cbind_plan **out,
                                           tbe_cbind_plan_error *error) {
  char schema[96];
  int schema_size = snprintf(schema, sizeof(schema),
                             "message One { %s value; }", spelling);
  cmeta_field_desc layout_field = {
      "value", value->storage_type->name, 0u, value->storage_type->size,
      value->storage_type->align, value->storage_type, NULL};
  cmeta_struct_desc layout = {
      "tbe_cbind_test_scalar_slot", sizeof(tbe_cbind_test_scalar_slot),
      _Alignof(tbe_cbind_test_scalar_slot), &layout_field, 1u};
  cmeta_data_field_desc data_field = {
      "test.tbe-cbind.scalar-slot.value", "value", 0u, value};
  cmeta_data_struct_shape shape = {&layout, &data_field, 1u};
  cmeta_data_desc data = {
      sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
      "test.tbe-cbind.scalar-slot.data", "tbe_cbind_test_scalar_slot",
      CMETA_DATA_STRUCT, &tbe_cbind_test_scalar_slot_type, &shape, NULL};
  tbe_cbind_plan_options options;

  check_greater(schema_size, 0);
  check_less((size_t)schema_size, sizeof(schema));
  tbe_cbind_plan_options_init(&options);
  return tbe_cbind_plan_create_from_text(
      schema, (size_t)schema_size, "One", sizeof("One") - 1u, &data,
      &options, out, error);
}

spec("TbeCBind schema semantic model") {
  it("reports parser syntax errors with source coordinates") {
    tbe_cbind_plan_error error;
    tbe_cbind_plan_error_init(&error);

    check_equal(create_one("message Broken { int32 value", "Broken", &error),
                TBE_CBIND_SCHEMA_ERROR);
    check_equal(error.phase, TBE_CBIND_PHASE_PARSE);
    check(error.line >= 0);
    check(error.column >= 0);
  }

  it("rejects an unknown requested type exactly") {
    tbe_cbind_plan_error error;
    tbe_cbind_plan_error_init(&error);

    check_equal(create_one("message Known { int32 value; }", "known", &error),
                TBE_CBIND_TYPE_NOT_FOUND);
    check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
  }

  it("rejects duplicate empty and unportable semantic names") {
    static const char *const schemas[] = {
        "message One { [name(primary), name(other)] int32 value; }",
        "message One { [name(\"\")] int32 value; }",
        "message One { [name(\"nested.value\")] int32 value; }"};
    size_t index;

    for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
      tbe_cbind_plan_error error;
      tbe_cbind_plan_error_init(&error);
      check_equal(create_one(schemas[index], "One", &error),
                  TBE_CBIND_SCHEMA_ERROR);
      check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
      check_not_null(strstr(error.path, "One.value"));
    }
  }

  it("rejects mapped to mapped and mapped to canonical semantic collisions") {
    static const char *const schemas[] = {
        "message Pair { [name(shared)] int32 first; "
        "[name(shared)] int32 second; }",
        "message Pair { [name(second)] int32 first; int32 second; }"};
    size_t index;

    for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
      tbe_cbind_plan_error error;
      tbe_cbind_plan_error_init(&error);
      check_equal(create_one(schemas[index], "Pair", &error),
                  TBE_CBIND_SCHEMA_ERROR);
      check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
    }
  }

  it("rejects unsupported declarations attributes and field types") {
    static const char *const schemas[] = {
        "enum State { Ready; } message One { int32 value; }",
        "flags StateFlags { Ready; } message One { int32 value; }",
        "union Choice { int32 value; } message One { int32 value; }",
        "message One { [alias(old)] int32 value; }",
        "message One { [id(1)] int32 value; }",
        "message One { optional int32 value; }",
        "message One { int32 value default 7; }",
        "message One { bytes value; }",
        "message One { list<int32> value; }",
        "group Entry { int32 value; } "
        "message One { group<Entry> entries; }"};
    size_t index;

    for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
      tbe_cbind_plan_error error;
      tbe_cbind_plan_error_init(&error);
      check_equal(create_one(schemas[index], "One", &error),
                  TBE_CBIND_UNSUPPORTED);
      check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
    }
  }

  it("accepts every canonical scalar spelling and exact alias") {
    const scalar_acceptance_case cases[] = {
        {"bool", &cmeta_data_bool},
        {"int8", &turbo_int8_cmeta_data},
        {"int8_t", &turbo_int8_cmeta_data},
        {"i8", &turbo_int8_cmeta_data},
        {"uint8", &turbo_uint8_cmeta_data},
        {"uint8_t", &turbo_uint8_cmeta_data},
        {"u8", &turbo_uint8_cmeta_data},
        {"byte", &turbo_uint8_cmeta_data},
        {"int16", &turbo_int16_cmeta_data},
        {"int16_t", &turbo_int16_cmeta_data},
        {"i16", &turbo_int16_cmeta_data},
        {"uint16", &turbo_uint16_cmeta_data},
        {"uint16_t", &turbo_uint16_cmeta_data},
        {"u16", &turbo_uint16_cmeta_data},
        {"int32", &turbo_int32_cmeta_data},
        {"int32_t", &turbo_int32_cmeta_data},
        {"i32", &turbo_int32_cmeta_data},
        {"uint32", &turbo_uint32_cmeta_data},
        {"uint32_t", &turbo_uint32_cmeta_data},
        {"u32", &turbo_uint32_cmeta_data},
        {"int64", &turbo_int64_cmeta_data},
        {"int64_t", &turbo_int64_cmeta_data},
        {"i64", &turbo_int64_cmeta_data},
        {"uint64", &turbo_uint64_cmeta_data},
        {"uint64_t", &turbo_uint64_cmeta_data},
        {"u64", &turbo_uint64_cmeta_data},
        {"float", &cmeta_data_float},
        {"double", &cmeta_data_double},
        {"string", &tbe_cbind_test_owned_string_data},
        {"uuid", &turbo_uuid_cmeta_data}};
    size_t index;

    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
      tbe_cbind_plan_error error;
      tbe_cbind_plan *plan = NULL;
      info("spelling: %s", cases[index].spelling);
      tbe_cbind_plan_error_init(&error);
      check_equal(create_scalar_slot(cases[index].spelling,
                                     cases[index].value, &plan, &error),
                  TBE_CBIND_OK);
      check_not_null(plan);
      tbe_cbind_plan_destroy(plan);
    }
  }

  it("rejects an unresolved field type before inspecting native storage") {
    tbe_cbind_plan_error error;
    tbe_cbind_plan_error_init(&error);

    check_equal(create_one("message One { Missing value; }", "One", &error),
                TBE_CBIND_UNSUPPORTED);
    check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
    check_not_null(strstr(error.path, "One.value"));
  }

  it("enforces schema type field depth and name limits during extraction") {
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan_options_init(&options);

    options.max_schema_bytes = 8u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "message One { int32 value; }", 28u, "One", 3u,
                    &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);

    tbe_cbind_plan_options_init(&options);
    options.max_types = 1u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "composite Child { int32 value; } "
                    "message One { Child child; }",
                    sizeof("composite Child { int32 value; } "
                           "message One { Child child; }") - 1u,
                    "One", 3u, &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);

    tbe_cbind_plan_options_init(&options);
    options.max_fields = 1u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "message One { int32 first; int32 second; }",
                    sizeof("message One { int32 first; int32 second; }") - 1u,
                    "One", 3u, &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);

    tbe_cbind_plan_options_init(&options);
    options.max_depth = 1u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "composite Child { int32 value; } "
                    "message One { Child child; }",
                    sizeof("composite Child { int32 value; } "
                           "message One { Child child; }") - 1u,
                    "One", 3u, &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);

    tbe_cbind_plan_options_init(&options);
    options.max_name_bytes = 4u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "message One { int32 value; }",
                    sizeof("message One { int32 value; }") - 1u,
                    "One", 3u, &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
  }

  it("rejects an inverse-declaration chain one level beyond max_depth") {
    static const char schema[] =
        "composite Leaf { int32 value; } "
        "composite Mid { Leaf leaf; } "
        "message Root { Mid mid; }";
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;

    tbe_cbind_plan_options_init(&options);
    options.max_depth = 2u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(schema, sizeof(schema) - 1u,
                                        "Root", 4u, &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
  }

  it("stops a root-first chain at max_depth before inspecting its distant tail") {
    enum { CHAIN_LENGTH = 64, SCHEMA_CAPACITY = 8192 };
    char schema[SCHEMA_CAPACITY];
    size_t used = 0u;
    size_t index;
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;

    used += (size_t)snprintf(schema + used, sizeof(schema) - used,
                             "message Root { Node0 child; } ");
    for (index = 0u; index + 1u < CHAIN_LENGTH; ++index)
      used += (size_t)snprintf(schema + used, sizeof(schema) - used,
                               "message Node%zu { Node%zu child; } ",
                               index, index + 1u);
    used += (size_t)snprintf(schema + used, sizeof(schema) - used,
                             "message Node%zu { Missing child; }",
                             (size_t)CHAIN_LENGTH - 1u);
    check_less(used, sizeof(schema));

    tbe_cbind_plan_options_init(&options);
    options.max_depth = 2u;
    options.max_types = CHAIN_LENGTH + 1u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(schema, used, "Root", 4u,
                                        &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
  }

  it("accepts a nested record whose height equals max_depth") {
    static const char schema[] =
        "composite Detail { int32 quantity; } "
        "message Root { Detail detail; double score; }";
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    tbe_cbind_plan_options_init(&options);
    options.max_depth = 2u;
    tbe_cbind_plan_error_init(&error);
    check_equal(tbe_cbind_plan_create_from_text(
                    schema, sizeof(schema) - 1u, "Root", 4u,
                    &tbe_cbind_test_nested_data, &options, &plan, &error),
                TBE_CBIND_OK);
    check_not_null(plan);
    tbe_cbind_plan_destroy(plan);
  }

  it("computes shared-DAG height once without shortening either parent path") {
    static const char schema[] =
        "composite Text { int32 value; } "
        "message Pair { Text left; Text right; }";
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    tbe_cbind_plan_options_init(&options);
    options.max_depth = 2u;
    tbe_cbind_plan_error_init(&error);
    check_equal(tbe_cbind_plan_create_from_text(
                    schema, sizeof(schema) - 1u, "Pair", 4u,
                    &tbe_cbind_multitu_pair_data, &options, &plan, &error),
                TBE_CBIND_OK);
    check_not_null(plan);
    tbe_cbind_plan_destroy(plan);
  }

  it("rejects a recursive record graph while computing subtree height") {
    static const char schema[] =
        "composite First { Second second; } "
        "message Second { First first; }";
    tbe_cbind_plan_error error;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_one(schema, "Second", &error), TBE_CBIND_UNSUPPORTED);
    check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
  }

  it("rejects embedded NULs and checked slice size overflow") {
    const char schema_with_nul[] =
        {'m','e','s','s','a','g','e',' ','O','n','e',' ','{','\0','}'};
    const char type_name_with_nul[] = {'O', 'n', '\0', 'e'};
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);

    check_equal(create_one_with_options(
                    schema_with_nul, sizeof(schema_with_nul), "One", 3u,
                    &options, &error),
                TBE_CBIND_INVALID_ARGUMENT);

    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "message One { int32 value; }",
                    sizeof("message One { int32 value; }") - 1u, "", 0u,
                    &options, &error),
                TBE_CBIND_INVALID_ARGUMENT);

    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options(
                    "message One { int32 value; }",
                    sizeof("message One { int32 value; }") - 1u,
                    type_name_with_nul, sizeof(type_name_with_nul), &options,
                    &error),
                TBE_CBIND_INVALID_ARGUMENT);

    options.max_schema_bytes = SIZE_MAX;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options("x", SIZE_MAX, "One", 3u,
                                        &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
  }
}
