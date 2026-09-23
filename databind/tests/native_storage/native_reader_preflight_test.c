/* #99 adversarial preflight: a shallow-valid graph is not necessarily a safe
 * native layout. All decoding and source-state checks use the real APIs. */
#define TINYTEST_NO_MAIN
#include "data_bind_native.h"
#include "reader_probe.h"
#include <salts_cmeta_data.h>
#include <tinytest.h>
#include <stdio.h>
#include <string.h>

enum { PREFLIGHT_WORKSPACE_BYTES = 4096, PREFLIGHT_DEPTH = 8, PREFLIGHT_ITEMS = 64 };
typedef union PreflightWorkspace {
  max_align_t alignment;
  unsigned char bytes[PREFLIGHT_WORKSPACE_BYTES];
} PreflightWorkspace;
typedef struct PreflightPair { int32_t a; int32_t b; } PreflightPair;

static PreflightWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static NativeReaderProbe probe;
static cserde_reader reader;

static void open_preflight_source(const NativeReaderProbeStep *steps, size_t count) {
  check_equal(native_reader_probe_open(&probe, steps, count, &reader), CSERDE_OK);
}

static void require_schema_rejection(const cmeta_data_desc *shape,
                                     void *destination, size_t size) {
  DataBindStatus status;
  check_true(cmeta_data_desc_valid(shape));
  status = data_bind_native_decode(&options, shape, &reader, destination, size, &diagnostic);
  (void)printf("PREFLIGHT id=%s status=%d calls=%zu message=%s\n",
               shape->stable_id, (int)status, probe.calls, diagnostic.error.message);
  check_equal(status, DATA_BIND_ERR_SCHEMA);
  check_equal(probe.calls, 0u);
}

static void require_pair_layout(bool overlap) {
  PreflightPair output = {0, 0};
  const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.preflight.Pair");
  const cmeta_type_desc type = {
      .name = "PreflightPair", .size = sizeof(output), .align = _Alignof(PreflightPair),
      .kind = CMETA_T_OBJECT, .identity = &identity};
  const size_t second_offset = overlap ? offsetof(PreflightPair, a) : offsetof(PreflightPair, b);
  const cmeta_field_desc fields[] = {
      {.name = "a", .type_name = "int32_t", .offset = offsetof(PreflightPair, a),
       .size = sizeof(int32_t), .align = _Alignof(int32_t), .type = salts_int32_cmeta_data.storage_type},
      {.name = "b", .type_name = "int32_t", .offset = second_offset,
       .size = sizeof(int32_t), .align = _Alignof(int32_t), .type = salts_int32_cmeta_data.storage_type}};
  const cmeta_struct_desc layout = {"PreflightPair", sizeof(output), _Alignof(PreflightPair), fields, 2u};
  const cmeta_data_field_desc values[] = {
      {"test.preflight.a", "a", offsetof(PreflightPair, a), &salts_int32_cmeta_data},
      {"test.preflight.b", "b", second_offset, &salts_int32_cmeta_data}};
  const cmeta_data_struct_shape record = {&layout, values, 2u};
  const cmeta_data_desc shape = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.preflight.Pair.data", .display_name = "PreflightPair",
      .kind = CMETA_DATA_STRUCT, .storage_type = &type, .shape = &record};
  static const unsigned char a[] = {'a'}, b[] = {'b'};
  const NativeReaderProbeStep steps[] = {
      native_reader_probe_token(CSERDE_MAP_BEGIN),
      native_reader_probe_slice(CSERDE_STRING, a, sizeof(a), CSERDE_VIEW_TRANSIENT),
      native_reader_probe_sint(7),
      native_reader_probe_slice(CSERDE_STRING, b, sizeof(b), CSERDE_VIEW_TRANSIENT),
      native_reader_probe_sint(9), native_reader_probe_token(CSERDE_MAP_END)};
  open_preflight_source(steps, sizeof(steps) / sizeof(steps[0]));
  check_true(cmeta_data_desc_valid(&shape));
  if (overlap) {
    require_schema_rejection(&shape, &output, sizeof(output));
    check_equal(output.a, 0);
    check_equal(output.b, 0);
  } else {
    check_equal(data_bind_native_decode(&options, &shape, &reader, &output,
                sizeof(output), &diagnostic), DATA_BIND_OK);
    check_equal(output.a, 7);
    check_equal(output.b, 9);
    check_equal(probe.calls, 6u);
  }
}

