/* #99: the same production declaration must provide C linkage in C++17. */
#include "data_bind_native.h"
#include "reader_probe.h"
#include <cmeta/data.h>
#include <cstddef>
#include <type_traits>

static_assert(std::is_standard_layout<DataBindNativeOptions>::value, "options must have C layout");
static_assert(std::is_standard_layout<DataBindNativeDiagnostic>::value, "diagnostics must have C layout");
using Decode = DataBindStatus (*)(const DataBindNativeOptions *, const cmeta_data_desc *,
                                 cserde_reader *, void *, size_t, DataBindNativeDiagnostic *);
static_assert(std::is_same<decltype(&data_bind_native_decode), Decode>::value, "decode signature drift");
using Lifecycle = DataBindStatus (*)(const DataBindNativeOptions *, const cmeta_data_desc *,
                                     void *, size_t, DataBindNativeDiagnostic *);
static_assert(std::is_same<decltype(&data_bind_native_init), Lifecycle>::value,
              "native init signature drift");
static_assert(std::is_same<decltype(&data_bind_native_clear), Lifecycle>::value,
              "native clear signature drift");

static_assert(std::is_standard_layout<DataBindNativeRequirements>::value,
              "requirements must have C layout");
using Measure = DataBindStatus (*)(const DataBindNativeOptions *, const cmeta_data_desc *,
                                   DataBindNativeRequirements *, DataBindNativeDiagnostic *);
static_assert(std::is_same<decltype(&data_bind_native_measure), Measure>::value,
              "native measurement signature drift");

int main() {
  enum { workspace_bytes = 4096, max_depth = 8, max_items = 64, max_owned_bytes = 32 };
  alignas(std::max_align_t) unsigned char workspace[workspace_bytes] = {};
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = max_depth;
  options.max_items = max_items;
  options.max_owned_bytes = max_owned_bytes;
  NativeReaderProbe probe = {};
  cserde_reader reader = {};
  const NativeReaderProbeStep steps[] = {native_reader_probe_sint(7)};
  int value = 0;
  size_t probe_bytes = 0u;
  DataBindNativeRequirements requirements = DATA_BIND_NATIVE_REQUIREMENTS_INIT;
  if (data_bind_native_probe_workspace_size(max_depth, &probe_bytes) != DATA_BIND_OK ||
      probe_bytes > sizeof(workspace)) return 1;
  if (data_bind_native_measure(&options, &cmeta_data_int, &requirements,
                               &diagnostic) != DATA_BIND_OK) return 1;
  if (requirements.descriptor_depth != 1u || requirements.descriptor_nodes != 1u ||
      requirements.decode_bytes > sizeof(workspace) || probe.calls != 0u) return 1;
  if (native_reader_probe_open(&probe, steps, 1u, &reader) != CSERDE_OK) return 1;
  if (data_bind_native_decode(&options, &cmeta_data_int, &reader, &value, sizeof(value),
                              &diagnostic) != DATA_BIND_OK) return 1;
  return value == 7 && probe.calls == 1u && diagnostic.error.code == DATA_BIND_OK ? 0 : 1;
}
