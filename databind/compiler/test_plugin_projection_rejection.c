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