static void require_diagnostic_alias_rejection(bool aliases_workspace) {
  const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
  unsigned char before[sizeof(DataBindNativeDiagnostic)];
  DataBindNativeDiagnostic *aliased = &diagnostic;
  void *destination = &diagnostic;
  size_t capacity = sizeof(diagnostic);
  bool separate_output = false;
  DataBindStatus status;
  if (aliases_workspace) {
    memcpy(workspace.bytes, &diagnostic, sizeof(diagnostic));
    aliased = (DataBindNativeDiagnostic *)(void *)workspace.bytes;
    destination = &separate_output;
    capacity = sizeof(separate_output);
  }
  memcpy(before, aliased, sizeof(before));
  open_preflight_source(steps, 1u);
  status = data_bind_native_decode(&options, &cmeta_data_bool, &reader,
                                   destination, capacity, aliased);
  (void)printf("DIAGNOSTIC_ALIAS workspace=%d status=%d calls=%zu unchanged=%d\n",
               (int)aliases_workspace, (int)status, probe.calls,
               (int)(memcmp(before, aliased, sizeof(before)) == 0));
  check_equal(status, DATA_BIND_ERR_INVALID_ARG);
  check_equal(probe.calls, 0u);
  check_equal(memcmp(before, aliased, sizeof(before)), 0);
  check_false(separate_output);
}

spec("DataBind native preflight preserves canonical storage and control records") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = PREFLIGHT_DEPTH;
    options.max_items = PREFLIGHT_ITEMS;
  }

  it("accepts an equivalent scalar descriptor without requiring pointer identity") {
    cmeta_data_desc shape = salts_int32_cmeta_data;
    cmeta_data_integer_shape integer = *(const cmeta_data_integer_shape *)shape.shape;
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(300)};
    int32_t value = 0;
    shape.shape = &integer;
    open_preflight_source(steps, 1u);
    check_equal(data_bind_native_decode(&options, &shape, &reader, &value,
                sizeof(value), &diagnostic), DATA_BIND_OK);
    check_equal(value, 300);
    check_equal(probe.calls, 1u);
  }
  it("rejects a signed shape width that disagrees with its canonical storage") {
    cmeta_data_desc shape = salts_int32_cmeta_data;
    const cmeta_data_integer_shape integer = {8u};
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(300)};
    int32_t value = 0;
    shape.shape = &integer;
    open_preflight_source(steps, 1u);
    require_schema_rejection(&shape, &value, sizeof(value));
    check_equal(value, 0);
  }
  it("rejects an unsigned shape width that disagrees with its canonical storage") {
    cmeta_data_desc shape = salts_uint32_cmeta_data;
    const cmeta_data_integer_shape integer = {8u};
    NativeReaderProbeStep step = native_reader_probe_token(CSERDE_UINT);
    uint32_t value = 0u;
    step.token.value.uint = 300u;
    shape.shape = &integer;
    open_preflight_source(&step, 1u);
    require_schema_rejection(&shape, &value, sizeof(value));
    check_equal(value, 0u);
  }
  it("rejects a floating shape width that disagrees with its canonical storage") {
    cmeta_data_desc shape = cmeta_data_double;
    const cmeta_data_float_shape floating = {32u};
    NativeReaderProbeStep step = native_reader_probe_token(CSERDE_FLOAT);
    double value = 0.0;
    step.token.value.floating = 1.5;
    shape.shape = &floating;
    open_preflight_source(&step, 1u);
    require_schema_rejection(&shape, &value, sizeof(value));
    check_true(value == 0.0);
  }
  it("rejects a different native kind reusing the canonical scalar identity") {
    cmeta_data_desc shape = salts_int32_cmeta_data;
    cmeta_type_desc type = *shape.storage_type;
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
    int32_t value = 0;
    type.kind = CMETA_T_OBJECT;
    shape.storage_type = &type;
    open_preflight_source(steps, 1u);
    require_schema_rejection(&shape, &value, sizeof(value));
    check_equal(value, 0);
  }
  it("accepts distinct canonical Struct field ranges") { require_pair_layout(false); }
  it("rejects overlapping Struct fields before reading or publishing") { require_pair_layout(true); }
  it("does not write an error through a diagnostic overlapping workspace") {
    require_diagnostic_alias_rejection(true);
  }
  it("does not write an error through a diagnostic overlapping destination") {
    require_diagnostic_alias_rejection(false);
  }
}

/* TurboDB #58 prerequisite: allocation-free sizing through the real planner.
 * Existing decode/lifecycle limits and every preceding test stay unchanged. */
