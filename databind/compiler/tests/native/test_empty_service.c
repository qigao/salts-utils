#define TINYTEST_NO_MAIN
#include "tinytest.h"
#include "data_bind_method_plan.h"
#include "empty_service.http.h"
#include "empty_service.rpc.h"
#include "empty_service.service_native.h"
#include "empty_service_native.h"
#include <string.h>

extern unsigned int empty_service_calls;
extern unsigned int empty_service_cleared;

typedef DataBindStatus (*EmptyServiceBindingFn)(
    DataBindNativeTypeBinding *, DataBindNativeTypeBinding *,
    DataBindServiceNativeBinding *, DataBindError *);

static struct {
  DataBind *codec;
  DataBindHttpMethodPlan *http;
  DataBindRpcMethodPlan *rpc;
  DataBindNativeTypeBinding request;
  DataBindNativeTypeBinding response;
  DataBindServiceNativeBinding service;
} empty_service_fixture;

static void check_empty_service(
    const char *operation, EmptyServiceBindingFn binding,
    const DataBindNativeExecution *execution,
    void *request, void *response, void *outcome,
    size_t ingress, size_t egress) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindBindingPlanDiagnostic diagnostic = DATA_BIND_BINDING_PLAN_DIAGNOSTIC_INIT;
  const DataBindHttpProjectionConfig *http = data_bind_http_projection_artifact_find(
      &databind_empty_service_http_projection, "Probe", operation);
  const DataBindRpcProjectionConfig *rpc = data_bind_rpc_projection_artifact_find(
      &databind_empty_service_rpc_projection, "Probe", operation);
  void *params[] = {request, response, outcome};
  int status = -1;
  check_equal(binding(&empty_service_fixture.request, &empty_service_fixture.response,
                      &empty_service_fixture.service, &error), DATA_BIND_OK);
  check_equal(EmptyService_codec_create(&empty_service_fixture.codec, &error), DATA_BIND_OK);
  check_equal(data_bind_http_method_plan_compile_service(
      empty_service_fixture.codec, "Probe", operation, http, &empty_service_fixture.service,
      &empty_service_fixture.http, &diagnostic), DATA_BIND_OK);
  check_equal(data_bind_rpc_method_plan_compile_service(
      empty_service_fixture.codec, "Probe", operation, rpc, &empty_service_fixture.service,
      &empty_service_fixture.rpc, &diagnostic), DATA_BIND_OK);
  check_equal(data_bind_binding_plan_ingress_count(
      data_bind_rpc_method_plan_binding(empty_service_fixture.rpc)), ingress);
  check_equal(data_bind_binding_plan_egress_count(
      data_bind_rpc_method_plan_binding(empty_service_fixture.rpc)), egress);
  check_true(data_bind_native_execution_valid(execution));
  check_true(execution->invoke(execution->context, &status, params,
                             empty_service_fixture.service.function->param_count));
  check_equal(status, 0);
  check_equal(empty_service_calls, 1u);
}

spec("DataBind empty Service messages") {
  before_each() {
    empty_service_calls = 0u;
    empty_service_cleared = 0u;
  }

  after_each() {
    data_bind_rpc_method_plan_free(empty_service_fixture.rpc);
    data_bind_http_method_plan_free(empty_service_fixture.http);
    data_bind_free(empty_service_fixture.codec);
    memset(&empty_service_fixture, 0, sizeof(empty_service_fixture));
  }

  it("binds empty request and response roots with typed errors") {
    Empty_t request = {0}, response = {0};
    databind_12_EmptyService_5_Probe_4_Ping__error outcome =
        databind_12_EmptyService_5_Probe_4_Ping__ERROR_INIT;
    check_equal(sizeof(request), (size_t)1u);
    check_empty_service("Ping", databind_12_EmptyService_5_Probe_4_Ping__databind_native_binding,
        databind_12_EmptyService_5_Probe_4_Ping__databind_execution(),
        &request, &response, &outcome, 0u, 0u);
    check_equal(outcome.kind, 0u);
  }

  it("binds an empty request to a populated status response") {
    Empty_t request = {0};
    Status_t response = {0};
    check_empty_service("Status", databind_12_EmptyService_5_Probe_6_Status__databind_native_binding,
        databind_12_EmptyService_5_Probe_6_Status__databind_execution(),
        &request, &response, NULL, 0u, 1u);
    check_equal(response.value, UINT32_C(42));
  }

  it("binds a populated request to an empty response") {
    Status_t request = {17u};
    Empty_t response = {0};
    check_empty_service("Clear", databind_12_EmptyService_5_Probe_5_Clear__databind_native_binding,
        databind_12_EmptyService_5_Probe_5_Clear__databind_execution(),
        &request, &response, NULL, 1u, 0u);
    check_equal(empty_service_cleared, 17u);
  }
}
