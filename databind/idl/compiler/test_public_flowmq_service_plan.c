#include "service.flowmq.h"
#include "tinytest.h"

#include <string.h>

spec("DataBind public FlowMQ ServicePlan frontend") {
  it("publishes canonical Service request/reply facts without runtime policy") {
    DataBindNativeTypeBinding request = {0};
    DataBindNativeTypeBinding response = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    const DataBindFormatProvider *binary_provider;

    check_equal(databind_service_flowmq_service_plan.abi_version,
                (uint32_t)DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION);
    check_equal(databind_service_flowmq_service_plan.service_name,
                "FlowService.Calc");
    check_equal(databind_service_flowmq_service_plan.operation_name,
                "FlowService.Calc.Add");
    check_equal(databind_service_flowmq_service_plan.request_type,
                "AddRequest");
    check_equal(databind_service_flowmq_service_plan.response_type,
                "AddResponse");
    check_equal(databind_service_flowmq_service_plan.ingress_format,
                DATA_BIND_FORMAT_BINARY);
    check_equal(databind_service_flowmq_service_plan.egress_format,
                DATA_BIND_FORMAT_JSON);
    check_equal(databind_service_flowmq_service_plan.pattern,
                DATA_BIND_FLOWMQ_SERVICE_REQ_REP);
    check_equal(databind_service_flowmq_service_plan.max_payload_bytes,
                (size_t)65536u);
    check_not_null(
        databind_service_flowmq_service_plan.request_native_binding);
    check_not_null(
        databind_service_flowmq_service_plan.response_native_binding);

    check_equal(
        databind_service_flowmq_service_plan.request_native_binding(
            &request, &error),
        DATA_BIND_OK);
    check_equal(
        databind_service_flowmq_service_plan.response_native_binding(
            &response, &error),
        DATA_BIND_OK);
    check_equal(request.idl_type_name, "AddRequest");
    check_equal(response.idl_type_name, "AddResponse");
    check_not_null(request.data);
    check_not_null(response.data);

    binary_provider =
        databind_service_binary_AddRequest_databind_binary_provider();
    check_not_null(binary_provider);
    check_equal(binary_provider->format, DATA_BIND_FORMAT_BINARY);
  }
}
