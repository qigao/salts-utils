#include "compiler_core.h"
#include "plugin_projection.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef PLUGIN_TYPED_ERROR_SCHEMA
#error "PLUGIN_TYPED_ERROR_SCHEMA is required"
#endif
#ifndef PLUGIN_MULTI_SERVICE_SCHEMA
#error "PLUGIN_MULTI_SERVICE_SCHEMA is required"
#endif

spec("DataBind Plugin projection semantic rejection") {
  it("rejects typed Service errors before creating outputs") {
    static const char source_output[] =
        "databind_plugin_typed_error_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_typed_error_should_not_exist.h";
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .native_header = "error_native.h",
        .service_header_output = header_output,
    };
    databind_compiler_projection_request request = {
        .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_TYPED_ERROR_SCHEMA, &root, &schema_data),
                0);
    check_not_null(root);
    check_not_null(schema_data);

    check_equal(databind_compiler_projection_run(
                    root, &request, 1u, &backend, 1u),
                -1);

    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);

    node_free(root);
    free(schema_data);
  }
  it("publishes multiple Services as independent contracts") {
    static const char source_output[] =
        "databind_plugin_multi_service.c";
    static const char header_output[] =
        "databind_plugin_multi_service.h";
    Node *root = NULL;
    char *schema_data = NULL;
    salts_fs_buf_t generated = {0};
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .native_header = "multi_native.h",
        .service_header_output = header_output,
    };
    databind_compiler_projection_request request = {
        .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &schema_data),
                0);
    check_not_null(root);
    check_not_null(schema_data);

    check_equal(databind_compiler_projection_run(
                    root, &request, 1u, &backend, 1u),
                0);

    check_equal(salts_fs_read_file(source_output, &generated), 0);
    check_not_null(generated.base);
    check_not_null(strstr(
        generated.base, "\"MultiServicePlugin.First.Read\""));
    check_not_null(strstr(
        generated.base, "\"MultiServicePlugin.Second.Write\""));
    check_not_null(strstr(
        generated.base, "\"MultiServicePlugin.First\""));
    check_not_null(strstr(
        generated.base, "\"MultiServicePlugin.Second\""));
    check_not_null(strstr(
        generated.base, ".export_count = 2u"));
    salts_fs_buf_free(&generated);

    check_equal(salts_fs_access(
                    header_output, SALTS_FS_ACCESS_EXISTS),
                0);

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    node_free(root);
    free(schema_data);
  }
}
