#include "tinytest.h"

#include "public_calc.http.h"
#include "public_calc.rpc.h"

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
}
