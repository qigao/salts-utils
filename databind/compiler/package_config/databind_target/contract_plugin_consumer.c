#include "contract_plugin.plugin_client.h"
#include "data_bind_native.h"

#include <string.h>

const cmeta_plugin_manifest *CMETA_PLUGIN_CALL cmeta_plugin_query(uint32_t host_abi);

int main(void) {
  const cmeta_plugin_manifest *manifest = cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION);
  const cmeta_plugin_export *function = NULL;
  databind_11_WasmRuntime_4_Calc_3_Add__native_owner owner = {0};
  DataBindServiceNativeBinding service = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  databind_plugin_client_11_WasmRuntime_10_Calculator client = {0};
  DataBindNativeOptions options = DATA_BIND_NATIVE_OPTIONS_INIT;
  DataBindNativeDiagnostic diagnostic = DATA_BIND_NATIVE_DIAGNOSTIC_INIT;
  DataBindNativePlan *plan = NULL;
  unsigned char workspace[16384] = {0};
  AddRequest request = {0};
  AddResponse response = {0};
  void *params[] = {&request, &response};
  int status = -1;
  if (!manifest || cmeta_plugin_query(CMETA_PLUGIN_ABI_VERSION + 1u) != NULL ||
      !manifest->plugin_id ||
      strcmp(manifest->plugin_id, "WasmRuntime.Calculator") != 0 ||
      manifest->export_count != 1u)
    return 1;
  if (cmeta_plugin_manifest_find_export(
          manifest, "WasmRuntime.Calc.Add", &function) != CMETA_PLUGIN_OK ||
      !function ||
      cmeta_plugin_export_require_function(
          function, "WasmRuntime.Calc", 1u, 0u) != CMETA_PLUGIN_OK ||
      !cmeta_function_desc_equal(function->value.function.desc,
          databind_11_WasmRuntime_4_Calc_3_Add__databind_function()) ||
      !cmeta_function_abi_desc_equal(function->value.function.abi,
          databind_11_WasmRuntime_4_Calc_3_Add__databind_function_abi()))
    return 2;

  if (databind_11_WasmRuntime_4_Calc_3_Add__databind_native_binding(
          &owner, &service, &error) != DATA_BIND_OK ||
      service.function != function->value.function.desc ||
      service.request != &owner.request_binding ||
      service.response != &owner.response_binding ||
      service.request->data != &owner.request_metadata.data ||
      service.response->data != &owner.response_metadata.data ||
      service.error_count != 0u)
    return 3;
  options.workspace = workspace;
  options.workspace_bytes = sizeof(workspace);
  options.max_depth = 16u;
  options.max_items = 128u;
  options.max_owned_bytes = 4096u;
  if (data_bind_native_plan_compile(
          &options, service.request->data, &plan, &diagnostic) != DATA_BIND_OK ||
      !plan)
    return 4;
  if (data_bind_native_plan_init(
          plan, &options, &request, sizeof(request), &diagnostic) != DATA_BIND_OK)
    return 5;
  request.left = 9u;
  request.right = 5u;
  if (!function->value.function.invoke(
          function->value.function.context, &status, params, 2u) ||
      status != 0 || response.sum != 14u || response.product != 45u)
    return 6;
  if (databind_plugin_client_11_WasmRuntime_10_Calculator_valid(&client) ||
      databind_11_WasmRuntime_4_Calc_3_Add_plugin_client_call(
          &client, &request, &response, &status) !=
          CMETA_PLUGIN_INVALID_ARGUMENT)
    return 7;
  if (data_bind_native_plan_clear(
          plan, &options, &request, sizeof(request), &diagnostic) != DATA_BIND_OK ||
      request.left || request.right)
    return 8;
  data_bind_native_plan_free(plan);
  return 0;
}
