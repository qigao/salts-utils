#include "data_bind_native.h"
#include "native_test_alignment.h"
#include "reader_probe.h"

#include <cmeta_cmeta_data.h>
#include <tinytest.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  OWNER_FIELD_COUNT = 2,
  OWNER_WORKSPACE_BYTES = 4096,
  OWNER_MAX_DEPTH = 8,
  OWNER_MAX_ITEMS = 16,
  OWNER_MAX_BYTES = 64,
  OWNER_POISON = 0xa5
};

typedef struct AdjacentOwners {
  tstr a;
  tstr b;
  uint8_t presence;
  uint8_t nulls;
} AdjacentOwners;

typedef struct NestedOwners {
  AdjacentOwners child;
  uint8_t presence;
} NestedOwners;

typedef union OwnerWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[OWNER_WORKSPACE_BYTES];
} OwnerWorkspace;

static const cmeta_type_identity OWNER_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.native-ownership.AdjacentOwners");
static const cmeta_type_desc OWNER_TYPE = {
    .name = "AdjacentOwners", .size = sizeof(AdjacentOwners),
    .align = _Alignof(AdjacentOwners), .kind = CMETA_T_OBJECT,
    .identity = &OWNER_ID};

static const cmeta_type_identity NESTED_OWNER_ID =
    CMETA_TYPE_ID_ATOM_INIT("test.native-ownership.NestedOwners");
static const cmeta_type_desc NESTED_OWNER_TYPE = {
    .name = "NestedOwners", .size = sizeof(NestedOwners),
    .align = _Alignof(NestedOwners), .kind = CMETA_T_OBJECT,
    .identity = &NESTED_OWNER_ID};

typedef struct OwnerGraph {
  cmeta_field_desc layout_fields[OWNER_FIELD_COUNT];
  cmeta_data_field_desc fields[OWNER_FIELD_COUNT];
  cmeta_struct_desc layout;
  cmeta_data_struct_shape shape;
  cmeta_data_desc data;
} OwnerGraph;

static OwnerWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;

static void bind_owner_graph(OwnerGraph *graph) {
  memset(graph, 0, sizeof(*graph));
  graph->layout_fields[0] = (cmeta_field_desc){
      .name = "a", .type_name = "tstr", .offset = offsetof(AdjacentOwners, a),
      .size = sizeof(tstr), .align = _Alignof(tstr),
      .type = cmeta_tstr_cmeta_data.storage_type};
  graph->layout_fields[1] = graph->layout_fields[0];
  graph->layout_fields[1].name = "b";
  graph->layout_fields[1].offset = offsetof(AdjacentOwners, b);
  graph->fields[0] = (cmeta_data_field_desc){
      "test.native-ownership.a", "a", offsetof(AdjacentOwners, a),
      &cmeta_tstr_cmeta_data};
  graph->fields[1] = (cmeta_data_field_desc){
      "test.native-ownership.b", "b", offsetof(AdjacentOwners, b),
      &cmeta_tstr_cmeta_data};
  graph->layout = (cmeta_struct_desc){
      "AdjacentOwners", sizeof(AdjacentOwners), _Alignof(AdjacentOwners),
      graph->layout_fields, OWNER_FIELD_COUNT};
  graph->shape = (cmeta_data_struct_shape){
      &graph->layout, graph->fields, OWNER_FIELD_COUNT};
  graph->data = (cmeta_data_desc){
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.native-ownership.AdjacentOwners.data",
      .display_name = "AdjacentOwners", .kind = CMETA_DATA_STRUCT,
      .storage_type = &OWNER_TYPE, .shape = &graph->shape};
}

static void set_field(OwnerGraph *graph, size_t index, size_t offset,
                      const cmeta_data_desc *value) {
  graph->layout_fields[index].offset = offset;
  graph->layout_fields[index].size = value->storage_type->size;
  graph->layout_fields[index].align = value->storage_type->align;
  graph->layout_fields[index].type = value->storage_type;
  graph->layout_fields[index].type_name = value->storage_type->name;
  graph->fields[index].offset = offset;
  graph->fields[index].value = value;
}

static void bind_nested_owner_graph(OwnerGraph *parent, OwnerGraph *child) {
  bind_owner_graph(child);
  bind_owner_graph(parent);
  set_field(parent, 0u, offsetof(NestedOwners, child), &child->data);
  parent->layout_fields[0].name = parent->fields[0].name = "child";
  parent->layout.name = NESTED_OWNER_TYPE.name;
  parent->layout.size = NESTED_OWNER_TYPE.size;
  parent->layout.align = NESTED_OWNER_TYPE.align;
  parent->layout.field_count = parent->shape.field_count = 1u;
  parent->data.storage_type = &NESTED_OWNER_TYPE;
  parent->data.stable_id = NESTED_OWNER_ID.stable_atom_id;
  parent->data.display_name = NESTED_OWNER_TYPE.name;
}

static cserde_status count_write(void *context, const cserde_token *token) {
  size_t *calls = context;
  (void)token;
  ++*calls;
  return CSERDE_OK;
}

