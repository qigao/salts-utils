#include "installed_service_flow.flowmq.h"

#include <data_bind_flowmq_plan.h>

#include <string.h>

int main(void) {
  DataBindNativeTypeBinding request = {0};
  DataBindNativeTypeBinding response = {0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  const DataBindFlowMQServicePlan *plan =
      &databind_installed_service_flow_flowmq_service_plan;

  if (plan->abi_version != DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION ||
      plan->service_name == NULL ||
      strcmp(plan->service_name, "ServiceSdk.Calc") != 0 ||
      plan->operation_name == NULL ||
      strcmp(plan->operation_name, "ServiceSdk.Calc.Add") != 0 ||
      plan->pattern != DATA_BIND_FLOWMQ_SERVICE_REQ_REP ||
      plan->ingress_format != DATA_BIND_FORMAT_JSON ||
      plan->egress_format != DATA_BIND_FORMAT_JSON ||
      plan->request_native_binding == NULL ||
      plan->response_native_binding == NULL)
    return 1;

  if (plan->request_native_binding(&request, &error) != DATA_BIND_OK ||
      plan->response_native_binding(&response, &error) != DATA_BIND_OK ||
      request.idl_type_name == NULL ||
      response.idl_type_name == NULL ||
      strcmp(request.idl_type_name, "AddRequest") != 0 ||
      strcmp(response.idl_type_name, "AddResponse") != 0)
    return 2;

  return 0;
}
