#include "data_bind_projection_plan.h"
#include "tinytest.h"

#include <string.h>

static DataBind *projection_plan_codec(void) {
  static const char schema[] =
      "schema ProjectionPlan [version(1)];"
      "message Request {"
      " optional uint32 optional_id;"
      " nullable uint32 nullable_id;"
      " optional nullable uint32 tri_id;"
      "}"
      "message Response {"
      " uint32 value;"
      "}"
      "service Store {"
      " Read: Request -> Response;"
      "}";
  DataBind *codec = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  return data_bind_create_from_text(
             schema, sizeof(schema) - 1u, &codec, &error) == DATA_BIND_OK
             ? codec
             : NULL;
}

spec("DataBind FormatPlan and TransportPlan") {
  it("freezes format state-space facts without runtime schema lookup") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *json = NULL;
    DataBindFormatPlanInfo info = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_JSON, &json, &error),
        DATA_BIND_OK);
    check_not_null(json);
    check(data_bind_format_plan_info(json, &info));
    check_equal(info.abi_version,
                (uint32_t)DATA_BIND_PROJECTION_PLAN_ABI_VERSION);
    check_equal(info.type_name, "Request");
    check_equal(info.format, DATA_BIND_FORMAT_JSON);
    check_true(info.has_optional);
    check_true(info.has_nullable);
    check((info.value_states & DATA_BIND_FORMAT_STATE_VALUE) != 0u);
    check((info.value_states & DATA_BIND_FORMAT_STATE_ABSENT) != 0u);
    check((info.value_states & DATA_BIND_FORMAT_STATE_NULL) != 0u);

    data_bind_free(codec);
    codec = NULL;

    /* The plan copied its execution facts and type identity. */
    info = (DataBindFormatPlanInfo)DATA_BIND_FORMAT_PLAN_INFO_INIT;
    check(data_bind_format_plan_info(json, &info));
    check_equal(info.type_name, "Request");
    check_true(info.has_nullable);

    data_bind_format_plan_free(json);
  }

  it("fails closed when the selected format cannot preserve explicit NULL") {
    DataBind *codec = projection_plan_codec();
    DataBindFormatPlan *plan = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_CSV, &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);
    check_contains(error.message, "NULL");

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_XML, &plan, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(plan);

    check_equal(
        data_bind_format_plan_compile(
            codec, "Request", DATA_BIND_FORMAT_YAML, &plan, &error),
        DATA_BIND_OK);
    data_bind_format_plan_free(plan);
    data_bind_free(codec);
  }

  it("composes independent ingress and egress FormatPlans into one transport") {
    DataBind *codec = projection_plan_codec();
    DataBindTransportPlan *transport = NULL;
    DataBindTransportPlanInfo info = DATA_BIND_TRANSPORT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo ingress = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindFormatPlanInfo egress = DATA_BIND_FORMAT_PLAN_INFO_INIT;
    DataBindError error = DATA_BIND_ERROR_INIT;

    check_not_null(codec);
    if (!codec) return;

    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "Store", "Read", DATA_BIND_TRANSPORT_HTTP,
            DATA_BIND_FORMAT_JSON, DATA_BIND_FORMAT_XML,
            &transport, &error),
        DATA_BIND_OK);
    check_not_null(transport);
    check(data_bind_transport_plan_info(transport, &info));
    check_equal(info.kind, DATA_BIND_TRANSPORT_HTTP);
    check_equal(info.service_name, "Store");
    check_equal(info.operation_name, "Read");
    check_not_null(info.ingress);
    check_not_null(info.egress);
    check(data_bind_format_plan_info(info.ingress, &ingress));
    check(data_bind_format_plan_info(info.egress, &egress));
    check_equal(ingress.type_name, "Request");
    check_equal(ingress.format, DATA_BIND_FORMAT_JSON);
    check_equal(egress.type_name, "Response");
    check_equal(egress.format, DATA_BIND_FORMAT_XML);

    data_bind_transport_plan_free(transport);

    transport = NULL;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    check_equal(
        data_bind_transport_plan_compile_service(
            codec, "Store", "Read", DATA_BIND_TRANSPORT_RPC,
            DATA_BIND_FORMAT_CSV, DATA_BIND_FORMAT_JSON,
            &transport, &error),
        DATA_BIND_ERR_SCHEMA);
    check_null(transport);

    data_bind_free(codec);
  }
}
