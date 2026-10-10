#include "tinytest.h"

#include "public_calc.http.h"
#include "public_calc.rpc.h"
#include "configured_calc.http.h"
#include "configured_calc.rpc.h"
#include "schema_app.http.h"
#include "schema_app.rpc.h"

spec("DataBind public MethodPlan projection frontend") {
  it("retains ordered policy declarations in HTTP metadata without changing v1 projection JSON") {
    check_equal(databind_schema_app_http_policy_at("Users", "Get", 0u), "no_store");
    check_equal(databind_schema_app_http_policy_at("Users", "Get", 1u), "auth");
    check_null(databind_schema_app_http_policy_at("Users", "Get", 2u));
    check_null(databind_schema_app_http_policy_at("Users", "Create", 0u));
    check_null(databind_schema_app_http_policy_at(NULL, "Get", 0u));
  }
  it("compiles multi-service projections generated entirely from schema annotations") {
    const DataBindHttpProjectionConfig *get = data_bind_http_projection_artifact_find(
        &databind_schema_app_http_projection, "Users", "Get");
    const DataBindHttpProjectionConfig *create = data_bind_http_projection_artifact_find(
        &databind_schema_app_http_projection, "Users", "Create");
    const DataBindRpcProjectionConfig *health = data_bind_rpc_projection_artifact_find(
        &databind_schema_app_rpc_projection, "System", "Check");
    check_not_null(get);
    check_equal(get->route, "/users/{id}");
    check_equal(get->method, "GET");
    check_equal(get->fields[0].location, DATA_BIND_HTTP_PATH);
    check_not_null(create);
    check_equal(create->route, "/users");
    check_equal(create->success_status, 201);
    check_equal(create->egress_format, DATA_BIND_FORMAT_XML);
    check_not_null(health);
    check_equal(health->wire_method, "system.health");
  }
  it("publishes convention HTTP projection through databind_target") {
    const DataBindHttpProjectionConfig *config =
        data_bind_http_projection_artifact_find(
            &databind_public_calc_http_projection, "Calc", "Add");

    check_not_null(config);
    check_null(config->method);
    check_null(config->route);
    check_equal(config->success_status, 200);
    check_equal(config->context_flags, UINT64_C(0));
    check_equal(config->field_count, (size_t)0);
    check_equal(config->error_count, (size_t)0);
    check_equal(config->ingress_format, DATA_BIND_FORMAT_JSON);
    check_equal(config->egress_format, DATA_BIND_FORMAT_JSON);
  }

  it("publishes convention RPC projection through databind_target") {
    const DataBindRpcProjectionConfig *config =
        data_bind_rpc_projection_artifact_find(
            &databind_public_calc_rpc_projection, "Calc", "Add");

    check_not_null(config);
    check_null(config->wire_method);
    check_equal(config->field_count, (size_t)0);
    check_equal(config->error_count, (size_t)0);
    check_equal(config->ingress_format, DATA_BIND_FORMAT_JSON);
    check_equal(config->egress_format, DATA_BIND_FORMAT_JSON);
  }

  it("applies external HTTP projection config without changing IDL") {
    const DataBindHttpProjectionConfig *config =
        data_bind_http_projection_artifact_find(
            &databind_configured_calc_http_projection, "Calc", "Add");

    check_not_null(config);
    check_equal(config->method, "GET");
    check_equal(config->route, "/add/{left}");
    check_equal(config->success_status, 201);
    check_equal(config->context_flags, UINT64_C(4));
    check_equal(config->field_count, (size_t)3);
    check_equal(config->fields[0].schema_field, "left");
    check_true(config->fields[0].location == DATA_BIND_HTTP_PATH);
    check_equal(config->fields[1].wire_name, "X-Right");
    check_equal(config->fields[2].schema_field, "sum");
    check_true(config->fields[2].location == DATA_BIND_HTTP_RESPONSE_BODY);
    check_equal(config->error_count, (size_t)1);
    check_equal(config->ingress_format, DATA_BIND_FORMAT_YAML);
    check_equal(config->egress_format, DATA_BIND_FORMAT_XML);
    check_equal(config->errors[0].error_type, "CalcError");
    check_equal(config->errors[0].status, 422);
  }

  it("applies external RPC projection config without changing IDL") {
    const DataBindRpcProjectionConfig *config =
        data_bind_rpc_projection_artifact_find(
            &databind_configured_calc_rpc_projection, "Calc", "Add");

    check_not_null(config);
    check_equal(config->wire_method, "calc.add");
    check_equal(config->field_count, (size_t)3);
    check_equal(config->fields[0].wire_name, "lhs");
    check_equal(config->fields[0].ordinal, (size_t)0);
    check_equal(config->fields[1].wire_name, "rhs");
    check_equal(config->fields[1].ordinal, (size_t)1);
    check_equal(config->fields[2].wire_name, "result");
    check_equal(config->fields[2].ordinal, (size_t)0);
    check_equal(config->error_count, (size_t)1);
    check_equal(config->ingress_format, DATA_BIND_FORMAT_JSON);
    check_equal(config->egress_format, DATA_BIND_FORMAT_BINARY);
    check_equal(config->errors[0].error_type, "CalcError");
    check_equal(config->errors[0].code, -32042);
  }
}
