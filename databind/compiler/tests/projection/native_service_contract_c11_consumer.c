#include "contract_service.service_native.h"
#include "data_bind_native.h"

#include <stdint.h>
#include <string.h>

/* The exact business signature is NativeSourceIR's canonical C record, never
 * a Binary Record_t alias or an opaque wire view. */
int databind_11_WasmRuntime_4_Calc_3_Add(
    const AddRequest *request, AddResponse *response) {
  if (request == NULL || response == NULL) return -1;
  response->sum = request->left + request->right;
  response->product = request->left * request->right;
  return 0;
}

int main(void) {
  databind_11_WasmRuntime_4_Calc_3_Add__native_owner owner = {0};
  DataBindServiceNativeBinding binding = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diag = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *request_plan = NULL;
  DataBindNativePlan *response_plan = NULL;
  const DataBindNativeExecution *execution =
      databind_11_WasmRuntime_4_Calc_3_Add__databind_execution();
  AddRequest request = {0};
  AddResponse response = {0};
  void *params[] = {&request, &response};
  unsigned char workspace[16384] = {0};
  int native_status = -1;

  if (!data_bind_native_execution_valid(execution) ||
      databind_11_WasmRuntime_4_Calc_3_Add__databind_function() !=
          execution->function ||
      databind_11_WasmRuntime_4_Calc_3_Add__databind_function_abi() !=
          execution->abi)
    return 1;
  if (databind_11_WasmRuntime_4_Calc_3_Add__databind_native_binding(
          &owner, &binding, &error) != DATA_BIND_OK ||
      binding.function != execution->function ||
      binding.request != &owner.request_binding ||
      binding.response != &owner.response_binding ||
      binding.errors != NULL || binding.error_count != 0u ||
      owner.request_binding.data != &owner.request_metadata.data ||
      owner.response_binding.data != &owner.response_metadata.data ||
      owner.request_metadata.reflection.mode != CMETA_DATA_REFLECTION_VALUE ||
      owner.response_metadata.reflection.mode != CMETA_DATA_REFLECTION_VALUE ||
      strcmp(owner.request_binding.idl_type_name, "AddRequest") != 0 ||
      strcmp(owner.response_binding.idl_type_name, "AddResponse") != 0 ||
      owner.request_binding.presence_count != 0u ||
      owner.response_binding.null_count != 0u)
    return 2;

  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = 4096u;
  if (data_bind_native_plan_compile(
          &options, owner.request_binding.data, &request_plan, &diag) != DATA_BIND_OK ||
      data_bind_native_plan_compile(
          &options, owner.response_binding.data, &response_plan, &diag) != DATA_BIND_OK)
    return 3;
  if (data_bind_native_plan_init(
          request_plan, &options, &request, sizeof(request), &diag) != DATA_BIND_OK ||
      data_bind_native_plan_init(
          response_plan, &options, &response, sizeof(response), &diag) != DATA_BIND_OK)
    return 4;
  request.left = 7u;
  request.right = 4u;
  if (!execution->invoke(execution->context, &native_status, params, 2u) ||
      native_status != 0 || response.sum != 11u || response.product != 28u)
    return 5;
  if (data_bind_native_plan_clear(
          response_plan, &options, &response, sizeof(response), &diag) != DATA_BIND_OK ||
      data_bind_native_plan_clear(
          request_plan, &options, &request, sizeof(request), &diag) != DATA_BIND_OK ||
      request.left || response.product)
    return 6;
  data_bind_native_plan_free(response_plan);
  data_bind_native_plan_free(request_plan);
  return 0;
}
