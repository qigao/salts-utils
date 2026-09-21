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
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    DataBindNativeOptions limited = options;
    check_equal(data_bind_native_workspace_requirements(&options, &shape, &required,
                  &diagnostic), DATA_BIND_OK);
    check_equal(required.descriptor_depth, 2u);
    check_equal(required.descriptor_nodes, 3u);
    check_equal(required.field_tracking_bytes, 2u);
    check_equal(required.workspace_bytes, required.traversal_bytes +
                  required.staging_bytes + required.field_tracking_bytes);
    check_equal(probe.calls, 0u);
    memcpy(&before, &required, sizeof(before));
    limited.max_depth = 1u;
    check_equal(data_bind_native_workspace_requirements(&limited, &shape, &required,
                  &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    limited = options;
    limited.max_items = 2u;
    check_equal(data_bind_native_workspace_requirements(&limited, &shape, &required,
                  &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    check_equal(probe.calls, 0u);
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


spec("DataBind native workspace requirements use the production graph validator") {
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.max_depth = PREFLIGHT_DEPTH;
    options.max_items = PREFLIGHT_ITEMS;
  }

  it("measures a scalar without caller workspace or source callbacks") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeOptions before;
    memcpy(&before, &options, sizeof(before));
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_OK);
    check_equal(required.descriptor_depth, 1u);
    check_equal(required.descriptor_nodes, 1u);
    check_equal(required.field_tracking_bytes, 0u);
    check_equal(required.traversal_bytes, PREFLIGHT_DEPTH * sizeof(const cmeta_data_desc *));
    check_true(required.staging_bytes >= sizeof(int32_t));
    check_equal(required.workspace_bytes, required.traversal_bytes + required.staging_bytes);
    check_equal(required.workspace_alignment % _Alignof(int32_t), 0u);
    check_equal(required.workspace_alignment % _Alignof(const cmeta_data_desc *), 0u);
    check_equal(memcmp(&options, &before, sizeof(before)), 0);
    check_equal(probe.calls, 0u);
  }

  it("ignores a supplied workspace while measuring and never mutates it") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    unsigned char byte = 0xa5u;
    options.workspace = &byte;
    options.workspace_bytes = 1u;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_OK);
    check_equal(byte, (unsigned char)0xa5u);
    check_equal(options.workspace_bytes, 1u);
  }

  it("decodes with exactly the measured aligned storage") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(300)};
    int32_t value = 91;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_OK);
    check_true(required.workspace_bytes <= sizeof(workspace.bytes));
    check_equal((uintptr_t)workspace.bytes % required.workspace_alignment, 0u);
    options.workspace = workspace.bytes;
    options.workspace_bytes = required.workspace_bytes;
    open_preflight_source(steps, 1u);
    check_equal(data_bind_native_init(&options, &salts_int32_cmeta_data, &value,
                  sizeof(value), &diagnostic), DATA_BIND_OK);
    check_equal(value, 0);
    check_equal(data_bind_native_decode(&options, &salts_int32_cmeta_data, &reader,
                  &value, sizeof(value), &diagnostic), DATA_BIND_OK);
    check_equal(value, 300);
    check_equal(probe.calls, 1u);
    check_equal(data_bind_native_clear(&options, &salts_int32_cmeta_data, &value,
                  sizeof(value), &diagnostic), DATA_BIND_OK);
    check_equal(value, 0);
  }

  it("rejects one byte below the measured decode storage before reading") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    const NativeReaderProbeStep steps[] = {native_reader_probe_sint(300)};
    int32_t value = 0;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_OK);
    check_true(required.workspace_bytes > 0u);
    options.workspace = workspace.bytes;
    options.workspace_bytes = required.workspace_bytes - 1u;
    open_preflight_source(steps, 1u);
    check_equal(data_bind_native_decode(&options, &salts_int32_cmeta_data, &reader,
                  &value, sizeof(value), &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(value, 0);
    check_equal(probe.calls, 0u);
  }

  it("preserves output on a descriptor traversal multiplication overflow") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    required.workspace_bytes = 123u;
    memcpy(&before, &required, sizeof(before));
    options.max_depth = SIZE_MAX;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    check_equal(probe.calls, 0u);
  }

  it("preserves output on zero native depth and item budgets") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    memcpy(&before, &required, sizeof(before));
    options.max_depth = 0u;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_ERR_LIMIT);
    options.max_depth = PREFLIGHT_DEPTH;
    options.max_items = 0u;
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_ERR_LIMIT);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
  }

  it("rejects a scalar graph that the production preflight cannot admit") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    memcpy(&before, &required, sizeof(before));
    cmeta_data_desc shape = salts_int32_cmeta_data;
    const cmeta_data_integer_shape integer = {8u};
    shape.shape = &integer;
    check_equal(data_bind_native_workspace_requirements(&options, &shape, &required,
                  &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    check_equal(probe.calls, 0u);
  }

  it("rejects an incompatible requirements ABI without changing its record") {
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    required.abi_version += 1u;
    memcpy(&before, &required, sizeof(before));
    check_equal(data_bind_native_workspace_requirements(&options, &salts_int32_cmeta_data,
                  &required, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    check_equal(probe.calls, 0u);
  }
  it("rejects a cyclic graph without caller workspace and without reader calls") {
    cmeta_data_desc shape = {0};
    const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.requirements.Cycle");
    const cmeta_type_desc type = {
        .name = "Cycle", .size = sizeof(int32_t), .align = _Alignof(int32_t),
        .kind = CMETA_T_OBJECT, .identity = &identity};
    const cmeta_field_desc field = {
        .name = "self", .type_name = "Cycle", .offset = 0u,
        .size = sizeof(int32_t), .align = _Alignof(int32_t), .type = &type};
    const cmeta_struct_desc layout = {
        "Cycle", sizeof(int32_t), _Alignof(int32_t), &field, 1u};
    const cmeta_data_field_desc value = {"test.requirements.self", "self", 0u, &shape};
    const cmeta_data_struct_shape record = {&layout, &value, 1u};
    DataBindNativeWorkspaceRequirements required = DATA_BIND_NATIVE_WORKSPACE_REQUIREMENTS_INIT;
    DataBindNativeWorkspaceRequirements before;
    memcpy(&before, &required, sizeof(before));
    int32_t destination = 0;
    shape.struct_size = sizeof(shape);
    shape.abi_version = CMETA_DATA_DESC_ABI_VERSION;
    shape.stable_id = "test.requirements.Cycle.data";
    shape.display_name = "Cycle";
    shape.kind = CMETA_DATA_STRUCT;
    shape.storage_type = &type;
    shape.shape = &record;
    check_true(cmeta_data_desc_valid(&shape));
    check_equal(data_bind_native_workspace_requirements(&options, &shape, &required,
                  &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_equal(memcmp(&required, &before, sizeof(before)), 0);
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    check_equal(data_bind_native_decode(&options, &shape, &reader, &destination,
                  sizeof(destination), &diagnostic), DATA_BIND_ERR_SCHEMA);
    check_equal(destination, 0);
    check_equal(probe.calls, 0u);
  }

  it("rejects an output record aliasing options without mutating either record") {
    union AliasedControls {
      max_align_t alignment;
      unsigned char bytes[sizeof(DataBindNativeWorkspaceRequirements) + sizeof(DataBindNativeOptions)];
    } storage;
    unsigned char before[sizeof(storage)];
    DataBindNativeOptions *aliased_options = (DataBindNativeOptions *)(void *)storage.bytes;
    DataBindNativeWorkspaceRequirements *aliased_requirements =
        (DataBindNativeWorkspaceRequirements *)(void *)storage.bytes;
    memset(&storage, 0, sizeof(storage));
    aliased_options->size = sizeof(storage);
    aliased_options->abi_version = DATA_BIND_NATIVE_ABI_VERSION;
    memcpy(before, &storage, sizeof(storage));
    check_equal(data_bind_native_workspace_requirements(aliased_options, &salts_int32_cmeta_data,
                  aliased_requirements, &diagnostic), DATA_BIND_ERR_INVALID_ARG);
    check_equal(memcmp(before, &storage, sizeof(storage)), 0);
  }

}