static cserde_status count_finish(void *context) {
  size_t *calls = context;
  ++*calls;
  return CSERDE_OK;
}

static const cserde_writer_ops COUNT_WRITER_OPS = {
    sizeof(cserde_writer_ops), CSERDE_WRITER_OPS_ABI_VERSION,
    count_write, count_finish};

/* One thread owns the complete storage envelope. Admission must finish before
 * any owner, reader or writer is inspected; a rejected operation retains every
 * byte and owner. Valid clear releases both owners and resets overlay bytes. */
static void expect_owner_rejection(const cmeta_data_desc *data) {
  AdjacentOwners object;
  unsigned char before[sizeof(object)];
  NativeReaderProbe probe;
  cserde_reader reader = {0};
  cserde_writer writer = {0};
  size_t writes = 0u;
  memset(&object, OWNER_POISON, sizeof(object));
  memcpy(before, &object, sizeof(object));
  check_equal(native_reader_probe_open(&probe, NULL, 0u, &reader), CSERDE_OK);
  check_equal(cserde_writer_init(&writer, &COUNT_WRITER_OPS, &writes), CSERDE_OK);
  check_equal(data_bind_native_init(&options, data, &object, sizeof(object), &diagnostic),
              DATA_BIND_ERR_SCHEMA);
  check_equal(&object, before, sizeof(object));
  check_equal(data_bind_native_clear(&options, data, &object, sizeof(object), &diagnostic),
              DATA_BIND_ERR_SCHEMA);
  check_equal(&object, before, sizeof(object));
  check_equal(data_bind_native_decode(&options, data, &reader, &object, sizeof(object),
                                     &diagnostic), DATA_BIND_ERR_SCHEMA);
  check_equal(probe.calls, 0u);
  check_equal(&object, before, sizeof(object));
  check_equal(data_bind_native_encode(&options, data, &object, sizeof(object), &writer,
                                     &diagnostic), DATA_BIND_ERR_SCHEMA);
  check_equal(writes, 0u);
  check_equal(&object, before, sizeof(object));
}

static void assign_owners(AdjacentOwners *object) {
  static const unsigned char a[] = "first";
  static const unsigned char b[] = "second";
  check_equal(cmeta_data_buffer_assign(&cmeta_tstr_cmeta_data, &object->a,
                                      a, sizeof(a) - 1u, OWNER_MAX_BYTES), CMETA_OK);
  check_equal(cmeta_data_buffer_assign(&cmeta_tstr_cmeta_data, &object->b,
                                      b, sizeof(b) - 1u, OWNER_MAX_BYTES), CMETA_OK);
  object->presence = UINT8_MAX;
  object->nulls = UINT8_MAX;
}

