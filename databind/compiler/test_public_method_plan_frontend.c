#include "tinytest.h"

#include "public_calc.http.h"
#include "public_calc.rpc.h"
#include "configured_calc.http.h"
#include "configured_calc.rpc.h"

spec("DataBind public MethodPlan projection frontend") {
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
  }

  it("publishes convention RPC projection through databind_target") {
    const DataBindRpcProjectionConfig *config =
        data_bind_rpc_projection_artifact_find(
            &databind_public_calc_rpc_projection, "Calc", "Add");

    check_not_null(config);
    check_null(config->wire_method);
    check_equal(config->field_count, (size_t)0);
    check_equal(config->error_count, (size_t)0);
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
    check_equal(config->errors[0].error_type, "CalcError");
    check_equal(config->errors[0].code, -32042);
  }
}
