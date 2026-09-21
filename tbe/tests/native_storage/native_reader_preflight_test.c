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
