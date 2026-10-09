#include "projection_frontend.h"

#include "cmeta_fs.h"
#include "tinytest.h"

#include <string.h>

static const char *path_base(const char *path, char out[SALTS_FS_MAX_PATH]) {
  if (cmeta_fs_path_basename(path, out, SALTS_FS_MAX_PATH) != 0)
    return NULL;
  return out;
}

spec("DataBind public typed generation frontend") {
  it("lowers one Plugin selection into the canonical projection registry") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "plugin",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image_processor",
        .artifact_version = "1.2.3",
        .output_path = "generated/image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
    check_equal(plan.backend_count, (size_t)1u);
    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_PLUGIN);
    check_true(plan.requests[0].config == &plan.plugin);
    check_equal(plan.backends[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT);
    check_equal(plan.backends[0].id.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_PLUGIN);
    check_equal(plan.backends[0].name, "plugin");

    check_equal(plan.plugin.plugin_version_major, 1u);
    check_equal(plan.plugin.plugin_version_minor, 2u);
    check_equal(plan.plugin.plugin_version_patch, 3u);
    check_equal(plan.plugin.component_id, "Image.ImageProcessor");
    check_equal(plan.plugin.native_header, "image_native.h");

    check_equal(path_base(plan.plugin_source, base),
                "image_processor.plugin.c");
    check_equal(path_base(plan.plugin_service_header, base),
                "image_processor.plugin.h");
    check_equal(path_base(plan.plugin_client_header, base),
                "image_processor.plugin_client.h");
    check_equal(path_base(plan.plugin_client_source, base),
                "image_processor.plugin_client.c");
    check_true(plan.plugin.service_header_output ==
               plan.plugin_service_header);
    check_true(plan.plugin.client_header_output ==
               plan.plugin_client_header);
    check_true(plan.plugin.client_source_output ==
               plan.plugin_client_source);
    /* The manifest also includes all three secondary Plugin outputs. */
    check_equal(plan.output_count, (size_t)5u);
    check_equal(plan.outputs[0].path, input.output_path);
    check_equal(plan.outputs[1].path, plan.plugin_source);
    check_equal(plan.outputs[2].path, plan.plugin_service_header);
    check_equal(plan.outputs[3].path, plan.plugin_client_header);
    check_equal(plan.outputs[4].path, plan.plugin_client_source);
    check_equal(plan.outputs[4].owner.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT);
    check_equal(plan.outputs[4].owner.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_PLUGIN);
  }

  it("lowers one transport-neutral NATIVE Service artifact") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "native",
        .artifact_name = "calc",
        .output_path = "generated/calc_native.h",
        .source_output_path = "generated/calc_native.c",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
    check_equal(plan.backend_count, (size_t)1u);
    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_NATIVE);
    check_true(plan.requests[0].config == &plan.native_service);
    check_equal(plan.backends[0].name, "native");
    check_equal(plan.backends[0].output_policy,
                DATABIND_COMPILER_OUTPUT_SELF_PUBLISHED);
    check_equal(path_base(plan.requests[0].output, base),
                "calc.service_native.c");
    check_equal(path_base(plan.native_service_header, base),
                "calc.service_native.h");
    check_equal(plan.native_service.native_header, "calc_native.h");
    check_true(plan.native_service.header_output ==
               plan.native_service_header);
    check_equal(plan.output_count, (size_t)4u);
    check_equal(plan.outputs[0].path, input.output_path);
    check_equal(plan.outputs[1].path, input.source_output_path);
    check_equal(plan.outputs[2].path, plan.requests[0].output);
    check_equal(plan.outputs[3].path, plan.native_service_header);

    databind_compiler_projection_frontend_dispose(&plan);

    /* Native Service is a typed artifact: no Binary serde companion needed. */
    input.source_output_path = NULL;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.output_count, (size_t)3u);
    check_equal(plan.outputs[0].path, input.output_path);
    check_equal(plan.outputs[1].path, plan.native_service_source);
    check_equal(plan.outputs[2].path, plan.native_service_header);
    databind_compiler_projection_frontend_dispose(&plan);
  }

  it("lowers OpenAPI as an artifact over the shared HTTP projection") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "openapi",
        .transports = "http",
        .artifact_name = "users",
        .output_path = "generated/users_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)2u);
    check_equal(plan.backend_count, (size_t)2u);

    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_OPENAPI);
    check_true(plan.requests[0].config == &plan.openapi);
    check_true(plan.openapi.http == &plan.http);
    check_equal(plan.backends[0].name, "openapi");
    check_equal(plan.backends[0].output_policy,
                DATABIND_COMPILER_OUTPUT_STAGED_SINGLE);
    check_equal(path_base(plan.requests[0].output, base),
                "users.openapi.json");

    check_equal(plan.requests[1].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.requests[1].id.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_HTTP);
    check_true(plan.requests[1].config == &plan.http);
    check_equal(plan.backends[1].name, "http");
    check_equal(path_base(plan.requests[1].output, base),
                "users.http.h");
    check_equal(plan.backends[1].output_policy,
                DATABIND_COMPILER_OUTPUT_STAGED_SINGLE);
    check_equal(plan.output_count, (size_t)3u);
    check_equal(plan.outputs[0].path, input.output_path);
    check_equal(plan.outputs[1].path, plan.openapi_output);
    check_equal(plan.outputs[2].path, plan.http_projection_header);
  }

  it("rejects OpenAPI without the HTTP transport authority") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "openapi",
        .artifact_name = "users",
        .output_path = "generated/users_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "HTTP"));
  }

  it("lowers convention HTTP and RPC selections without Plugin inputs") {
    databind_compiler_projection_frontend_input input = {
        .transports = "http,rpc",
        .artifact_name = "calc",
        .output_path = "generated/calc_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)2u);
    check_equal(plan.backend_count, (size_t)2u);

    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_HTTP);
    check_true(plan.requests[0].config == &plan.http);
    check_equal(plan.http.symbol_prefix, "databind_calc");
    check_equal(plan.backends[0].name, "http");
    check_equal(path_base(plan.requests[0].output, base), "calc.http.h");

    check_equal(plan.requests[1].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.requests[1].id.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_RPC);
    check_true(plan.requests[1].config == &plan.rpc);
    check_equal(plan.rpc.symbol_prefix, "databind_calc");
    check_equal(plan.backends[1].name, "rpc");
    check_equal(path_base(plan.requests[1].output, base), "calc.rpc.h");
  }

  it("loads one external HTTP/RPC config into the selected MethodPlans") {
    databind_compiler_projection_frontend_input input = {
        .transports = "http,rpc",
        .artifact_name = "calc",
        .projection_config_path = DATABIND_PROJECTION_CONFIG_FILE,
        .output_path = "generated/calc_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_true(plan.external_config.has_http);
    check_true(plan.external_config.has_rpc);
    check_equal(plan.http.operation_count, (size_t)1u);
    check_equal(plan.http.operations[0].service_name, "Calc");
    check_equal(plan.http.operations[0].operation_name, "Add");
    check_equal(plan.http.operations[0].method, "GET");
    check_equal(plan.http.operations[0].route, "/add/{left}");
    check_equal(plan.http.operations[0].success_status, 201);
    check_equal(plan.http.operations[0].context_flags, UINT64_C(4));
    check_equal(plan.http.operations[0].ingress_format,
                DATA_BIND_FORMAT_YAML);
    check_equal(plan.http.operations[0].egress_format,
                DATA_BIND_FORMAT_XML);
    check_equal(plan.http.field_count, (size_t)3u);
    check_equal(plan.http.errors[0].error_type, "CalcError");
    check_equal(plan.http.errors[0].status, 422);

    check_equal(plan.rpc.operation_count, (size_t)1u);
    check_equal(plan.rpc.operations[0].wire_method, "calc.add");
    check_equal(plan.rpc.operations[0].ingress_format,
                DATA_BIND_FORMAT_JSON);
    check_equal(plan.rpc.operations[0].egress_format,
                DATA_BIND_FORMAT_BINARY);
    check_equal(plan.rpc.field_count, (size_t)3u);
    check_equal(plan.rpc.fields[0].wire_name, "lhs");
    check_equal(plan.rpc.fields[0].ordinal, (size_t)0u);
    check_equal(plan.rpc.errors[0].error_type, "CalcError");
    check_equal(plan.rpc.errors[0].code, -32042);

    databind_compiler_projection_frontend_dispose(&plan);
  }

  it("lowers one explicit Socket transport through the shared frontend") {
    databind_compiler_projection_frontend_input input = {
        .transports = "socket",
        .artifact_name = "device",
        .projection_config_path = DATABIND_SOCKET_PROJECTION_CONFIG_FILE,
        .output_path = "generated/device_native.h",
        .source_output_path = "generated/device_native.c",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
    check_equal(plan.backend_count, (size_t)1u);
    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_SOCKET);
    check_true(plan.requests[0].config == &plan.socket);
    check_equal(plan.backends[0].name, "socket");
    check_equal(path_base(plan.requests[0].output, base),
                "device.socket.h");

    check_true(plan.external_config.has_socket);
    check_equal(plan.socket.symbol_prefix, "databind_device");
    check_equal(plan.socket.native_header_include, "device_native.h");
    check_equal(plan.socket.channel_name, "Device.Telemetry");
    check_equal(plan.socket.format, DATA_BIND_FORMAT_BINARY);
    check_equal(plan.socket.mode, DATA_BIND_SOCKET_MODE_STREAM);
    check_equal(plan.socket.framing,
                DATA_BIND_SOCKET_FRAMING_LENGTH32_BE);
    check_equal(plan.socket.max_frame_bytes, (size_t)65536u);

    databind_compiler_projection_frontend_dispose(&plan);

    input.source_output_path = NULL;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "source-output"));
  }

  it("lowers one explicit FlowMQ Channel transport through the shared frontend") {
    databind_compiler_projection_frontend_input input = {
        .transports = "flowmq",
        .artifact_name = "device",
        .projection_config_path = DATABIND_FLOWMQ_PROJECTION_CONFIG_FILE,
        .output_path = "generated/device_native.h",
        .source_output_path = "generated/device_native.c",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    char base[SALTS_FS_MAX_PATH];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
    check_equal(plan.backend_count, (size_t)1u);
    check_equal(plan.requests[0].id.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.requests[0].id.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_FLOWMQ);
    check_true(plan.requests[0].config == &plan.flowmq);
    check_equal(plan.backends[0].name, "flowmq");
    check_equal(path_base(plan.requests[0].output, base),
                "device.flowmq.h");

    check_true(plan.external_config.has_flowmq);
    check_equal(plan.flowmq.symbol_prefix, "databind_device");
    check_equal(plan.flowmq.native_header_include, "device_native.h");
    check_equal(plan.flowmq.channel_name, "Device.Telemetry");
    check_equal(plan.flowmq.format, DATA_BIND_FORMAT_BINARY);
    check_equal(plan.flowmq.pattern, DATA_BIND_FLOWMQ_CHANNEL_PUB_SUB);
    check_equal(plan.flowmq.max_payload_bytes, (size_t)65536u);

    databind_compiler_projection_frontend_dispose(&plan);

    input.source_output_path = NULL;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "source-output"));
  }

  it("rejects config sections for an unselected transport") {
    databind_compiler_projection_frontend_input input = {
        .transports = "http",
        .artifact_name = "calc",
        .projection_config_path = DATABIND_PROJECTION_CONFIG_FILE,
        .output_path = "generated/calc_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "rpc"));
  }

  it("rejects unknown JSON projection config keys") {
    databind_compiler_projection_frontend_input input = {
        .transports = "http",
        .artifact_name = "calc",
        .projection_config_path = DATABIND_PROJECTION_CONFIG_INVALID_FILE,
        .output_path = "generated/calc_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "Unknown projection config key"));
  }

  it("composes Plugin and MethodPlan projections in one frontend invocation") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "plugin",
        .transports = "http,rpc",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.2.3",
        .output_path = "generated/image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)3u);
    check_equal(plan.backend_count, (size_t)3u);
    check(strcmp(plan.requests[0].output, plan.requests[1].output) != 0);
    check(strcmp(plan.requests[1].output, plan.requests[2].output) != 0);
  }

  it("parses whitespace around artifact names") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "  plugin  ",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "0.0.1",
        .output_path = "image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
  }

  it("rejects duplicate, incomplete, unknown and unavailable projections") {
    databind_compiler_projection_frontend_input input = {
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.0.0",
        .output_path = "image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    input.artifacts = "plugin,plugin";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "more than once"));

    input.artifacts = "plugin,";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "comma"));

    input.artifacts = "unknown";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "Unknown artifact"));

    input.artifacts = "wasm";
    input.wasm_core_module_path = NULL;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "--wasm-core-module"));

    input.wasm_core_module_path = __FILE__;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)1u);
    check_equal(plan.backend_count, (size_t)1u);
    check_equal(
        plan.requests[0].id.kind,
        (uint32_t)DATABIND_COMPILER_ARTIFACT_WASM);
    check_not_null(strstr(plan.requests[0].output, ".wasm"));

    input.wasm_core_module_path = NULL;
    input.artifacts = NULL;
    input.transports = "socket";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "socket section"));
  }

  it("requires safe artifact identity and explicit Plugin inputs") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "plugin",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.0.0",
        .output_path = "image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    input.component_id = NULL;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "--component"));

    input.component_id = "Image.ImageProcessor";
    input.artifact_name = "../image";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "--artifact-name"));

    input.artifact_name = "image";
    input.artifact_version = "1.2";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "--artifact-version"));

    input.artifact_version = "4294967296.0.0";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
  }

  it("rejects deterministic artifact output collisions") {
    char collision[SALTS_FS_MAX_PATH];
    databind_compiler_projection_frontend_input input = {
        .artifacts = "plugin",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.0.0",
        .output_path = "generated/image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(cmeta_fs_path_join(
                    collision, sizeof(collision),
                    "generated", "image.plugin.c"),
                0);
    input.source_output_path = collision;

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "collide"));
  }

  it("enumerates all mixed primary and secondary output paths for transaction planning") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "native,plugin,wasm,openapi",
        .transports = "http",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.2.3",
        .wasm_core_module_path = __FILE__,
        .output_path = "generated/image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    size_t i;
    size_t j;
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)), 0);
    check_equal(plan.request_count, (size_t)5u);
    check_equal(plan.output_count, (size_t)13u);

    /* Built-in outputs and every request/secondary publication destination. */
    check_equal(plan.outputs[0].path, input.output_path);
    check_equal(plan.outputs[1].path, plan.native_service_source);
    check_equal(plan.outputs[2].path, plan.native_service_header);
    check_equal(plan.outputs[3].path, plan.plugin_source);
    check_equal(plan.outputs[4].path, plan.plugin_service_header);
    check_equal(plan.outputs[5].path, plan.plugin_client_header);
    check_equal(plan.outputs[6].path, plan.plugin_client_source);
    check_equal(plan.outputs[7].path, plan.wasm_component_output);
    check_equal(plan.outputs[8].path, plan.wasm_host_header);
    check_equal(plan.outputs[9].path, plan.wasm_host_source);
    check_equal(plan.outputs[10].path, plan.wasm_guest_header);
    check_equal(plan.outputs[11].path, plan.openapi_output);
    check_equal(plan.outputs[12].path, plan.http_projection_header);
    check_equal(plan.outputs[0].owner.axis, 0);
    check_equal(plan.outputs[1].owner.axis, 0);
    check_equal(plan.outputs[10].owner.kind,
                (uint32_t)DATABIND_COMPILER_ARTIFACT_WASM);
    check_equal(plan.outputs[12].owner.axis,
                DATABIND_COMPILER_PROJECTION_AXIS_TRANSPORT);
    check_equal(plan.outputs[12].owner.kind,
                (uint32_t)DATABIND_COMPILER_TRANSPORT_HTTP);

    for (i = 0u; i < plan.output_count; ++i) {
      check_not_null(plan.outputs[i].path);
      for (j = 0u; j < i; ++j)
        check_true(strcmp(plan.outputs[i].path, plan.outputs[j].path) != 0);
    }
    databind_compiler_projection_frontend_dispose(&plan);
  }

  it("rejects secondary artifact collisions without leaving a partial manifest") {
    databind_compiler_projection_frontend_input input = {
        .artifacts = "plugin",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.2.3",
        .output_path = "generated/image_native.h",
        .guest_output_path = "generated/image.plugin_client.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)), -1);
    check_not_null(strstr(error, "collide"));
    check_equal(plan.output_count, (size_t)0u);

#ifdef _WIN32
    /* Windows cmeta_fs_path_join uses backslash while the caller may use
     * forward slash; both must name the same output and be rejected.
     * Filename comparisons on Windows are ordinarily case-insensitive. */
    input.guest_output_path = "generated\\image.plugin_client.h";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)), -1);
    check_not_null(strstr(error, "collide"));
    check_equal(plan.output_count, (size_t)0u);

    input.guest_output_path = "GENERATED/IMAGE.PLUGIN_CLIENT.H";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)), -1);
    check_not_null(strstr(error, "collide"));
    check_equal(plan.output_count, (size_t)0u);
#else
    /* POSIX considers backslash part of a filename, not a separator. */
    input.guest_output_path = "generated\\image.plugin_client.h";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)), 0);
    check_equal(plan.output_count, (size_t)6u);
    databind_compiler_projection_frontend_dispose(&plan);
#endif
  }

  it("keeps an empty projection set inert") {
    databind_compiler_projection_frontend_input input = {0};
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                0);
    check_equal(plan.request_count, (size_t)0u);
    check_equal(plan.backend_count, (size_t)0u);
  }
}
