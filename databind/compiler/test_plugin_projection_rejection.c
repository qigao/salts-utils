#include "compiler_core.h"
#include "plugin_projection.h"
#include "service_native.h"

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
#ifndef PLUGIN_COMPONENT_SCHEMA
#error "PLUGIN_COMPONENT_SCHEMA is required"
#endif


static Node *plugin_test_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP || name == NULL) return NULL;
  for (i = 0u; i < parent->data.map.count; ++i) {
    Node *child = parent->data.map.items[i];
    if (child != NULL && child->name != NULL &&
        strcmp(child->name, name) == 0)
      return child;
  }
  return NULL;
}

static int plugin_test_select_codec(
    void *context, const char *service_name) {
  (void)context;
  return service_name != NULL &&
         strcmp(service_name, "Codec") == 0;
}

static int plugin_test_set_schema_version(Node *root, const char *value) {
  Node *schema = plugin_test_child(root, "schema");
  Node *version = plugin_test_child(schema, "schema_version");
  size_t length;
  char *copy;

  if (version == NULL || version->type != NODE_STRING || value == NULL)
    return 0;
  length = strlen(value);
  if (length == SIZE_MAX) return 0;
  copy = (char *)malloc(length + 1u);
  if (copy == NULL) return 0;
  memcpy(copy, value, length + 1u);
  free(version->data.string_val);
  version->data.string_val = copy;
  return 1;
}

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
        .component_name = "StorePlugin",
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
  it("rejects invalid schema contract versions without outputs") {
    static const char source_output[] =
        "databind_plugin_bad_version_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_bad_version_should_not_exist.h";
    static const char *const invalid_versions[] = {
        "", "0", "bad", "4294967296"
    };
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_name = "Bundle",
        .native_header = "bad_version_native.h",
        .service_header_output = header_output,
    };
    databind_compiler_projection_request request = {
        .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;
    size_t i;

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &schema_data),
                0);
    check_not_null(root);
    check_not_null(schema_data);

    for (i = 0u;
         i < sizeof(invalid_versions) / sizeof(invalid_versions[0]);
         ++i) {
      (void)salts_fs_unlink(source_output);
      (void)salts_fs_unlink(header_output);
      check_true(plugin_test_set_schema_version(
          root, invalid_versions[i]));
      check_equal(databind_compiler_projection_run(
                      root, &request, 1u, &backend, 1u),
                  -1);
      check(salts_fs_access(
                source_output, SALTS_FS_ACCESS_EXISTS) != 0);
      check(salts_fs_access(
                header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    }

    node_free(root);
    free(schema_data);
  }

  it("requires an explicit existing Component and never falls back to schema-wide publication") {
    static const char source_output[] =
        "databind_plugin_component_required_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_component_required_should_not_exist.h";
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_name = NULL,
        .native_header = "component_required_native.h",
        .service_header_output = header_output,
    };
    databind_compiler_projection_request request = {
        .kind = DATABIND_COMPILER_PROJECTION_PLUGIN,
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &schema_data),
                0);
    check_not_null(root);
    check_not_null(schema_data);

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    check_equal(databind_compiler_projection_run(
                    root, &request, 1u, &backend, 1u),
                -1);
    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.component_name = "Missing";
    check_equal(databind_compiler_projection_run(
                    root, &request, 1u, &backend, 1u),
                -1);
    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);

    node_free(root);
    free(schema_data);
  }

  it("shared Service native lowering excludes unselected Services before lowering") {
    Node *root = NULL;
    char *schema_data = NULL;
    databind_compiler_service_native_ir native_ir = {0};

    check_equal(tbe_compiler_parse_schema_file(
                    PLUGIN_COMPONENT_SCHEMA, &root, &schema_data),
                0);
    check_not_null(root);
    check_not_null(schema_data);

    check_equal(databind_compiler_service_native_build_selected(
                    root, plugin_test_select_codec, NULL, &native_ir),
                0);
    check_equal(native_ir.operation_count, (size_t)2u);
    check_equal(native_ir.operations[0].service_name, "Codec");
    check_equal(native_ir.operations[1].service_name, "Codec");
    check_equal(native_ir.operations[0].operation_name, "Decode");
    check_equal(native_ir.operations[1].operation_name, "Encode");

    databind_compiler_service_native_destroy(&native_ir);
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
        .component_name = "Bundle",
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
        generated.base, ".plugin_id = \"MultiServicePlugin.Bundle\""));
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
