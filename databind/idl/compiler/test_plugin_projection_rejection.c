#include "compiler_core.h"
#include "plugin_projection.h"
#include "service_native.h"

#include "salts_fs.h"
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

#ifndef PLUGIN_UNSUPPORTED_OWNED_TYPED_ERROR_SCHEMA
#error "PLUGIN_UNSUPPORTED_OWNED_TYPED_ERROR_SCHEMA is required"
#endif
#ifndef PLUGIN_TYPED_ERROR_SCHEMA
#error "PLUGIN_TYPED_ERROR_SCHEMA is required"
#endif
#ifndef PLUGIN_MULTI_SERVICE_SCHEMA
#error "PLUGIN_MULTI_SERVICE_SCHEMA is required"
#endif
#ifndef PLUGIN_COMPONENT_SCHEMA
#error "PLUGIN_COMPONENT_SCHEMA is required"
#endif

static int plugin_test_select_codec(
    void *context, const char *service_name) {
  (void)context;
  return service_name != NULL &&
         strcmp(service_name, "Codec") == 0;
}

spec("DataBind Plugin projection semantic rejection") {
  it("emits one lexical typed-error lifetime region across generated early exits") {
    static const char source_output[] =
        "databind_plugin_typed_error_lifetime_region.c";
    static const char header_output[] =
        "databind_plugin_typed_error_lifetime_region.h";
    static const char client_header_output[] =
        "databind_plugin_typed_error_lifetime_region_client.h";
    static const char client_source_output[] =
        "databind_plugin_typed_error_lifetime_region_client.c";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    char *schema_data = NULL;
    salts_fs_buf_t generated = {0};
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_id = "ErrorPlugin.StorePlugin",
        .native_header = "error_native.h",
        .service_header_output = header_output,
        .client_header_output = client_header_output,
        .client_source_output = client_source_output,
    };
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_PLUGIN},
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;
    const char *lease_check;
    const char *error_init;
    const char *invoke_call;
    const char *invoke_clear;
    const char *move_call;
    const char *move_clear;
    const char *native_status_write;

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_TYPED_ERROR_SCHEMA, &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    input = (databind_compiler_projection_input){.contract = contract};

    check_equal(databind_compiler_projection_run(
                    &input, &request, 1u, &backend, 1u),
                0);
    check_equal(salts_fs_read_file(client_source_output, &generated), 0);
    check_not_null(generated.base);

    lease_check = strstr(
        generated.base, "!salts_plugin_lease_valid(client->lease)");
    error_init = strstr(generated.base, "__error_init(&local_error);");
    invoke_call = strstr(
        generated.base, "if (!entry->value.function.invoke(");
    invoke_clear = invoke_call != NULL
        ? strstr(invoke_call, "__error_clear(&local_error);")
        : NULL;
    move_call = strstr(generated.base, "__error_move(typed_error, &local_error)");
    move_clear = move_call != NULL
        ? strstr(move_call, "__error_clear(&local_error);")
        : NULL;
    native_status_write = strstr(generated.base, "*native_status = result;");

    check_not_null(lease_check);
    check_not_null(error_init);
    check_not_null(invoke_call);
    check_not_null(invoke_clear);
    check_not_null(move_call);
    check_not_null(move_clear);
    check_not_null(native_status_write);

    check_true(lease_check < error_init);
    check_true(error_init < invoke_call);
    check_true(invoke_call < invoke_clear);
    check_true(invoke_clear < move_call);
    check_true(move_call < move_clear);
    check_true(move_clear < native_status_write);

    salts_fs_buf_free(&generated);
    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects optional owned typed Service errors before creating outputs") {
    static const char source_output[] =
        "databind_plugin_owned_typed_error_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_owned_typed_error_should_not_exist.h";
    static const char client_header_output[] =
        "databind_plugin_owned_typed_error_client_should_not_exist.h";
    static const char client_source_output[] =
        "databind_plugin_owned_typed_error_client_should_not_exist.c";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_id = "OwnedErrorPlugin.StorePlugin",
        .native_header = "owned_error_native.h",
        .service_header_output = header_output,
        .client_header_output = client_header_output,
        .client_source_output = client_source_output,
    };
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_PLUGIN},
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_UNSUPPORTED_OWNED_TYPED_ERROR_SCHEMA,
                    &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    check_not_null(schema_data);
    input = (databind_compiler_projection_input){
        .contract = contract};

    check_equal(databind_compiler_projection_run(
                    &input, &request, 1u, &backend, 1u),
                -1);

    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_source_output, SALTS_FS_ACCESS_EXISTS) != 0);

    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("rejects invalid schema contract versions without outputs") {
    static const char source_output[] =
        "databind_plugin_bad_version_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_bad_version_should_not_exist.h";
    static const char client_header_output[] =
        "databind_plugin_bad_version_client_should_not_exist.h";
    static const char client_source_output[] =
        "databind_plugin_bad_version_client_should_not_exist.c";
    static const char *const invalid_versions[] = {
        "", "0", "bad", "4294967296"
    };
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_id = "MultiServicePlugin.Bundle",
        .native_header = "bad_version_native.h",
        .service_header_output = header_output,
        .client_header_output = client_header_output,
        .client_source_output = client_source_output,
    };
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_PLUGIN},
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;
    IdlContract invalid_contract = {0};
    size_t i;

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    check_not_null(schema_data);
    input = (databind_compiler_projection_input){
        .contract = contract};

    for (i = 0u;
         i < sizeof(invalid_versions) / sizeof(invalid_versions[0]);
         ++i) {
      invalid_contract = *contract;
      invalid_contract.version = invalid_versions[i];
      input.contract = &invalid_contract;
      (void)salts_fs_unlink(source_output);
      (void)salts_fs_unlink(header_output);
      (void)salts_fs_unlink(client_header_output);
      (void)salts_fs_unlink(client_source_output);
      check_equal(databind_compiler_projection_run(
                      &input, &request, 1u, &backend, 1u),
                  -1);
      check(salts_fs_access(
                source_output, SALTS_FS_ACCESS_EXISTS) != 0);
      check(salts_fs_access(
                header_output, SALTS_FS_ACCESS_EXISTS) != 0);
      check(salts_fs_access(
                client_header_output, SALTS_FS_ACCESS_EXISTS) != 0);
      check(salts_fs_access(
                client_source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    }

    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("requires an explicit existing Component and never falls back to schema-wide publication") {
    static const char source_output[] =
        "databind_plugin_component_required_should_not_exist.c";
    static const char header_output[] =
        "databind_plugin_component_required_should_not_exist.h";
    static const char client_header_output[] =
        "databind_plugin_component_required_client_should_not_exist.h";
    static const char client_source_output[] =
        "databind_plugin_component_required_client_should_not_exist.c";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    char *schema_data = NULL;
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_id = NULL,
        .native_header = "component_required_native.h",
        .service_header_output = header_output,
        .client_header_output = client_header_output,
        .client_source_output = client_source_output,
    };
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_PLUGIN},
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    check_not_null(schema_data);
    input = (databind_compiler_projection_input){
        .contract = contract};

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);
    check_equal(databind_compiler_projection_run(
                    &input, &request, 1u, &backend, 1u),
                -1);
    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_source_output, SALTS_FS_ACCESS_EXISTS) != 0);

    config.component_id = "MultiServicePlugin.Missing";
    check_equal(databind_compiler_projection_run(
                    &input, &request, 1u, &backend, 1u),
                -1);
    check(salts_fs_access(source_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_header_output, SALTS_FS_ACCESS_EXISTS) != 0);
    check(salts_fs_access(client_source_output, SALTS_FS_ACCESS_EXISTS) != 0);

    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("shared Service native lowering excludes unselected Services before lowering") {
    Node *root = NULL;
    IdlContract *contract = NULL;
    char *schema_data = NULL;
    databind_compiler_service_native_ir native_ir = {0};

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_COMPONENT_SCHEMA, &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    check_not_null(schema_data);
    check_equal(databind_compiler_service_native_build_selected(
                    contract,
                    plugin_test_select_codec, NULL, &native_ir),
                0);
    check_equal(native_ir.operation_count, (size_t)2u);
    check_equal(native_ir.operations[0].service_name, "Codec");
    check_equal(native_ir.operations[1].service_name, "Codec");
    check_equal(native_ir.operations[0].operation_name, "Decode");
    check_equal(native_ir.operations[1].operation_name, "Encode");

    databind_compiler_service_native_destroy(&native_ir);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }

  it("publishes multiple Services as independent contracts") {
    static const char source_output[] =
        "databind_plugin_multi_service.c";
    static const char header_output[] =
        "databind_plugin_multi_service.h";
    static const char client_header_output[] =
        "databind_plugin_multi_service_client.h";
    static const char client_source_output[] =
        "databind_plugin_multi_service_client.c";
    Node *root = NULL;
    IdlContract *contract = NULL;
    databind_compiler_projection_input input = {0};
    char *schema_data = NULL;
    salts_fs_buf_t generated = {0};
    databind_compiler_plugin_config config = {
        .plugin_version_major = 1u,
        .plugin_version_minor = 0u,
        .plugin_version_patch = 0u,
        .component_id = "MultiServicePlugin.Bundle",
        .native_header = "multi_native.h",
        .service_header_output = header_output,
        .client_header_output = client_header_output,
        .client_source_output = client_source_output,
    };
    databind_compiler_projection_request request = {
        .id = {DATABIND_COMPILER_PROJECTION_AXIS_ARTIFACT,
               DATABIND_COMPILER_ARTIFACT_PLUGIN},
        .output = source_output,
        .config = &config,
    };
    databind_compiler_projection_backend backend =
        DATABIND_COMPILER_PLUGIN_BACKEND;

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);

    check_equal(databind_compiler_parse_contract_file(
                    PLUGIN_MULTI_SERVICE_SCHEMA, &root, &contract, &schema_data),
                0);
    check_not_null(root);
    check_not_null(contract);
    check_not_null(schema_data);
    input = (databind_compiler_projection_input){
        .contract = contract};

    check_equal(databind_compiler_projection_run(
                    &input, &request, 1u, &backend, 1u),
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
        generated.base, "DATA_BIND_PLUGIN_CATALOG_EXPORT_ID"));
    check_not_null(strstr(
        generated.base, "DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID"));
    check_not_null(strstr(
        generated.base, ".export_count = 3u"));
    salts_fs_buf_free(&generated);

    check_equal(salts_fs_access(
                    header_output, SALTS_FS_ACCESS_EXISTS),
                0);
    check_equal(salts_fs_access(
                    client_header_output, SALTS_FS_ACCESS_EXISTS),
                0);
    generated = (salts_fs_buf_t){0};
    check_equal(salts_fs_read_file(client_source_output, &generated), 0);
    check_not_null(generated.base);
    check_not_null(strstr(
        generated.base, "databind_18_MultiServicePlugin_5_First_4_Read_export"));
    check_not_null(strstr(
        generated.base, "databind_18_MultiServicePlugin_6_Second_5_Write_export"));
    salts_fs_buf_free(&generated);

    (void)salts_fs_unlink(source_output);
    (void)salts_fs_unlink(header_output);
    (void)salts_fs_unlink(client_header_output);
    (void)salts_fs_unlink(client_source_output);
    idl_contract_destroy(contract);
    node_free(root);
    free(schema_data);
  }
}