static DataBindNativeRequirements measured;

static void require_measurement_failure(const cmeta_data_desc *shape,
                                        DataBindStatus expected) {
  DataBindNativeRequirements before = measured;
  check_equal(data_bind_native_measure(&options, shape, &measured, &diagnostic),
              expected);
  check_equal(memcmp(&measured, &before, sizeof(before)), 0);
  check_equal(probe.calls, 0u);
}

static void require_measured_pair(bool nested, bool short_depth, bool short_items) {
  const cmeta_type_identity pair_id = CMETA_TYPE_ID_ATOM_INIT("test.measure.Pair");
  const cmeta_type_desc pair_type = {
      .name = "MeasuredPair", .size = sizeof(PreflightPair),
      .align = _Alignof(PreflightPair), .kind = CMETA_T_OBJECT, .identity = &pair_id};
  const cmeta_field_desc fields[] = {
      {.name = "a", .type_name = "int32_t", .offset = offsetof(PreflightPair, a),
       .size = sizeof(int32_t), .align = _Alignof(int32_t),
       .type = salts_int32_cmeta_data.storage_type},
      {.name = "b", .type_name = "int32_t", .offset = offsetof(PreflightPair, b),
       .size = sizeof(int32_t), .align = _Alignof(int32_t),
       .type = salts_int32_cmeta_data.storage_type}};
  const cmeta_struct_desc layout = {
      "MeasuredPair", sizeof(PreflightPair), _Alignof(PreflightPair), fields, 2u};
  const cmeta_data_field_desc values[] = {
      {"measure.a", "a", offsetof(PreflightPair, a), &salts_int32_cmeta_data},
      {"measure.b", "b", offsetof(PreflightPair, b), &salts_int32_cmeta_data}};
  const cmeta_data_struct_shape record = {&layout, values, 2u};
  const cmeta_data_desc pair = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.measure.Pair.data", .display_name = "Pair",
      .kind = CMETA_DATA_STRUCT, .storage_type = &pair_type, .shape = &record};
  const cmeta_type_identity wrap_id = CMETA_TYPE_ID_ATOM_INIT("test.measure.Wrapper");
  const cmeta_type_desc wrap_type = {
      .name = "MeasuredWrapper", .size = sizeof(PreflightPair),
      .align = _Alignof(PreflightPair), .kind = CMETA_T_OBJECT, .identity = &wrap_id};
  const cmeta_field_desc wrap_field = {
      .name = "pair", .type_name = "MeasuredPair", .offset = 0u,
      .size = sizeof(PreflightPair), .align = _Alignof(PreflightPair), .type = &pair_type};
  const cmeta_struct_desc wrap_layout = {
      "MeasuredWrapper", sizeof(PreflightPair), _Alignof(PreflightPair), &wrap_field, 1u};
  const cmeta_data_field_desc wrap_value = {"measure.pair", "pair", 0u, &pair};
  const cmeta_data_struct_shape wrap_record = {&wrap_layout, &wrap_value, 1u};
  const cmeta_data_desc wrap = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.measure.Wrapper.data", .display_name = "Wrapper",
      .kind = CMETA_DATA_STRUCT, .storage_type = &wrap_type, .shape = &wrap_record};
  const cmeta_data_desc *shape = nested ? &wrap : &pair;
  static const unsigned char a[] = {'a'}, b[] = {'b'}, pair_name[] = "pair";
  NativeReaderProbeStep steps[9];
  size_t n = 0u;
  PreflightPair output = {0, 0};
  const size_t depth = nested ? 3u : 2u;
  const size_t nodes = nested ? 4u : 3u;
  const size_t tracking = nested ? 2u : 1u;
  options.max_depth = depth - (short_depth ? 1u : 0u);
  options.max_items = nodes - (short_items ? 1u : 0u);
  if (short_depth || short_items) {
    require_measurement_failure(shape, DATA_BIND_ERR_LIMIT);
    return;
  }
  check_equal(data_bind_native_measure(&options, shape, &measured, &diagnostic),
              DATA_BIND_OK);
  check_equal(measured.descriptor_depth, depth);
  check_equal(measured.container_depth, nested ? 2u : 1u);
  check_equal(measured.descriptor_nodes, nodes);
  check_equal(measured.field_tracking_bytes, tracking);
  check_equal(measured.staging_bytes, sizeof(output));
  check_equal(measured.lifecycle_bytes, depth * sizeof(const cmeta_data_desc *));
  check_true(measured.decode_bytes <= sizeof(workspace.bytes));
  check_equal((uintptr_t)workspace.bytes % measured.workspace_alignment, 0u);
  if (nested) {
    steps[n++] = native_reader_probe_token(CSERDE_MAP_BEGIN);
    steps[n++] = native_reader_probe_slice(CSERDE_STRING, pair_name,
                                           sizeof(pair_name) - 1u, CSERDE_VIEW_STABLE);
  }
  steps[n++] = native_reader_probe_token(CSERDE_MAP_BEGIN);
  steps[n++] = native_reader_probe_slice(CSERDE_STRING, a, sizeof(a), CSERDE_VIEW_STABLE);
  steps[n++] = native_reader_probe_sint(7);
  steps[n++] = native_reader_probe_slice(CSERDE_STRING, b, sizeof(b), CSERDE_VIEW_STABLE);
  steps[n++] = native_reader_probe_sint(9);
  steps[n++] = native_reader_probe_token(CSERDE_MAP_END);
  if (nested) steps[n++] = native_reader_probe_token(CSERDE_MAP_END);
  open_preflight_source(steps, n);
  options.workspace_bytes = measured.decode_bytes - 1u;
  check_equal(data_bind_native_decode(&options, shape, &reader, &output,
              sizeof(output), &diagnostic), DATA_BIND_ERR_LIMIT);
  check_equal(probe.calls, 0u);
  check_equal(output.a, 0);
  check_equal(output.b, 0);
  options.workspace_bytes = measured.decode_bytes;
  check_equal(data_bind_native_decode(&options, shape, &reader, &output,
              sizeof(output), &diagnostic), DATA_BIND_OK);
  check_equal(probe.calls, n);
  check_equal(output.a, 7);
  check_equal(output.b, 9);
  options.workspace_bytes = measured.lifecycle_bytes;
  check_equal(data_bind_native_clear(&options, shape, &output, sizeof(output),
              &diagnostic), DATA_BIND_OK);
  check_equal(output.a, 0);
  check_equal(output.b, 0);
}

