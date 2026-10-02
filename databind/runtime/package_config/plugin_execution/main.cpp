#include <data_bind_plugin_execution.h>

#include <type_traits>

static_assert(std::is_standard_layout_v<DataBindPluginOperationBinding>);
static_assert(std::is_standard_layout_v<DataBindNativeExecution>);
static_assert(std::is_standard_layout_v<salts_plugin_export>);

int main() {
  DataBindNativeExecution execution = DATA_BIND_NATIVE_EXECUTION_INIT;
  return data_bind_plugin_operation_execution_admit(
             nullptr, nullptr, &execution)
             ? 1
             : data_bind_native_execution_valid(&execution) ? 2 : 0;
}
