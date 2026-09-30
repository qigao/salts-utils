#include "tinytest.h"

#include "data_bind_method_plan.h"

#include "service_native_public.http.h"
#include "service_native_public.rpc.h"
#include "service_native_public.service_native.h"
#include "service_native_public_native.h"

#include <string.h>

spec("DataBind public NATIVE Service artifact") {
  it("compiles HTTP and RPC MethodPlans from one generated Service binding") {
    DataBindNativeTypeBinding request = {0};
    DataBindNativeTypeBinding response = {0};
    DataBindServiceNativeBinding native = {0};
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    const DataBindHttpProjectionConfig *http_config;
    const DataBindRpcProjectionConfig *rpc_config;
    DataBindHttpMethodPlan *http = NULL;
    DataBindRpcMethodPlan *rpc = NULL;
    const DataBindBindingPlan *http_binding;
    const DataBindBindingPlan *rpc_binding;
    DataBindTransportPlanInfo http_transport =
        DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
    DataBindTransportPlanInfo rpc_transport =
        DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo format =
        DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBind *codec = NULL;

    check_equal(
        databind_13_ServiceNative_4_Calc_3_Add__databind_native_binding(
            &request, &response, &native, &error),
        DATA_BIND_OK);
    check_not_null(native.function);
    check_true(
        databind_13_ServiceNative_4_Calc_3_Add__databind_function() ==
        native.function);
    check_not_null(
        databind_13_ServiceNative_4_Calc_3_Add__databind_function_abi());

    http_config = data_bind_http_projection_artifact_find(
        &databind_service_native_public_http_projection, "Calc", "Add");
    rpc_config = data_bind_rpc_projection_artifact_find(
        &databind_service_native_public_rpc_projection, "Calc", "Add");
    check_not_null(http_config);
    check_not_null(rpc_config);

    check_equal(ServiceNative_codec_create(&codec, &error), DATA_BIND_OK);
    check_not_null(codec);

    check_equal(
        data_bind_http_method_plan_compile_service(
            codec, "Calc", "Add", http_config, &native,
            &http, &diagnostic),
        DATA_BIND_OK);
    check_not_null(http);
    check_equal(
        data_bind_rpc_method_plan_compile_service(
            codec, "Calc", "Add", rpc_config, &native,
            &rpc, &diagnostic),
        DATA_BIND_OK);
    check_not_null(rpc);

    /*
     * The generated codec is a control-plane compiler input only.
     * Immutable MethodPlans must retain no schema registry dependency.
     */
    data_bind_free(codec);
    codec = NULL;

    check_equal(data_bind_http_method_plan_method(http), "POST");
    check_equal(data_bind_http_method_plan_route(http), "/Calc/Add");
    check_equal(data_bind_rpc_method_plan_wire_method(rpc), "Calc.Add");

    http_binding = data_bind_http_method_plan_binding(http);
    rpc_binding = data_bind_rpc_method_plan_binding(rpc);
    check_not_null(http_binding);
    check_not_null(rpc_binding);
    check_true(data_bind_binding_plan_function(http_binding) == native.function);
    check_true(data_bind_binding_plan_function(rpc_binding) == native.function);
    check_not_null(data_bind_binding_plan_operation_id(http_binding));
    check_not_null(data_bind_binding_plan_operation_id(rpc_binding));
    check_equal(
        strcmp(data_bind_binding_plan_operation_id(http_binding),
               data_bind_binding_plan_operation_id(rpc_binding)),
        0);

    check(data_bind_transport_plan_info(
        data_bind_http_method_plan_transport(http), &http_transport));
    check_equal(http_transport.kind, DATA_BIND_TRANSPORT_HTTP);
    check_equal(http_transport.service_name, "Calc");
    check_equal(http_transport.operation_name, "Add");
    check(data_bind_format_plan_info(http_transport.ingress, &format));
    check_equal(format.format, DATA_BIND_FORMAT_JSON);
    format = (DataBindFormatPlanInfo)DATA_BIND_FORMAT_PLAN_INFO_INIT;
    check(data_bind_format_plan_info(http_transport.egress, &format));
    check_equal(format.format, DATA_BIND_FORMAT_JSON);

    check(data_bind_transport_plan_info(
        data_bind_rpc_method_plan_transport(rpc), &rpc_transport));
    check_equal(rpc_transport.kind, DATA_BIND_TRANSPORT_RPC);
    check_equal(rpc_transport.service_name, "Calc");
    check_equal(rpc_transport.operation_name, "Add");
    format = (DataBindFormatPlanInfo)DATA_BIND_FORMAT_PLAN_INFO_INIT;
    check(data_bind_format_plan_info(rpc_transport.ingress, &format));
    check_equal(format.format, DATA_BIND_FORMAT_JSON);
    format = (DataBindFormatPlanInfo)DATA_BIND_FORMAT_PLAN_INFO_INIT;
    check(data_bind_format_plan_info(rpc_transport.egress, &format));
    check_equal(format.format, DATA_BIND_FORMAT_JSON);

    data_bind_rpc_method_plan_free(rpc);
    data_bind_http_method_plan_free(http);
  }
}
