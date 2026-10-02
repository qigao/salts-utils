#include "data_bind_method_plan.h"

#include "installed_service.http.h"
#include "installed_service.rpc.h"
#include "installed_service.service_native.h"
#include "installed_service_native.h"

#include <string.h>

int main(void) {
  DataBindNativeTypeBinding request = {0};
  DataBindNativeTypeBinding response = {0};
  DataBindServiceNativeBinding native = {0};
  const DataBindNativeExecution *execution = NULL;
  cflow_function_typed_adapter_projection cflow_projection = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindBindingPlanDiagnostic diagnostic =
      DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  const DataBindHttpProjectionConfig *http_config;
  const DataBindRpcProjectionConfig *rpc_config;
  DataBindHttpMethodPlan *http = NULL;
  DataBindRpcMethodPlan *rpc = NULL;
  DataBindTransportPlanInfo transport =
      DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
  DataBind *codec = NULL;
  DataBindStatus (*write_inputs_fn)(
      const DataBindBindingPlan *,
      const DataBindBindingProvider *,
      const DataBindBindingCallFrame *,
      DataBindBindingPlanDiagnostic *) =
      data_bind_binding_plan_write_inputs;
  DataBindStatus (*bind_outcome_fn)(
      const DataBindBindingPlan *,
      const DataBindBindingProvider *,
      const DataBindNativeOptions *,
      DataBindBindingCallFrame *,
      const DataBindBindingOutcome *,
      DataBindBindingPlanDiagnostic *) =
      data_bind_binding_plan_bind_outcome;
  int failed = 0;

  if (write_inputs_fn == NULL || bind_outcome_fn == NULL)
    return 10;

  if (databind_10_ServiceSdk_4_Calc_3_Add__databind_native_binding(
          &request, &response, &native, &error) != DATA_BIND_OK ||
      native.function == NULL)
    return 1;
  execution =
      databind_10_ServiceSdk_4_Calc_3_Add__databind_execution();
  if (execution == NULL || !data_bind_native_execution_valid(execution))
    return 11;
  if (databind_10_ServiceSdk_4_Calc_3_Add__databind_cflow_projection(
          &cflow_projection) != CFLOW_FUNCTION_PROJECTION_OK ||
      !cflow_function_typed_adapter_projection_valid(&cflow_projection) ||
      cflow_projection.function != native.function)
    return 12;

  http_config = data_bind_http_projection_artifact_find(
      &databind_installed_service_http_projection, "Calc", "Add");
  rpc_config = data_bind_rpc_projection_artifact_find(
      &databind_installed_service_rpc_projection, "Calc", "Add");
  if (http_config == NULL || rpc_config == NULL) return 2;

  if (ServiceSdk_codec_create(&codec, &error) != DATA_BIND_OK ||
      codec == NULL)
    return 3;

  if (data_bind_http_method_plan_compile_service(
          codec, "Calc", "Add", http_config, &native,
          &http, &diagnostic) != DATA_BIND_OK ||
      http == NULL)
    failed = 4;
  if (!failed &&
      (data_bind_rpc_method_plan_compile_service(
           codec, "Calc", "Add", rpc_config, &native,
           &rpc, &diagnostic) != DATA_BIND_OK ||
       rpc == NULL))
    failed = 5;

  data_bind_free(codec);
  codec = NULL;
  if (failed) goto cleanup;

  if (strcmp(data_bind_http_method_plan_method(http), "POST") != 0 ||
      strcmp(data_bind_http_method_plan_route(http), "/Calc/Add") != 0 ||
      strcmp(data_bind_rpc_method_plan_wire_method(rpc), "Calc.Add") != 0) {
    failed = 6;
    goto cleanup;
  }

  if (data_bind_binding_plan_function(
          data_bind_http_method_plan_binding(http)) != native.function ||
      data_bind_binding_plan_function(
          data_bind_rpc_method_plan_binding(rpc)) != native.function) {
    failed = 7;
    goto cleanup;
  }

  if (!data_bind_transport_plan_info(
          data_bind_http_method_plan_transport(http), &transport) ||
      transport.kind != DATA_BIND_TRANSPORT_HTTP ||
      strcmp(transport.service_name, "Calc") != 0 ||
      strcmp(transport.operation_name, "Add") != 0) {
    failed = 8;
    goto cleanup;
  }

  transport = (DataBindTransportPlanInfo)DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
  if (!data_bind_transport_plan_info(
          data_bind_rpc_method_plan_transport(rpc), &transport) ||
      transport.kind != DATA_BIND_TRANSPORT_RPC ||
      strcmp(transport.service_name, "Calc") != 0 ||
      strcmp(transport.operation_name, "Add") != 0) {
    failed = 9;
    goto cleanup;
  }

cleanup:
  data_bind_rpc_method_plan_free(rpc);
  data_bind_http_method_plan_free(http);
  return failed;
}
