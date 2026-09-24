#include "tinytest.h"

#include "generated_http_projection.h"
#include "generated_rpc_projection.h"

spec("DataBind generated MethodPlan projection artifacts") {
  it("publishes compiled HTTP projection config") {
    const DataBindHttpProjectionConfig *config =
        data_bind_http_projection_artifact_find(
            &projection_fixture_http_projection, "Calc", "Add");

    check_not_null(config);
    check_equal(config->method, "GET");
    check_equal(config->route, "/add/{left}");
    check_equal(config->success_status, 201);
    check_equal(config->field_count, (size_t)3);
    check_equal(config->error_count, (size_t)1);
    check_equal(config->fields[0].schema_field, "left");
    check_true(config->fields[0].location == DATA_BIND_HTTP_PATH);
    check_equal(config->fields[1].wire_name, "X-Right");
    check_equal(config->errors[0].error_type, "CalcError");
    check_equal(config->errors[0].status, 422);
  }

  it("publishes compiled RPC projection config") {
    const DataBindRpcProjectionConfig *config =
        data_bind_rpc_projection_artifact_find(
            &projection_fixture_rpc_projection, "Calc", "Add");

    check_not_null(config);
    check_equal(config->wire_method, "calc.add");
    check_equal(config->field_count, (size_t)3);
    check_equal(config->fields[0].wire_name, "lhs");
    check_equal(config->fields[0].ordinal, (size_t)0);
    check_equal(config->fields[1].wire_name, "rhs");
    check_equal(config->fields[1].ordinal, (size_t)1);
    check_equal(config->errors[0].error_type, "CalcError");
    check_equal(config->errors[0].code, -32042);
  }

  it("does not expose an unknown operation") {
    check_null(data_bind_http_projection_artifact_find(
        &projection_fixture_http_projection, "Calc", "Missing"));
    check_null(data_bind_rpc_projection_artifact_find(
        &projection_fixture_rpc_projection, "Missing", "Add"));
  }
}
