#include "installed_contract_service.service_native.h"
#include "data_bind_native.h"

#include <string.h>

int main(void) {
  databind_11_WasmRuntime_4_Calc_3_Add__native_owner owner = {0};
  DataBindServiceNativeBinding binding = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  AddRequest request = {0};
  AddResponse response = {0};
  void *params[2] = {&request, &response};
  int returned = -1;
  const DataBindNativeExecution *execution =
      databind_11_WasmRuntime_4_Calc_3_Add__databind_execution();
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *plan = NULL;
  unsigned char workspace[16384] = {0};

  if (execution == NULL || !data_bind_native_execution_valid(execution) ||
      databind_11_WasmRuntime_4_Calc_3_Add__databind_native_binding(
          &owner, &binding, &error) != DATA_BIND_OK ||
      binding.request != &owner.request_binding ||
      binding.response != &owner.response_binding ||
      binding.function != execution->function ||
      strcmp(binding.request->idl_type_name, "AddRequest") != 0 ||
      binding.request->data != &owner.request_metadata.data ||
      binding.response->data != &owner.response_metadata.data)
    return 1;

  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = 4096u;
  if (data_bind_native_plan_compile(
          &options, binding.request->data, &plan, &diagnostic) != DATA_BIND_OK ||
      data_bind_native_plan_init(
          plan, &options, &request, sizeof(request), &diagnostic) != DATA_BIND_OK)
    return 2;
  request.left = 5u;
  request.right = 6u;
  if (!execution->invoke(execution->context, &returned, params, 2u) ||
      returned != 0 || response.sum != 11u || response.product != 30u)
    return 3;
  if (data_bind_native_plan_clear(
          plan, &options, &request, sizeof(request), &diagnostic) != DATA_BIND_OK ||
      request.left || request.right)
    return 4;
  data_bind_native_plan_free(plan);
  return 0;
}
