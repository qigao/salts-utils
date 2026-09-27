#include "compiler_core.h"
#include "flowmq_plan_projection.h"

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
        databind_compiler_flowmq_channel_plan_backend();

    (void)salts_fs_unlink(output);
    check_equal(
        tbe_compiler_parse_schema_file(
            FLOWMQ_PLAN_SCHEMA, &root, &schema_data),
        0);
    check_not_null(root);
    check_not_null(schema_data);
    if (root == NULL || schema_data == NULL) {
      node_free(root);
      free(schema_data);
      return;
    }

    check_equal(
        databind_compiler_projection_run(
            root, &request, 1u, &backend, 1u),
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
            root, &request, 1u, &backend, 1u),
        0);
    check(file_contains(output, "DATA_BIND_FORMAT_JSON"));
    check(file_contains(output, "DATA_BIND_FLOWMQ_CHANNEL_PUSH_PULL"));
    (void)salts_fs_unlink(output);

    config.format = DATA_BIND_FORMAT_XML;
    check_equal(
        databind_compiler_projection_run(
            root, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.format = DATA_BIND_FORMAT_BINARY;
    config.channel_name = "Device.ChoiceEvents";
    check_equal(
        databind_compiler_projection_run(
            root, &request, 1u, &backend, 1u),
        -1);
    check(salts_fs_access(output, SALTS_FS_ACCESS_EXISTS) != 0);

    node_free(root);
    free(schema_data);
  }
}
