#include "opaque_service.http.h"
#include "opaque_service.flowmq.h"

#include <data_bind_method_plan.h>
#include <data_bind_opaque_plan.h>
#include <tinytest.h>

#include <string.h>

spec("generated opaque bytes Service composition") {
  it("shares one OpaquePlan across HTTP and FlowMQ without native binding") {
    static const char schema[] =
        "schema OpaqueService [version(1)];"
        "service Raw { Echo: bytes -> bytes; }";
    static const unsigned char payload[] = {0x00u, 0x7fu, 0xffu};
    DataBind *codec = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindBindingPlanDiagnostic diagnostic =
        DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
    DataBindHttpMethodPlan *method_plan = NULL;
    DataBindOpaqueSpan span = DATA_BIND_OPAQUE_SPAN_INIT;
    const DataBindHttpProjectionConfig *http =
        data_bind_http_projection_artifact_find(
            &databind_opaque_service_http_projection, "Raw", "Echo");
    const DataBindFlowMQServicePlan *flow =
        &databind_opaque_service_flowmq_service_plan;
    const DataBindOpaquePlan *plan =
        databind_opaque_service_databind_opaque_plan();

    check_not_null(http);
    check_not_null(plan);
    if (http == NULL || plan == NULL) return;

    check_equal(http->method, "POST");
    check_equal(http->route, "/raw");
    check_equal(http->success_status, 200);
    check_equal(http->field_count, (size_t)0u);
    check_equal(http->error_count, (size_t)0u);
    check_equal(http->ingress_format, DATA_BIND_FORMAT_NONE);
    check_equal(http->egress_format, DATA_BIND_FORMAT_NONE);
    check_equal(http->ingress_payload_kind, DATA_BIND_PAYLOAD_OPAQUE);
    check_equal(http->egress_payload_kind, DATA_BIND_PAYLOAD_OPAQUE);
    check_true(http->ingress_opaque_plan == plan);
    check_true(http->egress_opaque_plan == plan);

    check_equal(
        flow->abi_version,
        (uint32_t)DATA_BIND_FLOWMQ_SERVICE_PLAN_ABI_VERSION);
    check_equal(flow->service_name, "OpaqueService.Raw");
    check_equal(flow->operation_name, "OpaqueService.Raw.Echo");
    check_equal(flow->request_type, "bytes");
    check_equal(flow->response_type, "bytes");
    check_equal(flow->ingress_format, DATA_BIND_FORMAT_NONE);
    check_equal(flow->egress_format, DATA_BIND_FORMAT_NONE);
    check_equal(flow->ingress_payload_kind, DATA_BIND_PAYLOAD_OPAQUE);
    check_equal(flow->egress_payload_kind, DATA_BIND_PAYLOAD_OPAQUE);
    check_null(flow->request_native_binding);
    check_null(flow->response_native_binding);
    check_true(flow->ingress_opaque_plan == plan);
    check_true(flow->egress_opaque_plan == plan);

    check_equal(plan->logical_type, "bytes");
    check_equal(plan->max_bytes, (size_t)64u);
    check_equal(
        plan->value_states, (uint32_t)DATA_BIND_OPAQUE_STATE_VALUE);

    check_equal(
        data_bind_opaque_plan_borrow(
            plan, DATA_BIND_OPAQUE_VALUE,
            payload, sizeof(payload), &span, &error),
        DATA_BIND_OK);
    check_true(span.data == payload);
    check_equal(span.bytes, sizeof(payload));

    check_equal(
        data_bind_create_from_text(
            schema, sizeof(schema) - 1u, &codec, &error),
        DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      check_equal(
          data_bind_http_method_plan_compile_service(
              codec, "Raw", "Echo", http, NULL,
              &method_plan, &diagnostic),
          DATA_BIND_ERR_SCHEMA);
      check_null(method_plan);
      check_contains(diagnostic.message, "pass-through");
    }
    data_bind_free(codec);
  }
}
