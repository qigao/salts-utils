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
    check_true(plan.plugin.service_header_output ==
               plan.plugin_service_header);
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
    check_null(plan.requests[0].config);
    check_equal(plan.backends[0].name, "http");
    check_equal(path_base(plan.requests[0].output, base), "calc.http.h");

    check_equal(plan.requests[1].kind,
                DATABIND_COMPILER_PROJECTION_RPC);
    check_null(plan.requests[1].config);
    check_equal(plan.backends[1].name, "rpc");
    check_equal(path_base(plan.requests[1].output, base), "calc.rpc.h");
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
    check_true(plan.requests[0].output != plan.requests[1].output);
    check_true(plan.requests[1].output != plan.requests[2].output);
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
