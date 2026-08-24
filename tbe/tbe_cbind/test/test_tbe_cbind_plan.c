#include <tbe_cbind/tbe_cbind.h>

#include "tbe_cbind_internal.h"
#include "tbe_cbind_multitu_fixture.h"
#include "tbe_cbind_test_fixtures.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

typedef struct tbe_cbind_test_data_prefix {
  size_t struct_size;
  uint32_t abi_version;
  const char *stable_id;
  const char *display_name;
  cmeta_data_kind kind;
  const cmeta_type_desc *storage_type;
  const void *shape;
} tbe_cbind_test_data_prefix;

_Static_assert(sizeof(tbe_cbind_test_data_prefix) ==
                   offsetof(cmeta_data_desc, buffer_ops),
               "CMeta data prefix fixture must end before buffer_ops");

static tbe_cbind_test_data_prefix *tbe_cbind_test_guarded_prefix_create(
    const tbe_cbind_test_data_prefix *value, void **allocation) {
#if defined(_WIN32)
  SYSTEM_INFO system_info;
  unsigned char *region;
  unsigned char *page;
  GetSystemInfo(&system_info);
  region = (unsigned char *)VirtualAlloc(
      NULL, (size_t)system_info.dwPageSize * 2u, MEM_RESERVE, PAGE_NOACCESS);
  if (region == NULL) return NULL;
  page = (unsigned char *)VirtualAlloc(region, system_info.dwPageSize,
                                       MEM_COMMIT, PAGE_READWRITE);
  if (page == NULL) {
    (void)VirtualFree(region, 0u, MEM_RELEASE);
    return NULL;
  }
  *allocation = region;
  memcpy(page + system_info.dwPageSize - sizeof(*value), value,
         sizeof(*value));
  return (tbe_cbind_test_data_prefix *)(
      page + system_info.dwPageSize - sizeof(*value));
#else
  tbe_cbind_test_data_prefix *copy =
      (tbe_cbind_test_data_prefix *)malloc(sizeof(*copy));
  if (copy != NULL) *copy = *value;
  *allocation = copy;
  return copy;
#endif
}

static void tbe_cbind_test_guarded_prefix_destroy(void *allocation) {
#if defined(_WIN32)
  if (allocation != NULL) (void)VirtualFree(allocation, 0u, MEM_RELEASE);
#else
  free(allocation);
#endif
}

static tbe_cbind_status create_plan_with_options(
    const char *schema, size_t schema_size, const char *type_name,
    size_t type_name_size, const cmeta_data_desc *native_shape,
    const tbe_cbind_plan_options *options, tbe_cbind_plan **out,
    tbe_cbind_plan_error *error) {
  *out = (tbe_cbind_plan *)(uintptr_t)1u;
  return tbe_cbind_plan_create_from_text(
      schema, schema_size, type_name, type_name_size, native_shape, options,
      out, error);
}

static tbe_cbind_status create_plan(const char *schema, const char *type_name,
                                    const cmeta_data_desc *native_shape,
                                    tbe_cbind_plan **out,
                                    tbe_cbind_plan_error *error) {
  tbe_cbind_plan_options options;
  tbe_cbind_plan_options_init(&options);
  return create_plan_with_options(schema, strlen(schema), type_name,
                                  strlen(type_name), native_shape, &options,
                                  out, error);
}

typedef struct fail_allocator_state {
  size_t call_count;
  size_t fail_at;
  size_t live_count;
} fail_allocator_state;

static void *fail_allocator_calloc(void *opaque, size_t count, size_t size) {
  fail_allocator_state *state = (fail_allocator_state *)opaque;
  void *pointer;
  ++state->call_count;
  if (state->call_count == state->fail_at) return NULL;
  pointer = calloc(count, size);
  if (pointer != NULL) ++state->live_count;
  return pointer;
}

