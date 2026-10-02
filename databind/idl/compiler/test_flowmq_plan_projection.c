#include "compiler_core.h"
#include "flowmq_plan_projection.h"
#include "tbe_contract_overlay.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef FLOWMQ_PLAN_SCHEMA
#error "FLOWMQ_PLAN_SCHEMA is required"
#endif

static int file_contains(const char *path, const char *needle) {
  salts_fs_buf_t buffer = {0};
  int found = 0;
  if (salts_fs_read_file(path, &buffer) == 0 && buffer.base != NULL)
    found = strstr(buffer.base, needle) != NULL;
  salts_fs_buf_free(&buffer);
  return found;
}

spec("DataBind generated FlowMQ ChannelPlan") {
  it("generates PUB/SUB and PUSH/PULL without FlowMQ runtime policy") {
    static const char output[] = "databind_flowmq_channel_plan_generated.h";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;
    char *schema_data = NULL;
    databind_compiler_flowmq_projection_config config = {
        "databind_device",
        "flow_native.h",
        "Device.Telemetry",
        DATA_BIND_FORMAT_BINARY,
        DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB,
        65536u};
    databind_compiler_projection_request request = {
        {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
         DATABIND_COMPILER_TRANSPORT_FLOWMQ},
        output,
        &config};
    databind_compiler_projection_backend backend =
        databind_compiler_flowmq_plan_backend();

    (void)salts_fs_unlink(output);
    check_equal(
        databind_compiler_parse_contract_file(
            FLOWMQ_PLAN_SCHEMA, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || contract == NULL || schema_data == NULL) {
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }
    tbe_error_init(&format_error);
    if (!databind_tbe_format_plan_build(
            contract, root, &format_plan, &format_error)) {
      check(false);
      databind_tbe_format_plan_destroy(&format_plan);
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }
    input = (databind_compiler_projection_input){
        .contract = contract,
        .tbe_format = &format_plan};

    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "Device.Telemetry"));
    check(file_contains(output, "TelemetryEvent"));
    check(file_contains(output, "DATA_BIND_FORMAT_BINARY"));
    check(file_contains(output, "DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB"));
    check(file_contains(output, "__databind_message_native_binding"));
    check_false(file_contains(output, "endpoint"));
    check_false(file_contains(output, "high_water"));
    check_false(file_contains(output, "reconnect"));
    check_false(file_contains(output, "session"));
    (void)salts_fs_unlink(output);

    config.format = DATA_BIND_FORMAT_JSON;
    config.pattern = DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "DATA_BIND_FORMAT_JSON"));
    check(file_contains(output, "DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL"));
    (void)salts_fs_unlink(output);

    config.format = DATA_BIND_FORMAT_XML;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.format = DATA_BIND_FORMAT_BINARY;
    config.channel_name = "Device.ChoiceEvents";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.native_header_include = NULL;
    config.channel_name = "Device.Raw";
    config.format = DATA_BIND_FORMAT_NONE;
    config.payload_kind = DATA_BIND_PAYLOAD_OPAQUE;
    config.opaque_max_bytes = 64u;
    config.pattern = DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB;
    config.max_payload_bytes = 128u;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "Device.Raw"));
    check(file_contains(output, "\"bytes\""));
    check(file_contains(output, "DATA_BIND_FORMAT_NONE"));
    check(file_contains(output, "DATA_BIND_PAYLOAD_OPAQUE"));
    check(file_contains(output, "DATA_BIND_OPAQUE_STATE_VALUE, 64u"));
    check(file_contains(output, "&databind_device_opaque_plan"));
    check_false(file_contains(output, "__databind_message_native_binding"));
    check_false(file_contains(output, "#include \"flow_native.h\""));
    (void)salts_fs_unlink(output);

    config.channel_name = "Device.Telemetry";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("generates Service REQ/REP and ROUTER/DEALER from one canonical operation") {
    static const char output[] = "databind_flowmq_service_plan_generated.h";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    databind_tbe_format_plan format_plan = {0};
    tbe_error_t format_error;
    char *schema_data = NULL;
    databind_compiler_flowmq_projection_config config = {
        .symbol_prefix = "databind_calc",
        .native_header_include = "flow_native.h",
        .max_payload_bytes = 65536u,
        .payload_kind = DATA_BIND_PAYLOAD_FORMAT,
        .service_name = "Calc",
        .operation_name = "Add",
        .ingress_format = DATA_BIND_FORMAT_JSON,
        .egress_format = DATA_BIND_FORMAT_BINARY,
        .service_pattern = DATA_BIND_FLOWMQ_SERVICE_ROUTER_DEALER};
    databind_compiler_projection_request request = {
        {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
         DATABIND_COMPILER_TRANSPORT_FLOWMQ},
        output,
        &config};
    databind_compiler_projection_backend backend =
        databind_compiler_flowmq_plan_backend();

    (void)salts_fs_unlink(output);
    check_equal(
        databind_compiler_parse_contract_file(
            FLOWMQ_PLAN_SCHEMA, &root, &contract, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || contract == NULL || schema_data == NULL) {
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }

    tbe_error_init(&format_error);
    if (!databind_tbe_format_plan_build(
            contract, root, &format_plan, &format_error)) {
      check(false);
      databind_tbe_format_plan_destroy(&format_plan);
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }
    input = (databind_compiler_projection_input){
        .contract = contract,
        .tbe_format = &format_plan};

    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "Device.Calc"));
    check(file_contains(output, "Device.Calc.Add"));
    check(file_contains(output, "AddRequest"));
    check(file_contains(output, "AddResponse"));
    check(file_contains(output, "DATA_BIND_FORMAT_JSON"));
    check(file_contains(output, "DATA_BIND_FORMAT_BINARY"));
    check(file_contains(output, "DATA_BIND_FLOWMQ_SERVICE_ROUTER_DEALER"));
    check_false(file_contains(
        output,
        "databind_calc_binary_AddResponse_databind_binary_provider"));
    check(file_contains(
        output,
        "databind_calc_flowmq_service_request__databind_message_native_binding"));
    check(file_contains(
        output,
        "databind_calc_flowmq_service_response__databind_message_native_binding"));
    check_false(file_contains(output, "CalcError"));
    check_false(file_contains(output, "endpoint"));
    check_false(file_contains(output, "routing_identity"));
    check_false(file_contains(output, "high_water"));
    check_false(file_contains(output, "reconnect"));
    check_false(file_contains(output, "session"));
    (void)salts_fs_unlink(output);

    config.service_pattern = DATA_BIND_FLOWMQ_SERVICE_REQ_REP;
    config.ingress_format = DATA_BIND_FORMAT_BINARY;
    config.egress_format = DATA_BIND_FORMAT_JSON;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "DATA_BIND_FLOWMQ_SERVICE_REQ_REP"));
    check(file_contains(output, "DATA_BIND_FORMAT_BINARY"));
    check(file_contains(output, "DATA_BIND_FORMAT_JSON"));
    check(file_contains(
        output,
        "databind_calc_binary_AddRequest_databind_binary_provider"));
    (void)salts_fs_unlink(output);

    config.egress_format = DATA_BIND_FORMAT_XML;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.egress_format = DATA_BIND_FORMAT_JSON;
    config.operation_name = "Missing";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.operation_name = "Add";
    config.service_pattern = (DataBindFlowMQServicePattern)99;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.service_pattern = DATA_BIND_FLOWMQ_SERVICE_REQ_REP;
    config.operation_name = "Choose";
    config.ingress_format = DATA_BIND_FORMAT_JSON;
    config.egress_format = DATA_BIND_FORMAT_BINARY;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    databind_tbe_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

}