static void require_measure_size_overflow(bool alignment) {
  const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.measure.Huge");
  const cmeta_type_desc type = {
      .name = "Huge", .size = alignment ? 1u : SIZE_MAX,
      .align = alignment ? SIZE_MAX : 1u, .kind = CMETA_T_OBJECT, .identity = &identity};
  const cmeta_struct_desc layout = {"Huge", type.size, type.align, NULL, 0u};
  const cmeta_data_struct_shape record = {&layout, NULL, 0u};
  const cmeta_data_desc shape = {
      .struct_size = sizeof(cmeta_data_desc), .abi_version = CMETA_DATA_DESC_ABI_VERSION,
      .stable_id = "test.measure.Huge.data", .display_name = "Huge",
      .kind = CMETA_DATA_STRUCT, .storage_type = &type, .shape = &record};
  check_true(cmeta_data_desc_valid(&shape));
  require_measurement_failure(&shape, DATA_BIND_ERR_LIMIT);
}

spec("DataBind native workspace measurement before source dispatch") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    measured = (DataBindNativeRequirements)DATA_BIND_NATIVE_REQUIREMENTS_INIT;
    measured.decode_bytes = 123u; /* Detect accidental partial result publication. */
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = PREFLIGHT_DEPTH;
    options.max_items = PREFLIGHT_ITEMS;
  }

  it("rejects zero depth and overflow without modifying the probe-size result") {
    size_t bytes = 91u;
    check_equal(data_bind_native_probe_workspace_size(0u, &bytes), DATA_BIND_ERR_LIMIT);
    check_equal(bytes, 91u);
    check_equal(data_bind_native_probe_workspace_size(SIZE_MAX, &bytes), DATA_BIND_ERR_LIMIT);
    check_equal(bytes, 91u);
    check_equal(data_bind_native_probe_workspace_size(1u, NULL), DATA_BIND_ERR_INVALID_ARG);
  }
  it("measures using only exact traversal storage at every pointer alignment") {
    size_t bound = 0u;
    size_t offset;
    const size_t alignment = _Alignof(const cmeta_data_desc *);
    const size_t traversal = PREFLIGHT_DEPTH * sizeof(const cmeta_data_desc *);
    check_equal(data_bind_native_probe_workspace_size(PREFLIGHT_DEPTH, &bound), DATA_BIND_OK);
    check_equal(bound, traversal + alignment - 1u);
    for (offset = 0u; offset < alignment; ++offset) {
      size_t padding;
      options.workspace = workspace.bytes + offset;
      padding = (uintptr_t)options.workspace % alignment;
      if (padding != 0u) padding = alignment - padding;
      options.workspace_bytes = traversal + padding - 1u;
      require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_LIMIT);
      options.workspace_bytes += 1u;
      check_equal(data_bind_native_measure(&options, &salts_int32_cmeta_data,
                  &measured, &diagnostic), DATA_BIND_OK);
      check_equal(measured.traversal_bytes, traversal);
      check_equal(measured.field_tracking_bytes, 0u);
      check_equal(measured.descriptor_nodes, 1u);
      check_equal(measured.descriptor_depth, 1u);
    }
  }
  it("measures flat-record exact decode and lifecycle boundaries") {
    require_measured_pair(false, false, false);
  }
  it("includes simultaneously active nested field tracking") {
    require_measured_pair(true, false, false);
  }
  it("does not confuse descriptor leaves with container-only depth") {
    require_measured_pair(false, true, false);
  }
  it("rejects one-over node budget without treating it as per-container items") {
    require_measured_pair(false, false, true);
  }
  it("rejects zero item budget and missing probe storage without publication") {
    options.max_items = 0u;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_LIMIT);
    options.max_items = PREFLIGHT_ITEMS;
    options.workspace = NULL;
    options.workspace_bytes = 0u;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_LIMIT);
  }
  it("rejects depth multiplication overflow before touching probe storage") {
    options.max_depth = SIZE_MAX;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_LIMIT);
    check_equal(workspace.bytes[0], 0u);
  }
  it("rejects staging addition overflow") {
    require_measure_size_overflow(false);
  }
  it("rejects common alignment multiplication overflow") {
    require_measure_size_overflow(true);
  }
  it("rejects canonical scalar mismatch through the existing graph validator") {
    cmeta_data_desc shape = salts_int32_cmeta_data;
    const cmeta_data_integer_shape integer = {8u};
    shape.shape = &integer;
    require_measurement_failure(&shape, DATA_BIND_ERR_SCHEMA);
  }
  it("rejects missing descriptors without partial result publication") {
    require_measurement_failure(NULL, DATA_BIND_ERR_INVALID_ARG);
  }
  it("validates output ABI and leaves invalid control records untouched") {
    measured.abi_version += 1u;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_INVALID_ARG);
    measured.abi_version = DATA_BIND_NATIVE_ABI_VERSION;
    measured.size -= 1u;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_INVALID_ARG);
  }
  it("rejects output aliasing probe workspace before mutating either") {
    DataBindNativeRequirements *aliased = (DataBindNativeRequirements *)(void *)workspace.bytes;
    DataBindNativeRequirements before = measured;
    memcpy(workspace.bytes, &measured, sizeof(measured));
    check_equal(data_bind_native_measure(&options, &salts_int32_cmeta_data,
                aliased, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    check_equal(memcmp(workspace.bytes, &before, sizeof(before)), 0);
  }
  it("rejects workspace address overflow before any traversal") {
    options.workspace = (void *)(uintptr_t)(UINTPTR_MAX - 1u);
    options.workspace_bytes = 8u;
    require_measurement_failure(&salts_int32_cmeta_data, DATA_BIND_ERR_INVALID_ARG);
  }
  it("preserves a larger valid result header and does not charge zero payload") {
    struct ExtendedRequirements { DataBindNativeRequirements base; size_t tail; } out;
    out.base = measured;
    out.base.size = sizeof(out);
    out.tail = 91u;
    options.max_owned_bytes = 0u;
    check_equal(data_bind_native_measure(&options, &salts_tstr_cmeta_data,
                &out.base, &diagnostic), DATA_BIND_OK);
    check_equal(out.base.size, sizeof(out));
    check_equal(out.tail, 91u);
    check_equal(out.base.staging_bytes, sizeof(tstr));
    check_equal(out.base.descriptor_nodes, 1u);
    check_equal(diagnostic.error.code, DATA_BIND_OK);
  }
}

static void require_buffer_bounds(const char *left, const char *right,
                                   size_t aggregate, size_t per_value,
                                   DataBindStatus expected) {
  typedef struct BufferPair { tstr left; tstr right; } BufferPair;
  BufferPair output = {NULL, NULL};
  const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.budget.BufferPair");
  const cmeta_type_desc type = {.name = "BufferPair", .size = sizeof(output),
      .align = _Alignof(BufferPair), .kind = CMETA_T_OBJECT, .identity = &identity};
  const cmeta_field_desc fields[] = {
      {.name = "left", .type_name = "tstr", .offset = offsetof(BufferPair, left),
       .size = sizeof(tstr), .align = _Alignof(tstr), .type = &salts_tstr_cmeta_type},
      {.name = "right", .type_name = "tstr", .offset = offsetof(BufferPair, right),
       .size = sizeof(tstr), .align = _Alignof(tstr), .type = &salts_tstr_cmeta_type}};
  const cmeta_struct_desc layout = {"BufferPair", sizeof(output), _Alignof(BufferPair), fields, 2u};
  const cmeta_data_field_desc values[] = {
      {"pair.left", "left", offsetof(BufferPair, left), &salts_tstr_cmeta_data},
      {"pair.right", "right", offsetof(BufferPair, right), &salts_tstr_cmeta_data}};
  const cmeta_data_struct_shape record = {&layout, values, 2u};
  const cmeta_data_desc shape = {.struct_size = sizeof(cmeta_data_desc),
      .abi_version = CMETA_DATA_DESC_ABI_VERSION, .stable_id = "test.budget.pair",
      .display_name = "BufferPair", .kind = CMETA_DATA_STRUCT,
      .storage_type = &type, .shape = &record};
  const NativeReaderProbeStep steps[] = {
      native_reader_probe_token(CSERDE_MAP_BEGIN),
      native_reader_probe_slice(CSERDE_STRING, (const unsigned char *)"left", 4u, CSERDE_VIEW_STABLE),
      native_reader_probe_slice(CSERDE_STRING, (const unsigned char *)left, strlen(left), CSERDE_VIEW_TRANSIENT),
      native_reader_probe_slice(CSERDE_STRING, (const unsigned char *)"right", 5u, CSERDE_VIEW_STABLE),
      native_reader_probe_slice(CSERDE_STRING, (const unsigned char *)right, strlen(right), CSERDE_VIEW_TRANSIENT),
      native_reader_probe_token(CSERDE_MAP_END)};
  PreflightWorkspace memory;
  DataBindNativeOptions limits = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic error = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  NativeReaderProbe source = {0};
  cserde_reader input = {0};
  limits.workspace = memory.bytes;
  limits.workspace_bytes = sizeof(memory.bytes);
  limits.max_depth = 2u;
  limits.max_items = 3u;
  limits.max_owned_bytes = aggregate;
  check_equal(native_reader_probe_open(&source, steps, 6u, &input), CSERDE_OK);
  const DataBindStatus status = data_bind_native_decode_bounded(
      &limits, &shape, &input, &output, sizeof(output), per_value, &error);
  check_equal(status, expected);
  if (expected == DATA_BIND_OK) {
    check_equal(tstr_len(output.left), strlen(left));
    check_equal(tstr_len(output.right), strlen(right));
    if (strlen(left) != 0u) check_equal(memcmp(output.left, left, strlen(left)), 0);
    if (strlen(right) != 0u) check_equal(memcmp(output.right, right, strlen(right)), 0);
    check_equal(source.calls, 6u);
  } else {
    check_null(output.left);
    check_null(output.right);
    check_equal(error.error.code, DATA_BIND_ERR_LIMIT);
  }
  check_equal(data_bind_native_clear(&limits, &shape, &output, sizeof(output), &error), DATA_BIND_OK);
  check_null(output.left);
  check_null(output.right);
}

spec("DataBind per-value payload bounds preserve aggregate accounting and rollback") {
  it("allows two separately bounded values whose sum exceeds the per-value bound") {
    require_buffer_bounds("abc", "def", 6u, 3u, DATA_BIND_OK);
  }
  it("rejects one-over per-value size after an earlier owned value") {
    require_buffer_bounds("abc", "defg", 7u, 3u, DATA_BIND_ERR_LIMIT);
  }
  it("keeps the aggregate limit even when each value fits") {
    require_buffer_bounds("abc", "def", 5u, 3u, DATA_BIND_ERR_LIMIT);
  }
  it("allows empty owned values at zero per-value and aggregate limits") {
    require_buffer_bounds("", "", 0u, 0u, DATA_BIND_OK);
  }
  it("rejects nonempty payload at a zero per-value limit") {
    require_buffer_bounds("", "x", SIZE_MAX, 0u, DATA_BIND_ERR_LIMIT);
  }
  it("accepts the exact per-value boundary with the address-space aggregate bound") {
    require_buffer_bounds("abc", "def", SIZE_MAX, 3u, DATA_BIND_OK);
  }
}
