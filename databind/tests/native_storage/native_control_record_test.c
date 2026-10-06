#include "data_bind_native.h"
#include "native_test_alignment.h"
#include "reader_probe.h"
#include <cmeta_cmeta_data.h>
#include <tinytest.h>
#include <string.h>

enum { CONTROL_WORKSPACE_BYTES = 4096, CONTROL_DEPTH = 8, CONTROL_ITEMS = 64 };
typedef union ControlWorkspace {
  DataBindNativeTestAlignment alignment;
  unsigned char bytes[CONTROL_WORKSPACE_BYTES];
} ControlWorkspace;

static ControlWorkspace workspace;
static DataBindNativeOptions options;
static DataBindNativeDiagnostic diagnostic;
static NativeReaderProbe probe;
static cserde_reader reader;

static void open_preflight_source(const NativeReaderProbeStep *steps, size_t count) {
  check_equal(native_reader_probe_open(&probe, steps, count, &reader), CSERDE_OK);
}

static void require_control_record_rejection(
    bool diagnostic_record, size_t record_size, uint32_t abi_version) {
  const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
  DataBindNativePlan *plan = NULL;
  DataBindNativePlan *rejected = NULL;
  DataBindNativeRequirements requirements = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  DataBindNativeRequirements requirements_before = requirements;
  DataBindNativeDiagnostic diagnostic_before;
  int32_t value = 91;

  check_equal(data_bind_native_plan_compile(
                  &options, &cmeta_data_int32, &plan, &diagnostic),
              DATA_BIND_OK);
  check_not_null(plan);
  if (diagnostic_record) {
    diagnostic.size = record_size;
    diagnostic.abi_version = abi_version;
  } else {
    options.size = record_size;
    options.abi_version = abi_version;
  }
  diagnostic_before = diagnostic;
  open_preflight_source(steps, 1u);
  check_equal(data_bind_native_init(
                  &options, &cmeta_data_int32, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_native_clear(
                  &options, &cmeta_data_int32, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_native_decode(
                  &options, &cmeta_data_int32, &reader, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_native_measure(
                  &options, &cmeta_data_int32, &requirements, &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(memcmp(&requirements, &requirements_before, sizeof(requirements)), 0);
  check_equal(data_bind_native_plan_compile(
                  &options, &cmeta_data_int32, &rejected, &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_null(rejected);
  check_equal(data_bind_native_plan_init(
                  plan, &options, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_native_plan_clear(
                  plan, &options, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(data_bind_native_plan_decode(
                  plan, &options, &reader, &value, sizeof(value), &diagnostic),
              DATA_BIND_ERR_INVALID_ARG);
  check_equal(value, 91);
  check_equal(probe.calls, (size_t)0u);
  if (diagnostic_record)
    check_equal(memcmp(&diagnostic, &diagnostic_before, sizeof(diagnostic)), 0);
  data_bind_native_plan_free(plan);
}

spec("DataBind exact native control-record admission") {
  (void)ttest_config__;
  before_each() {
    memset(&workspace, 0, sizeof(workspace));
    memset(&probe, 0, sizeof(probe));
    memset(&reader, 0, sizeof(reader));
    options = (DataBindNativeOptions)DATA_BIND_NATIVE_OPTIONS_INIT;
    diagnostic = (DataBindNativeDiagnostic)DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
    options.workspace = workspace.bytes;
    options.workspace_bytes = sizeof(workspace.bytes);
    options.max_depth = CONTROL_DEPTH;
    options.max_items = CONTROL_ITEMS;
  }

  it("rejects retired native options ABI before lifecycle or source dispatch") {
    require_control_record_rejection(
        false, sizeof(options), DATA_BIND_NATIVE_ABI_VERSION - 1u);
  }
  it("rejects retired native diagnostic ABI without modifying it") {
    require_control_record_rejection(
        true, sizeof(diagnostic), DATA_BIND_NATIVE_ABI_VERSION - 1u);
  }
  it("rejects an options prefix instead of accepting partial control records") {
    require_control_record_rejection(
        false, offsetof(DataBindNativeOptions, abi_version), DATA_BIND_NATIVE_ABI_VERSION);
  }
  it("rejects a diagnostic prefix without modifying it") {
    require_control_record_rejection(
        true, offsetof(DataBindNativeDiagnostic, abi_version), DATA_BIND_NATIVE_ABI_VERSION);
  }
  it("rejects extended options instead of interpreting a known prefix") {
    require_control_record_rejection(
        false, sizeof(options) + 1u, DATA_BIND_NATIVE_ABI_VERSION);
  }
  it("rejects extended diagnostics instead of interpreting a known prefix") {
    require_control_record_rejection(
        true, sizeof(diagnostic) + 1u, DATA_BIND_NATIVE_ABI_VERSION);
  }
}