spec("DataBind canonical ownership admission") {
  before_each() {
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = OWNER_MAX_DEPTH;
    options.max_items = OWNER_MAX_ITEMS;
    options.max_owned_bytes = OWNER_MAX_BYTES;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  }

  it("rejects overlapping owners before touching poisoned storage or IO") {
    OwnerGraph graph;
    bind_owner_graph(&graph);
    set_field(&graph, 1u, offsetof(AdjacentOwners, a), &cmeta_tstr_cmeta_data);
    expect_owner_rejection(&graph.data);
  }

  it("rejects scalar and owner overlap in either declaration order") {
    OwnerGraph graph;
    bind_owner_graph(&graph);
    set_field(&graph, 0u, offsetof(AdjacentOwners, a), &cmeta_data_uint32);
    set_field(&graph, 1u, offsetof(AdjacentOwners, a), &cmeta_tstr_cmeta_data);
    expect_owner_rejection(&graph.data);
    set_field(&graph, 0u, offsetof(AdjacentOwners, a), &cmeta_tstr_cmeta_data);
    set_field(&graph, 1u, offsetof(AdjacentOwners, a), &cmeta_data_uint32);
    expect_owner_rejection(&graph.data);
  }

  it("rejects invalid nested owners before touching their parent") {
    OwnerGraph parent, child;
    bind_owner_graph(&parent);
    bind_owner_graph(&child);
    set_field(&child, 1u, offsetof(AdjacentOwners, a), &cmeta_tstr_cmeta_data);
    set_field(&parent, 0u, 0u, &child.data);
    parent.layout.field_count = parent.shape.field_count = 1u;
    expect_owner_rejection(&parent.data);
  }

  it("rejects owner offset addition overflow before lifecycle or IO") {
    OwnerGraph graph;
    bind_owner_graph(&graph);
    set_field(&graph, 1u, SIZE_MAX - 1u, &cmeta_tstr_cmeta_data);
    expect_owner_rejection(&graph.data);
  }

  it("rejects owner storage extending past the complete host envelope") {
    OwnerGraph graph;
    bind_owner_graph(&graph);
    set_field(&graph, 1u, sizeof(AdjacentOwners), &cmeta_tstr_cmeta_data);
    expect_owner_rejection(&graph.data);
  }

  it("rejects owner metadata disagreeing with its physical layout") {
    OwnerGraph graph;
    bind_owner_graph(&graph);
    graph.layout_fields[1].offset = offsetof(AdjacentOwners, a);
    expect_owner_rejection(&graph.data);
  }

  it("releases adjacent owners and overlay state then permits reuse") {
    OwnerGraph graph;
    AdjacentOwners object;
    bind_owner_graph(&graph);
    memset(&object, OWNER_POISON, sizeof(object));
    check_equal(data_bind_native_init(&options, &graph.data, &object, sizeof(object),
                                     &diagnostic), DATA_BIND_OK);
    check_null(object.a);
    check_null(object.b);
    check_equal(object.presence, 0u);
    check_equal(object.nulls, 0u);
    assign_owners(&object);
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_OK);
    check_null(object.a);
    check_null(object.b);
    check_equal(object.presence, 0u);
    check_equal(object.nulls, 0u);
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_OK);
    assign_owners(&object);
    check_equal(object.a, "first");
    check_equal(object.b, "second");
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_OK);
  }

  it("retains live adjacent owners when a malformed clear is rejected") {
    OwnerGraph graph;
    AdjacentOwners object;
    unsigned char before[sizeof(object)];
    bind_owner_graph(&graph);
    check_equal(data_bind_native_init(&options, &graph.data, &object, sizeof(object),
                                     &diagnostic), DATA_BIND_OK);
    assign_owners(&object);
    memcpy(before, &object, sizeof(object));
    set_field(&graph, 1u, offsetof(AdjacentOwners, a), &cmeta_tstr_cmeta_data);
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_equal(&object, before, sizeof(object));
    check_equal(object.a, "first");
    check_equal(object.b, "second");
    set_field(&graph, 1u, offsetof(AdjacentOwners, b), &cmeta_tstr_cmeta_data);
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_OK);
  }

  it("restores nested overlays through raw and admitted lifecycles") {
    OwnerGraph parent, child;
    NestedOwners object;
    DataBindNativePlan *plan = NULL;
    DataBindStatus status;
    bind_nested_owner_graph(&parent, &child);
    check_equal(data_bind_native_init(&options, &parent.data, &object,
                                     sizeof(object), &diagnostic), DATA_BIND_OK);
    assign_owners(&object.child);
    object.presence = UINT8_MAX;
    check_equal(data_bind_native_clear(&options, &parent.data, &object,
                                      sizeof(object), &diagnostic), DATA_BIND_OK);
    check_null(object.child.a);
    check_null(object.child.b);
    check_equal(object.child.presence, 0u);
    check_equal(object.child.nulls, 0u);
    check_equal(object.presence, 0u);
    check_equal(data_bind_native_clear(&options, &parent.data, &object,
                                      sizeof(object), &diagnostic), DATA_BIND_OK);

    check_equal(data_bind_native_plan_compile(&options, &parent.data, &plan,
                                              &diagnostic), DATA_BIND_OK);
    assign_owners(&object.child);
    object.presence = UINT8_MAX;
    status = data_bind_native_plan_clear(plan, &options, &object,
                                         sizeof(object), &diagnostic);
    data_bind_native_plan_free(plan);
    check_equal(status, DATA_BIND_OK);
    check_null(object.child.a);
    check_null(object.child.b);
    check_equal(object.child.presence, 0u);
    check_equal(object.child.nulls, 0u);
    check_equal(object.presence, 0u);
  }

  it("accepts canonical negative floating zero before native publication") {
    double value = -0.0;
    bool zero = false;
    NativeReaderProbe probe;
    NativeReaderProbeStep step = native_reader_probe_token(CSERDE_FLOAT);
    cserde_reader reader = {0};
    step.token.value.floating = 1.0;
    check_equal(cmeta_data_value_is_zero(&cmeta_data_double, &value, &zero), CMETA_OK);
    check_true(zero);
    check_equal(native_reader_probe_open(&probe, &step, 1u, &reader), CSERDE_OK);
    check_equal(data_bind_native_decode(&options, &cmeta_data_double, &reader,
                                        &value, sizeof(value), &diagnostic), DATA_BIND_OK);
    check_true(value == step.token.value.floating);
    check_equal(data_bind_native_clear(&options, &cmeta_data_double, &value,
                                      sizeof(value), &diagnostic), DATA_BIND_OK);
    check_true(value == 0.0);
  }

  it("retains both live owners on a lifecycle budget failure and permits retry") {
    OwnerGraph graph;
    AdjacentOwners object;
    unsigned char before[sizeof(object)];
    bind_owner_graph(&graph);
    check_equal(data_bind_native_init(&options, &graph.data, &object, sizeof(object),
                                     &diagnostic), DATA_BIND_OK);
    assign_owners(&object);
    memcpy(before, &object, sizeof(object));
    options.max_items = 1u;
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(&object, before, sizeof(object));
    check_equal(object.a, "first");
    check_equal(object.b, "second");
    options.max_items = OWNER_MAX_ITEMS;
    check_equal(data_bind_native_clear(&options, &graph.data, &object, sizeof(object),
                                      &diagnostic), DATA_BIND_OK);
    check_null(object.a);
    check_null(object.b);
  }
}
