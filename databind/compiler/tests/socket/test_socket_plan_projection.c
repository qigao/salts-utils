#include "compiler_core.h"
#include "socket_plan_projection.h"
#include "binary_contract_overlay.h"

#include "cmeta_fs.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef SOCKET_PLAN_SCHEMA
#error "SOCKET_PLAN_SCHEMA is required"
#endif

static int file_contains(const char *path, const char *needle) {
  cmeta_fs_buf_t buffer = {0};
  int found = 0;
  if (cmeta_fs_read_file(path, &buffer) == 0 && buffer.base != NULL)
    found = strstr(buffer.base, needle) != NULL;
  cmeta_fs_buf_free(&buffer);
  return found;
}

spec("DataBind generated SocketPlan") {
  it("generates bounded JSON/Binary plans and rejects unsupported shapes") {
    static const char output[] = "databind_socket_plan_generated.h";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    databind_binary_format_plan format_plan = {0};
    tbe_error_t format_error;
    char *schema_data = NULL;
    databind_compiler_socket_projection_config config = {
        .symbol_prefix = "databind_device",
        .native_header_include = "device.h",
        .channel_name = "Device.Telemetry",
        .format = DATA_BIND_FORMAT_BINARY,
        .mode = DATA_BIND_SOCKET_MODE_STREAM,
        .framing = DATA_BIND_SOCKET_FRAMING_LENGTH32_BE,
        .max_frame_bytes = 65536u};
    databind_compiler_projection_request request = {
        {DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT,
         DATABIND_COMPILER_TRANSPORT_SOCKET},
        output,
        &config};
    databind_compiler_projection_backend backend =
        databind_compiler_socket_plan_backend();

    (void)cmeta_fs_unlink(output);
    check_equal(
        databind_compiler_parse_contract_file(
            SOCKET_PLAN_SCHEMA, &root, &contract, &schema_data),
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
    if (!databind_binary_format_plan_build(
            contract, root, &format_plan, &format_error)) {
      check(false);
      databind_binary_format_plan_destroy(&format_plan);
      idl_contract_destroy(contract);
      node_free(root);
      free(schema_data);
      return;
    }
    input = (databind_compiler_projection_input){
        .contract = contract,
        .binary_format = &format_plan};

    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "Device.Telemetry"));
    check(file_contains(output, "TelemetryEvent"));
    check(file_contains(output, "DATA_BIND_FORMAT_BINARY"));
    check(file_contains(output, "DATA_BIND_SOCKET_MODE_STREAM"));
    check(file_contains(output, "DATA_BIND_SOCKET_FRAMING_LENGTH32_BE"));
    check(file_contains(output, "#include \"device.h\""));
    check(file_contains(output, "TelemetryEvent_cmeta_data(&data, error)"));
    check(file_contains(output, "offsetof(TelemetryEvent_t, _presence)"));
    check(file_contains(output, "offsetof(TelemetryEvent_t, _nulls)"));
    check(file_contains(
        output, "databind_device__databind_message_native_binding"));
    check_false(file_contains(output, "endpoint"));
    check_false(file_contains(output, "tls"));
    check_false(file_contains(output, "reconnect"));
    (void)cmeta_fs_unlink(output);

    config.format = DATA_BIND_FORMAT_JSON;
    config.mode = DATA_BIND_SOCKET_MODE_DATAGRAM;
    config.framing = DATA_BIND_SOCKET_FRAMING_NONE;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "DATA_BIND_FORMAT_JSON"));
    check(file_contains(output, "DATA_BIND_SOCKET_MODE_DATAGRAM"));
    check(file_contains(output, "DATA_BIND_SOCKET_FRAMING_NONE"));
    (void)cmeta_fs_unlink(output);

    config.format = DATA_BIND_FORMAT_XML;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(cmeta_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.format = DATA_BIND_FORMAT_BINARY;
    config.mode = DATA_BIND_SOCKET_MODE_STREAM;
    config.framing = DATA_BIND_SOCKET_FRAMING_NONE;
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(cmeta_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.framing = DATA_BIND_SOCKET_FRAMING_LENGTH32_BE;
    config.channel_name = "Device.ChoiceEvents";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(cmeta_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.channel_name = "Device.Telemetry";
    config.native_header_include = "../device.h";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(cmeta_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.native_header_include = NULL;
    config.channel_name = "Device.Raw";
    config.format = DATA_BIND_FORMAT_NONE;
    config.payload_kind = DATA_BIND_PAYLOAD_OPAQUE;
    config.opaque_max_bytes = 64u;
    config.mode = DATA_BIND_SOCKET_MODE_DATAGRAM;
    config.framing = DATA_BIND_SOCKET_FRAMING_NONE;
    config.max_frame_bytes = 128u;
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
    check_false(file_contains(output, "#include \"device.h\""));
    (void)cmeta_fs_unlink(output);

    config.channel_name = "Device.Telemetry";
    check_equal(
        databind_compiler_projection_run(
            &input, &request, 1u, &backend, 1u),
        -1);
    check(cmeta_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    databind_binary_format_plan_destroy(&format_plan);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }
}
