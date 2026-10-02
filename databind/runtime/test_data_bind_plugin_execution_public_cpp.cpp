#include <data_bind_plugin_execution.h>

#include <type_traits>

static_assert(
    std::is_standard_layout_v<DataBindPluginOperationBinding>,
    "Plugin operation binding must remain C-compatible");
static_assert(
    std::is_standard_layout_v<DataBindNativeExecution>,
    "Plugin execution bridge must remain C-compatible");
static_assert(
    std::is_standard_layout_v<salts_plugin_export>,
    "Salts Plugin export must remain C-compatible");

using Admit = int (*)(
    const DataBindPluginOperationBinding *,
    const salts_plugin_export *,
    DataBindNativeExecution *);

static_assert(
    std::is_same_v<
        decltype(&data_bind_plugin_operation_execution_admit),
        Admit>,
    "Plugin execution admission surface must remain explicit");

int main() {
  DataBindNativeExecution execution =
      DATA_BIND_NATIVE_EXECUTION_INIT;
  return data_bind_plugin_operation_execution_admit(
             nullptr, nullptr, &execution)
             ? 1
             : data_bind_native_execution_valid(&execution) ? 2 : 0;
}
