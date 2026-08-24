#include <tbe_cbind/tbe_cbind.h>

#include "tinytest.h"

#include <stddef.h>

spec("TbeCBind public C API") {
  it("initializes the versioned options and error contracts") {
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;

    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);

    check_equal(options.struct_size, sizeof(options));
    check_equal(options.abi_version, TBE_CBIND_OPTIONS_ABI_VERSION);
    check_greater(options.max_schema_bytes, (size_t)0);
    check_greater(options.max_types, (size_t)0);
    check_greater(options.max_fields, (size_t)0);
    check_greater(options.max_depth, (size_t)0);
    check_greater(options.max_name_bytes, (size_t)0);
    check_greater(options.max_plan_bytes, (size_t)0);
    check_equal(error.struct_size, sizeof(error));
    check_equal(error.abi_version, TBE_CBIND_ERROR_ABI_VERSION);
    check_equal(error.status, TBE_CBIND_OK);
    check_equal(error.phase, TBE_CBIND_PHASE_NONE);
  }

  it("keeps the output null when factory arguments are invalid") {
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = (tbe_cbind_plan *)(size_t)1u;

    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);

    check_equal(tbe_cbind_plan_create_from_text(
                    NULL, 0u, "Type", 4u, NULL, &options, &plan, &error),
                TBE_CBIND_INVALID_ARGUMENT);
    check_null(plan);
    check_equal(error.status, TBE_CBIND_INVALID_ARGUMENT);
    check_null(tbe_cbind_plan_shape(NULL));
    tbe_cbind_plan_destroy(NULL);
  }

  it("rejects uninitialized and zero-limit options") {
    static const char schema[] = "message Type { int32 value; }";
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    cmeta_data_desc native_shape = {0};

    tbe_cbind_plan_error_init(&error);
    options = (tbe_cbind_plan_options){0};
    check_equal(tbe_cbind_plan_create_from_text(
                    schema, sizeof(schema) - 1u, "Type", 4u, &native_shape,
                    &options, &plan, &error),
                TBE_CBIND_INVALID_OPTIONS);
    check_null(plan);

    tbe_cbind_plan_options_init(&options);
    options.max_fields = 0u;
    tbe_cbind_plan_error_init(&error);
    check_equal(tbe_cbind_plan_create_from_text(
                    schema, sizeof(schema) - 1u, "Type", 4u, &native_shape,
                    &options, &plan, &error),
                TBE_CBIND_INVALID_OPTIONS);
    check_null(plan);
  }

  it("rejects decode without a ready plan before touching CBind inputs") {
    check_equal(tbe_cbind_plan_decode(NULL, NULL, NULL, NULL, NULL),
                CBIND_INVALID_ARGUMENT);
  }
}
