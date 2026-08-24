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

static void *tbe_cbind_test_guarded_copy_create(
    const void *value, size_t value_size, void **allocation) {
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
  if (value_size > system_info.dwPageSize) {
    (void)VirtualFree(region, 0u, MEM_RELEASE);
    *allocation = NULL;
    return NULL;
  }
  memcpy(page + system_info.dwPageSize - value_size, value, value_size);
  return page + system_info.dwPageSize - value_size;
#else
  void *copy = malloc(value_size);
  if (copy != NULL) memcpy(copy, value, value_size);
  *allocation = copy;
  return copy;
#endif
}

static tbe_cbind_test_data_prefix *tbe_cbind_test_guarded_prefix_create(
    const tbe_cbind_test_data_prefix *value, void **allocation) {
  return (tbe_cbind_test_data_prefix *)tbe_cbind_test_guarded_copy_create(
      value, sizeof(*value), allocation);
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

typedef struct tbe_cbind_depth_shared {
  tbe_cbind_test_one leaf;
} tbe_cbind_depth_shared;

typedef struct tbe_cbind_depth_wrapper {
  tbe_cbind_depth_shared shared;
} tbe_cbind_depth_wrapper;

typedef struct tbe_cbind_depth_root {
  tbe_cbind_depth_shared shallow;
  tbe_cbind_depth_wrapper deep;
} tbe_cbind_depth_root;

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

  it("reuses the same semantic and native descriptor pointer pair") {
    static const char schema[] =
        "composite Text { int32 value; } "
        "message Pair { Text left; Text right; }";
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;
    const cmeta_data_struct_shape *root_shape;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Pair", &tbe_cbind_multitu_pair_data,
                            &plan, &error),
                TBE_CBIND_OK);
    root_shape =
        (const cmeta_data_struct_shape *)tbe_cbind_plan_shape(plan)->shape;
    check_true(root_shape->fields[0].value == root_shape->fields[1].value);
    check_equal(plan->node_count, (size_t)2u);
    check_null(plan->node_slots);
    tbe_cbind_plan_destroy(plan);
  }

  it("keeps equivalent descriptors from another TU as separate occurrences") {
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
    {
      const cmeta_data_struct_shape *root_shape =
          (const cmeta_data_struct_shape *)tbe_cbind_plan_shape(plan)->shape;
      check_true(root_shape->fields[0].value != root_shape->fields[1].value);
      check_equal(plan->node_count, (size_t)3u);
      check_null(plan->node_slots);
    }
    tbe_cbind_plan_destroy(plan);
  }

  it("charges distinct native occurrences to max_plan_bytes") {
    static const char schema[] =
        "composite Text { int32 value; } "
        "message Pair { Text left; Text right; }";
    const size_t shared_pair_plan_bytes =
        sizeof(tbe_cbind_plan) + 2u * sizeof(tbe_cbind_plan_node) +
        3u * sizeof(cmeta_field_desc) +
        3u * sizeof(cmeta_data_field_desc) +
        sizeof("Pair") + sizeof("left") + sizeof("right") +
        sizeof("Text") + sizeof("value");
    cmeta_data_field_desc fields[2];
    cmeta_data_struct_shape shape = tbe_cbind_multitu_pair_shape;
    cmeta_data_desc data = tbe_cbind_multitu_pair_data;
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    tbe_cbind_plan_options_init(&options);
    options.max_plan_bytes = shared_pair_plan_bytes;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan_with_options(
                    schema, sizeof(schema) - 1u, "Pair", 4u,
                    &tbe_cbind_multitu_pair_data, &options, &plan, &error),
                TBE_CBIND_OK);
    tbe_cbind_plan_destroy(plan);

    fields[0] = tbe_cbind_multitu_pair_data_fields[0];
    fields[1] = tbe_cbind_multitu_pair_data_fields[1];
    fields[1].value = tbe_cbind_multitu_external_text_data();
    shape.fields = fields;
    data.shape = &shape;
    plan = NULL;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan_with_options(
                    schema, sizeof(schema) - 1u, "Pair", 4u,
                    &data, &options, &plan, &error),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_PLAN);
  }

  it("accepts repeated standard string adapters emitted in another TU") {
    static const char schema[] =
        "message Text { string value; } "
        "message Pair { Text left; Text right; }";
    cmeta_data_field_desc fields[2];
    cmeta_data_struct_shape shape = tbe_cbind_multitu_string_pair_shape;
    cmeta_data_desc data = tbe_cbind_multitu_string_pair_data;
    const cmeta_data_desc *external =
        tbe_cbind_multitu_external_string_text_data();
    const cmeta_data_buffer_ops *local_ops = cmeta_data_buffer_ops_of(
        tbe_cbind_multitu_string_text_data_fields[0].value);
    const cmeta_data_buffer_ops *external_ops = cmeta_data_buffer_ops_of(
        ((const cmeta_data_struct_shape *)external->shape)->fields[0].value);
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    check_not_null(local_ops);
    check_not_null(external_ops);
    check_true(local_ops != external_ops);
    check_true(local_ops->assign != external_ops->assign);
    fields[0] = tbe_cbind_multitu_string_pair_data_fields[0];
    fields[1] = tbe_cbind_multitu_string_pair_data_fields[1];
    fields[1].value = external;
    shape.fields = fields;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Pair", &data, &plan, &error),
                TBE_CBIND_OK);
    check_not_null(plan);
    tbe_cbind_plan_destroy(plan);
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

  it("checks remaining height before reusing a shallow-path ready node") {
    cmeta_type_identity shared_identity =
        CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.depth-shared");
    cmeta_type_identity wrapper_identity =
        CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.depth-wrapper");
    cmeta_type_identity root_identity =
        CMETA_TYPE_ID_ATOM_INIT("test.tbe-cbind.depth-root");
    cmeta_type_desc shared_type = {
        "tbe_cbind_depth_shared", sizeof(tbe_cbind_depth_shared),
        _Alignof(tbe_cbind_depth_shared), CMETA_T_OBJECT, NULL, NULL,
        &shared_identity};
    cmeta_type_desc wrapper_type = {
        "tbe_cbind_depth_wrapper", sizeof(tbe_cbind_depth_wrapper),
        _Alignof(tbe_cbind_depth_wrapper), CMETA_T_OBJECT, NULL, NULL,
        &wrapper_identity};
    cmeta_type_desc root_type = {
        "tbe_cbind_depth_root", sizeof(tbe_cbind_depth_root),
        _Alignof(tbe_cbind_depth_root), CMETA_T_OBJECT, NULL, NULL,
        &root_identity};
    cmeta_field_desc shared_layout_fields[] = {{
        "leaf", "tbe_cbind_test_one", offsetof(tbe_cbind_depth_shared, leaf),
        sizeof(tbe_cbind_test_one), _Alignof(tbe_cbind_test_one),
        &tbe_cbind_test_one_type, NULL}};
    cmeta_struct_desc shared_layout = {
        "tbe_cbind_depth_shared", sizeof(tbe_cbind_depth_shared),
        _Alignof(tbe_cbind_depth_shared), shared_layout_fields, 1u};
    cmeta_data_field_desc shared_data_fields[] = {{
        "test.tbe-cbind.depth-shared.leaf", "leaf",
        offsetof(tbe_cbind_depth_shared, leaf), &tbe_cbind_test_one_data}};
    cmeta_data_struct_shape shared_shape = {
        &shared_layout, shared_data_fields, 1u};
    cmeta_data_desc shared_data = {
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
        "test.tbe-cbind.depth-shared.data", "tbe_cbind_depth_shared",
        CMETA_DATA_STRUCT, &shared_type, &shared_shape, NULL};
    cmeta_field_desc wrapper_layout_fields[] = {{
        "shared", "tbe_cbind_depth_shared",
        offsetof(tbe_cbind_depth_wrapper, shared),
        sizeof(tbe_cbind_depth_shared), _Alignof(tbe_cbind_depth_shared),
        &shared_type, NULL}};
    cmeta_struct_desc wrapper_layout = {
        "tbe_cbind_depth_wrapper", sizeof(tbe_cbind_depth_wrapper),
        _Alignof(tbe_cbind_depth_wrapper), wrapper_layout_fields, 1u};
    cmeta_data_field_desc wrapper_data_fields[] = {{
        "test.tbe-cbind.depth-wrapper.shared", "shared",
        offsetof(tbe_cbind_depth_wrapper, shared), &shared_data}};
    cmeta_data_struct_shape wrapper_shape = {
        &wrapper_layout, wrapper_data_fields, 1u};
    cmeta_data_desc wrapper_data = {
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
        "test.tbe-cbind.depth-wrapper.data", "tbe_cbind_depth_wrapper",
        CMETA_DATA_STRUCT, &wrapper_type, &wrapper_shape, NULL};
    cmeta_field_desc root_layout_fields[] = {
        {"shallow", "tbe_cbind_depth_shared",
         offsetof(tbe_cbind_depth_root, shallow),
         sizeof(tbe_cbind_depth_shared), _Alignof(tbe_cbind_depth_shared),
         &shared_type, NULL},
        {"deep", "tbe_cbind_depth_wrapper",
         offsetof(tbe_cbind_depth_root, deep), sizeof(tbe_cbind_depth_wrapper),
         _Alignof(tbe_cbind_depth_wrapper), &wrapper_type, NULL}};
    cmeta_struct_desc root_layout = {
        "tbe_cbind_depth_root", sizeof(tbe_cbind_depth_root),
        _Alignof(tbe_cbind_depth_root), root_layout_fields, 2u};
    cmeta_data_field_desc root_data_fields[] = {
        {"test.tbe-cbind.depth-root.shallow", "shallow",
         offsetof(tbe_cbind_depth_root, shallow), &shared_data},
        {"test.tbe-cbind.depth-root.deep", "deep",
         offsetof(tbe_cbind_depth_root, deep), &wrapper_data}};
    cmeta_data_struct_shape root_shape = {
        &root_layout, root_data_fields, 2u};
    cmeta_data_desc root_data = {
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION,
        "test.tbe-cbind.depth-root.data", "tbe_cbind_depth_root",
        CMETA_DATA_STRUCT, &root_type, &root_shape, NULL};
    tbe_cbind_semantic_field leaf_fields[] = {{
        "value", "value", "value", "int32", TBE_CBIND_SEMANTIC_INT32,
        NULL}};
    tbe_cbind_semantic_field shared_fields[] = {{
        "leaf", "leaf", "leaf", "Leaf", TBE_CBIND_SEMANTIC_RECORD, NULL}};
    tbe_cbind_semantic_field wrapper_fields[] = {{
        "shared", "shared", "shared", "Shared",
        TBE_CBIND_SEMANTIC_RECORD, NULL}};
    tbe_cbind_semantic_field root_fields[] = {
        {"shallow", "shallow", "shallow", "Shared",
         TBE_CBIND_SEMANTIC_RECORD, NULL},
        {"deep", "deep", "deep", "Wrapper", TBE_CBIND_SEMANTIC_RECORD,
         NULL}};
    tbe_cbind_semantic_type types[] = {
        {"Leaf", leaf_fields, 1u, 1u, 2u},
        {"Shared", shared_fields, 1u, 2u, 2u},
        {"Wrapper", wrapper_fields, 1u, 1u, 2u},
        {"Root", root_fields, 2u, 1u, 2u}};
    tbe_cbind_schema_model model = {0};
    tbe_cbind_plan_options options;
    tbe_cbind_plan_error error;
    fail_allocator_state allocator_state = {0u, SIZE_MAX, 0u};
    tbe_cbind_allocator allocator = {
        &allocator_state, fail_allocator_calloc, fail_allocator_free};
    tbe_cbind_build_context context = {0};
    tbe_cbind_plan *plan = NULL;

    shared_fields[0].record_type = &types[0];
    wrapper_fields[0].record_type = &types[1];
    root_fields[0].record_type = &types[1];
    root_fields[1].record_type = &types[2];
    model.types = types;
    model.type_count = 4u;
    model.root = &types[3];
    tbe_cbind_plan_options_init(&options);
    options.max_depth = 3u;
    tbe_cbind_plan_error_init(&error);
    context.options = &options;
    context.error = &error;
    context.allocator = allocator;

    check_equal(tbe_cbind_plan_build(&context, &model, &root_data, &plan),
                TBE_CBIND_LIMIT_EXCEEDED);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_PLAN);
    check_equal(error.path, "Shared");
    check_equal(allocator_state.live_count, (size_t)0u);
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

  it("returns a native-shape error for a nested null reflected field name") {
    static const char schema[] =
        "composite Detail { int32 quantity; } "
        "message Root { Detail detail; double score; }";
    cmeta_field_desc inner_layout_field =
        tbe_cbind_test_inner_layout_fields[0];
    cmeta_struct_desc inner_layout = tbe_cbind_test_inner_layout;
    cmeta_data_struct_shape inner_shape = tbe_cbind_test_inner_shape;
    cmeta_data_desc inner_data = tbe_cbind_test_inner_data;
    cmeta_data_field_desc root_fields[2];
    cmeta_data_struct_shape root_shape = tbe_cbind_test_nested_shape;
    cmeta_data_desc root_data = tbe_cbind_test_nested_data;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    inner_layout_field.name = NULL;
    inner_layout.fields = &inner_layout_field;
    inner_shape.layout = &inner_layout;
    inner_data.shape = &inner_shape;
    memcpy(root_fields, tbe_cbind_test_nested_data_fields,
           sizeof(root_fields));
    root_fields[0].value = &inner_data;
    root_shape.fields = root_fields;
    root_data.shape = &root_shape;

    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Root", &root_data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);
  }

  it("rejects a root count mismatch before crossing a guarded field array") {
    static const char schema[] = "message One { int32 value; }";
    cmeta_field_desc layout_value = tbe_cbind_test_one_layout_fields[0];
    void *allocation = NULL;
    const cmeta_field_desc *guarded_layout =
        (const cmeta_field_desc *)tbe_cbind_test_guarded_copy_create(
            &layout_value, sizeof(layout_value), &allocation);
    cmeta_struct_desc layout = tbe_cbind_test_one_layout;
    cmeta_data_struct_shape shape = tbe_cbind_test_one_shape;
    cmeta_data_desc data = tbe_cbind_test_one_data;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    check_not_null(guarded_layout);
    layout.fields = guarded_layout;
    layout.field_count = 2u;
    shape.layout = &layout;
    data.shape = &shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "One", &data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);
    tbe_cbind_test_guarded_prefix_destroy(allocation);
  }

  it("rejects a nested count mismatch before crossing a guarded field array") {
    static const char schema[] =
        "composite Detail { int32 quantity; } "
        "message Root { Detail detail; double score; }";
    cmeta_field_desc layout_value = tbe_cbind_test_inner_layout_fields[0];
    void *allocation = NULL;
    const cmeta_field_desc *guarded_layout =
        (const cmeta_field_desc *)tbe_cbind_test_guarded_copy_create(
            &layout_value, sizeof(layout_value), &allocation);
    cmeta_struct_desc inner_layout = tbe_cbind_test_inner_layout;
    cmeta_data_struct_shape inner_shape = tbe_cbind_test_inner_shape;
    cmeta_data_desc inner_data = tbe_cbind_test_inner_data;
    cmeta_data_field_desc root_fields[2];
    cmeta_data_struct_shape root_shape = tbe_cbind_test_nested_shape;
    cmeta_data_desc root_data = tbe_cbind_test_nested_data;
    tbe_cbind_plan_error error;
    tbe_cbind_plan *plan = NULL;

    check_not_null(guarded_layout);
    inner_layout.fields = guarded_layout;
    inner_layout.field_count = 2u;
    inner_shape.layout = &inner_layout;
    inner_data.shape = &inner_shape;
    memcpy(root_fields, tbe_cbind_test_nested_data_fields,
           sizeof(root_fields));
    root_fields[0].value = &inner_data;
    root_shape.fields = root_fields;
    root_data.shape = &root_shape;
    tbe_cbind_plan_error_init(&error);
    check_equal(create_plan(schema, "Root", &root_data, &plan, &error),
                TBE_CBIND_NATIVE_SHAPE_ERROR);
    check_null(plan);
    check_equal(error.phase, TBE_CBIND_PHASE_NATIVE_SHAPE);
    tbe_cbind_test_guarded_prefix_destroy(allocation);
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

  it("cleans multi-occurrence pair-index allocations under deterministic OOM") {
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
