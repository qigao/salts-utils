#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_test_fixtures.h"
#include "tinytest.h"

#include <stdint.h>
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
        "union Choice { int32 value; } message One { int32 value; }",
        "message One { [alias(old)] int32 value; }",
        "message One { [id(1)] int32 value; }",
        "message One { optional int32 value; }",
        "message One { int32 value default 7; }",
        "message One { bool value; }",
        "message One { uint32 value; }",
        "message One { list<int32> value; }"};
    size_t index;

    for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
      tbe_cbind_plan_error error;
      tbe_cbind_plan_error_init(&error);
      check_equal(create_one(schemas[index], "One", &error),
                  TBE_CBIND_UNSUPPORTED);
      check_equal(error.phase, TBE_CBIND_PHASE_SCHEMA);
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

  it("rejects embedded NULs and checked slice size overflow") {
    const char schema_with_nul[] =
        {'m','e','s','s','a','g','e',' ','O','n','e',' ','{','\0','}'};
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);

    check_equal(create_one_with_options(
                    schema_with_nul, sizeof(schema_with_nul), "One", 3u,
                    &options, &error),
                TBE_CBIND_INVALID_ARGUMENT);

    options.max_schema_bytes = SIZE_MAX;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_one_with_options("x", SIZE_MAX, "One", 3u,
                                        &options, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
  }
}
