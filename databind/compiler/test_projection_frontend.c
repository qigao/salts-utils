#include "projection_frontend.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <string.h>

static const char *path_base(const char *path, char out[SALTS_FS_MAX_PATH]) {
  if (salts_fs_path_basename(path, out, SALTS_FS_MAX_PATH) != 0)
    return NULL;
  return out;
}

spec("DataBind public projection frontend") {
  it("lowers one Plugin selection into the canonical projection registry") {
    databind_compiler_projection_frontend_input input = {
        .projections = "plugin",
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
    check_equal(plan.requests[0].kind,
                DATABIND_COMPILER_PROJECTION_PLUGIN);
    check_true(plan.requests[0].config == &plan.plugin);
    check_equal(plan.backends[0].kind,
                DATABIND_COMPILER_PROJECTION_PLUGIN);
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
  }

  it("lowers convention HTTP and RPC selections without Plugin inputs") {
    databind_compiler_projection_frontend_input input = {
        .projections = "http,rpc",
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

    check_equal(plan.requests[0].kind,
                DATABIND_COMPILER_PROJECTION_HTTP);
    check_true(plan.requests[0].config == &plan.http);
    check_equal(plan.http.symbol_prefix, "databind_calc");
    check_equal(plan.backends[0].name, "http");
    check_equal(path_base(plan.requests[0].output, base), "calc.http.h");

    check_equal(plan.requests[1].kind,
                DATABIND_COMPILER_PROJECTION_RPC);
    check_true(plan.requests[1].config == &plan.rpc);
    check_equal(plan.rpc.symbol_prefix, "databind_calc");
    check_equal(plan.backends[1].name, "rpc");
    check_equal(path_base(plan.requests[1].output, base), "calc.rpc.h");
  }

  it("loads one external HTTP/RPC config into the selected MethodPlans") {
    databind_compiler_projection_frontend_input input = {
        .projections = "http,rpc",
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

  it("rejects config sections for an unselected transport") {
    databind_compiler_projection_frontend_input input = {
        .projections = "http",
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
        .projections = "http",
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
        .projections = "plugin,http,rpc",
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

  it("parses whitespace around projection names") {
    databind_compiler_projection_frontend_input input = {
        .projections = "  plugin  ",
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

    input.projections = "plugin,plugin";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "more than once"));

    input.projections = "plugin,";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "comma"));

    input.projections = "unknown";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "Unknown projection"));

    input.projections = "wasm";
    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "not available"));
  }

  it("requires safe artifact identity and explicit Plugin inputs") {
    databind_compiler_projection_frontend_input input = {
        .projections = "plugin",
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
        .projections = "plugin",
        .component_id = "Image.ImageProcessor",
        .artifact_name = "image",
        .artifact_version = "1.0.0",
        .output_path = "generated/image_native.h",
    };
    databind_compiler_projection_frontend_plan plan;
    char error[256];

    check_equal(salts_fs_path_join(
                    collision, sizeof(collision),
                    "generated", "image.plugin.c"),
                0);
    input.source_output_path = collision;

    check_equal(databind_compiler_projection_frontend_build(
                    &input, &plan, error, sizeof(error)),
                -1);
    check_not_null(strstr(error, "collide"));
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