static void fail_allocator_free(void *opaque, void *pointer) {
  fail_allocator_state *state = (fail_allocator_state *)opaque;
  if (pointer != NULL) {
    check_greater(state->live_count, (size_t)0);
    --state->live_count;
    free(pointer);
  }
}

static bool tbe_cbind_test_alternate_string_is_zero(const void *object) {
  (void)object;
  return false;
}

spec("TbeCBind native plan overlay") {
  it("builds both overlays with semantic names and native storage metadata") {
    char schema[] =
        "message One { [name(external), c(value)] int32 internal; }TAIL";
    const size_t schema_size = sizeof(schema) - sizeof("TAIL");
    const char type_slice[] = {'O', 'n', 'e', 'X'};
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    const cmeta_data_desc *shape;
    const cmeta_data_struct_shape *record;

    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan_with_options(
                    schema, schema_size, type_slice, 3u,
                    &tbe_cbind_test_one_data, &options, &plan, &error),
                TBE_CBIND_OK);
    check_not_null(plan);
    memset(schema, 'x', schema_size);

    shape = tbe_cbind_plan_shape(plan);
    check_not_null(shape);
    check_equal(shape->kind, CMETA_DATA_STRUCT);
    record = (const cmeta_data_struct_shape *)shape->shape;
    check_not_null(record);
    check_equal(record->field_count, (size_t)1);
    check_equal(record->layout->fields[0].name, "external");
    check_equal(record->fields[0].name, "external");
    check_equal(record->fields[0].offset,
                offsetof(tbe_cbind_test_one, value));
    check_true(record->fields[0].value == &cmeta_data_int);
    check_true(record->layout->fields[0].type == &cmeta_type_int);
    check_true(cmeta_data_desc_valid(shape));
    tbe_cbind_plan_destroy(plan);
  }

  it("accepts a native struct descriptor backed by only the public prefix") {
    static const char schema[] = "message One { int32 value; }";
    const tbe_cbind_test_data_prefix native_value = {
        sizeof(tbe_cbind_test_data_prefix),
        CMETA_DATA_DESC_ABI_VERSION,
        "test.tbe-cbind.one.prefix",
        "tbe_cbind_test_one",
        CMETA_DATA_STRUCT,
        &tbe_cbind_test_one_type,
        &tbe_cbind_test_one_shape};
    void *allocation = NULL;
    const tbe_cbind_test_data_prefix *native =
        tbe_cbind_test_guarded_prefix_create(&native_value, &allocation);
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    check_not_null(native);
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One",
                            (const cmeta_data_desc *)native, &plan, &error),
                TBE_CBIND_OK);
    check_not_null(tbe_cbind_plan_shape(plan));
    tbe_cbind_plan_destroy(plan);
    tbe_cbind_test_guarded_prefix_destroy(allocation);
  }

  it("recursively builds nested overlays") {
    static const char schema[] =
        "composite Detail { [name(amount), c(quantity)] int32 quantity; } "
        "message Root { Detail detail; double score; }";
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    const cmeta_data_struct_shape *root_shape;
    const cmeta_data_struct_shape *inner_shape;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Root", &tbe_cbind_test_nested_data,
                            &plan, &error),
                TBE_CBIND_OK);
    check_not_null(plan);
    root_shape = (const cmeta_data_struct_shape *)
        tbe_cbind_plan_shape(plan)->shape;
    inner_shape = (const cmeta_data_struct_shape *)root_shape->fields[0].value->shape;
    check_equal(root_shape->layout->fields[0].name, "detail");
    check_equal(inner_shape->layout->fields[0].name, "amount");
    check_equal(inner_shape->fields[0].name, "amount");
    tbe_cbind_plan_destroy(plan);
  }

  it("reuses a repeated nested type described equivalently in another TU") {
    static const char schema[] =
        "composite Text { int32 value; } "
        "message Pair { Text left; Text right; }";
    cmeta_data_field_desc fields[2];
    cmeta_data_struct_shape shape = tbe_cbind_multitu_pair_shape;
    cmeta_data_desc data = tbe_cbind_multitu_pair_data;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    tbe_cbind_status status;

    fields[0] = tbe_cbind_multitu_pair_data_fields[0];
    fields[1] = tbe_cbind_multitu_pair_data_fields[1];
    fields[1].value = tbe_cbind_multitu_external_text_data();
    check_true(fields[0].value != fields[1].value);
    check_true(cmeta_type_equal(fields[0].value->storage_type,
                                fields[1].value->storage_type));
    shape.fields = fields;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    status = create_plan(schema, "Pair", &data, &plan, &error);
    check_equal(status, TBE_CBIND_OK);
    check_not_null(plan);
    tbe_cbind_plan_destroy(plan);
  }

  it("rejects equivalent records whose string adapter callbacks differ") {
    tbe_cbind_semantic_field fields[] = {
        {"owned", "owned", "owned", "string",
         TBE_CBIND_SEMANTIC_STRING, NULL},
        {"borrowed", "borrowed", "borrowed", "string",
         TBE_CBIND_SEMANTIC_STRING, NULL}};
    tbe_cbind_semantic_type semantic = {
        "Text", fields, 2u, 1u, 2u};
    cmeta_data_buffer_ops alternate_ops = turbo_tstr_cmeta_buffer_ops;
    cmeta_data_desc alternate_string = tbe_cbind_test_owned_string_data;
    cmeta_data_field_desc alternate_fields[2];
    cmeta_data_struct_shape alternate_shape = tbe_cbind_test_strings_shape;
    cmeta_data_desc alternate_data = tbe_cbind_test_strings_data;
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    fail_allocator_state allocator_state = {0u, SIZE_MAX, 0u};
    tbe_cbind_allocator allocator = {
        &allocator_state, fail_allocator_calloc, fail_allocator_free};
    tbe_cbind_build_context context = {0};

    alternate_ops.is_zero = tbe_cbind_test_alternate_string_is_zero;
    alternate_string.buffer_ops = &alternate_ops;
    alternate_fields[0] = tbe_cbind_test_strings_data_fields[0];
    alternate_fields[0].value = &alternate_string;
    alternate_fields[1] = tbe_cbind_test_strings_data_fields[1];
    alternate_shape.fields = alternate_fields;
    alternate_data.shape = &alternate_shape;
    tbe_cbind_plan_options_init(&options);
    tbe_cbind_plan_error_init(&error);
    context.options = &options;
    context.error = &error;
    context.allocator = allocator;

    check_equal(tbe_cbind_native_record_equivalent(
                    &context, &semantic, &tbe_cbind_test_strings_data,
                    &alternate_data, 1u),
                TBE_CBIND_TYPE_MISMATCH);
    check_equal(error.phase, TBE_CBIND_PHASE_PLAN);
    check_equal(allocator_state.live_count, (size_t)0u);
  }

  it("defensively rejects a semantic model deeper than max_depth") {
    tbe_cbind_semantic_field child_fields[] = {
        {"quantity", "quantity", "quantity", "int32",
         TBE_CBIND_SEMANTIC_INT32, NULL}};
    tbe_cbind_semantic_field root_fields[] = {
        {"detail", "detail", "detail", "Detail",
         TBE_CBIND_SEMANTIC_RECORD, NULL},
        {"score", "score", "score", "double",
         TBE_CBIND_SEMANTIC_DOUBLE, NULL}};
    tbe_cbind_semantic_type types[] = {
        {"Detail", child_fields, 1u, 1u, 2u},
        {"Root", root_fields, 2u, 2u, 2u}};
    tbe_cbind_schema_model model = {0};
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    fail_allocator_state allocator_state = {0u, SIZE_MAX, 0u};
    tbe_cbind_allocator allocator = {
        &allocator_state, fail_allocator_calloc, fail_allocator_free};
    tbe_cbind_build_context context = {0};
    tbe_cbind_plan *plan = NULL;

    root_fields[0].record_type = &types[0];
    model.types = types;
    model.type_count = 2u;
    model.root = &types[1];
    tbe_cbind_plan_options_init(&options);
    options.max_depth = 1u;
    tbe_cbind_plan_error_init(&error);
    context.options = &options;
    context.error = &error;
    context.allocator = allocator;

    check_equal(tbe_cbind_plan_build(&context, &model,
                                     &tbe_cbind_test_nested_data, &plan),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_null(plan);
    check_equal(allocator_state.live_count, (size_t)0u);
    tbe_cbind_plan_destroy(plan);
  }

  it("rejects a non-struct root and malformed native layout") {
    static const char schema[] = "message One { int32 value; }";
    cmeta_data_desc data = tbe_cbind_test_one_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_one_shape;
    cmeta_struct_desc layout = tbe_cbind_test_one_layout;
    cmeta_field_desc field = tbe_cbind_test_one_layout_fields[0];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &cmeta_data_int, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);

    field.offset = sizeof(tbe_cbind_test_one);
    layout.fields = &field;
    shape.layout = &layout;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
  }

  it("returns a native-shape error for null reflected names and arrays") {
    static const char schema[] = "message One { int32 value; }";
    cmeta_data_desc data = tbe_cbind_test_one_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_one_shape;
    cmeta_struct_desc layout = tbe_cbind_test_one_layout;
    cmeta_field_desc layout_field = tbe_cbind_test_one_layout_fields[0];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    layout_field.name = NULL;
    layout.fields = &layout_field;
    shape.layout = &layout;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);

    layout = tbe_cbind_test_one_layout;
    layout.fields = NULL;
    shape = tbe_cbind_test_one_shape;
    shape.layout = &layout;
    data = tbe_cbind_test_one_data;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);

    layout = tbe_cbind_test_one_layout;
    shape = tbe_cbind_test_one_shape;
    shape.layout = &layout;
    shape.fields = NULL;
    data = tbe_cbind_test_one_data;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);
  }

  it("rejects malformed field size alignment and reflected type") {
    static const char schema[] = "message One { int32 value; }";
    size_t variant;
    for (variant = 0u; variant < 3u; ++variant) {
      cmeta_data_desc data = tbe_cbind_test_one_data;
      cmeta_data_struct_shape shape = tbe_cbind_test_one_shape;
      cmeta_struct_desc layout = tbe_cbind_test_one_layout;
      cmeta_field_desc field = tbe_cbind_test_one_layout_fields[0];
      tbe_cbind_plan_error error;
      tbe_cbind_plan *plan = NULL;
      if (variant == 0u) field.size += 1u;
      if (variant == 1u) field.align += 1u;
      if (variant == 2u) field.type = &cmeta_type_double;
      layout.fields = &field;
      shape.layout = &layout;
      data.shape = &shape;
      tbe_cbind_plan_error_init(&error);
      check_equal(create_plan(schema, "One", &data, &plan, &error),
                  TBE_CBIND_NATIVE_SHAPE_ERROR);
      check_null(plan);
    }
  }

  it("rejects invalid c mappings collisions and missing native members") {
    static const char *const schemas[] = {
        "message One { [c(primary), c(other)] int32 value; }",
        "message One { [c(\"\")] int32 value; }",
        "message One { [c(\"not-valid\")] int32 value; }",
        "message Pair { [c(second)] int32 first; int32 second; }",
        "message Pair { [c(shared)] int32 first; [c(shared)] int32 second; }",
        "message One { [c(missing)] int32 value; }"};
    size_t index;
    for (index = 0u; index < sizeof(schemas) / sizeof(schemas[0]); ++index) {
      const cmeta_data_desc *native_shape =
          strstr(schemas[index], "message Pair") != NULL
              ? &tbe_cbind_test_pair_data
              : &tbe_cbind_test_one_data;
      const char *type_name = native_shape == &tbe_cbind_test_pair_data
                                  ? "Pair"
                                  : "One";
      tbe_cbind_plan_error error;
      tbe_cbind_plan *plan = NULL;
      tbe_cbind_plan_error_init(&error);
      check_not_equal(create_plan(schemas[index], type_name, native_shape,
                                  &plan, &error),
                      TBE_CBIND_OK);
      check_null(plan);
    }
  }

  it("does not guess a semantic-only layout by declaration order") {
    static const char schema[] = "message One { int32 value; }";
    cmeta_data_desc data = tbe_cbind_test_one_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_one_shape;
    cmeta_struct_desc layout = tbe_cbind_test_one_layout;
    cmeta_field_desc field = tbe_cbind_test_one_layout_fields[0];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    field.name = "semanticValue";
    layout.fields = &field;
    shape.layout = &layout;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
  }

  it("rejects extra native fields and nested storage mismatch") {
    static const char one_schema[] = "message One { int32 first; }";
    static const char nested_schema[] =
        "composite Detail { int32 quantity; } "
        "message Root { Detail detail; double score; }";
    cmeta_data_desc nested_data = tbe_cbind_test_nested_data;
    cmeta_data_struct_shape nested_shape = tbe_cbind_test_nested_shape;
    cmeta_data_field_desc nested_fields[2];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(one_schema, "One", &tbe_cbind_test_pair_data,
                            &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);

    memcpy(nested_fields, tbe_cbind_test_nested_data_fields,
           sizeof(nested_fields));
    nested_fields[0].value = &cmeta_data_int;
    nested_shape.fields = nested_fields;
    nested_data.shape = &nested_shape;
    tbe_cbind_plan_error_init(&error);
    check_not_equal(create_plan(nested_schema, "Root", &nested_data,
                                &plan, &error),
                    TBE_CBIND_OK);
    check_null(plan);
  }

  it("rejects a string descriptor without a legal public buffer adapter") {
    static const char schema[] = "message Text { string owned; }";
    cmeta_data_desc bad_string = tbe_cbind_test_owned_string_data;
    cmeta_data_desc data = tbe_cbind_test_strings_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_strings_shape;
    cmeta_struct_desc layout = tbe_cbind_test_strings_layout;
    cmeta_data_field_desc data_field = tbe_cbind_test_strings_data_fields[0];
    cmeta_field_desc layout_field = tbe_cbind_test_strings_layout_fields[0];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    bad_string.buffer_ops = NULL;
    data_field.value = &bad_string;
    shape.fields = &data_field;
    shape.field_count = 1u;
    layout.fields = &layout_field;
    layout.field_count = 1u;
    shape.layout = &layout;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Text", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
  }

  it("rejects a CUSTOM string adapter while creating the plan") {
    static const char schema[] = "message Text { string owned; }";
    cmeta_data_buffer_shape custom_shape = {CMETA_DATA_BUFFER_CUSTOM};
    cmeta_data_buffer_ops custom_ops = turbo_tstr_cmeta_buffer_ops;
    cmeta_data_desc custom_string = tbe_cbind_test_owned_string_data;
    cmeta_data_desc data = tbe_cbind_test_strings_data;
    cmeta_data_struct_shape shape = tbe_cbind_test_strings_shape;
    cmeta_struct_desc layout = tbe_cbind_test_strings_layout;
    cmeta_data_field_desc data_field = tbe_cbind_test_strings_data_fields[0];
    cmeta_field_desc layout_field = tbe_cbind_test_strings_layout_fields[0];
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    custom_ops.ownership = CMETA_DATA_BUFFER_CUSTOM;
    custom_string.shape = &custom_shape;
    custom_string.buffer_ops = &custom_ops;
    data_field.value = &custom_string;
    shape.fields = &data_field;
    shape.field_count = 1u;
    layout.fields = &layout_field;
    layout.field_count = 1u;
    shape.layout = &layout;
    data.shape = &shape;
    check_true(cmeta_data_desc_valid(&custom_string));
    check_not_null(cmeta_data_buffer_ops_of(&custom_string));
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Text", &data, &plan, &error),
                TBE_CBIND_UNSUPPORTED);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);
  }

  it("enforces max_plan_bytes only while materializing the ready overlay") {
    static const char schema[] = "message One { int32 value; }";
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    tbe_cbind_plan_options_init(&options);
    options.max_plan_bytes = 1u;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan_with_options(
                    schema, sizeof(schema) - 1u, "One", 3u,
                    &tbe_cbind_test_one_data, &options, &plan, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_PLAN);
  }

  it("cleans every partial allocation under deterministic builder OOM") {
    static const char schema[] = "message One { int32 value; }";
    tbe_cbind_plan_options options;
    size_t fail_at;
    int reached_success = 0;
    tbe_cbind_plan_options_init(&options);
    for (fail_at = 1u; fail_at < 128u; ++fail_at) {
      fail_allocator_state state = {0u, fail_at, 0u};
      tbe_cbind_allocator allocator = {
          &state, fail_allocator_calloc, fail_allocator_free};
      tbe_cbind_plan_error error;
      tbe_cbind_plan *plan = (tbe_cbind_plan *)(uintptr_t)1u;
      tbe_cbind_status status;
      tbe_cbind_plan_error_init(&error);
      status = tbe_cbind_plan_create_from_text_with_allocator(
          schema, sizeof(schema) - 1u, "One", 3u,
          &tbe_cbind_test_one_data, &options, &plan, &error, &allocator);
      if (status == TBE_CBIND_OK) {
        check_not_null(plan);
        tbe_cbind_plan_destroy(plan);
        check_equal(state.live_count, (size_t)0);
        reached_success = 1;
        break;
      }
      check_equal(status, TBE_CBIND_OUT_OF_MEMORY);
      check_null(plan);
      check_equal(state.live_count, (size_t)0);
    }
    check_true(reached_success);
  }

  it("cleans deep multi-TU comparison allocations under deterministic OOM") {
    static const char schema[] =
        "composite Text { int32 value; } "
        "message Pair { Text left; Text right; }";
    cmeta_data_field_desc fields[2];
    cmeta_data_struct_shape shape = tbe_cbind_multitu_pair_shape;
    cmeta_data_desc data = tbe_cbind_multitu_pair_data;
    tbe_cbind_plan_options options;
    size_t fail_at;
    int reached_success = 0;

    fields[0] = tbe_cbind_multitu_pair_data_fields[0];
    fields[1] = tbe_cbind_multitu_pair_data_fields[1];
    fields[1].value = tbe_cbind_multitu_external_text_data();
    shape.fields = fields;
    data.shape = &shape;
    tbe_cbind_plan_options_init(&options);

    for (fail_at = 1u; fail_at < 256u; ++fail_at) {
      fail_allocator_state state = {0u, fail_at, 0u};
      tbe_cbind_allocator allocator = {
          &state, fail_allocator_calloc, fail_allocator_free};
      tbe_cbind_plan_error error;
      tbe_cbind_plan *plan = (tbe_cbind_plan *)(uintptr_t)1u;
      tbe_cbind_status status;

      tbe_cbind_plan_error_init(&error);
      status = tbe_cbind_plan_create_from_text_with_allocator(
          schema, sizeof(schema) - 1u, "Pair", 4u, &data, &options, &plan,
          &error, &allocator);
      if (status == TBE_CBIND_OK) {
        check_not_null(plan);
        tbe_cbind_plan_destroy(plan);
        check_equal(state.live_count, (size_t)0);
        reached_success = 1;
        break;
      }
      check_equal(status, TBE_CBIND_OUT_OF_MEMORY);
      check_null(plan);
      check_equal(state.live_count, (size_t)0);
    }
    check_true(reached_success);
  }
}
