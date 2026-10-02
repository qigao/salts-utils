#include <data_bind_plugin_execution.h>

int main(void) {
  DataBindNativeExecution execution = DATA_BIND_NATIVE_EXECUTION_INIT;
  if (data_bind_plugin_operation_execution_admit(
          NULL, NULL, &execution))
    return 1;
  return data_bind_native_execution_valid(&execution) ? 2 : 0;
}
