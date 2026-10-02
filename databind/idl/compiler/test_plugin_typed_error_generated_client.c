#include <salts/plugin.h>
#include <data_bind_plugin_catalog.h>
#include <tinytest.h>

#include "error.plugin_client.h"
#include "data_bind_plugin_execution.h"

#ifndef GENERATED_TYPED_ERROR_PLUGIN_PATH
#error "GENERATED_TYPED_ERROR_PLUGIN_PATH is required"
#endif

typedef databind_plugin_client_11_ErrorPlugin_11_StorePlugin
    StorePluginClient;

spec("generated typed-error DataBind Plugin client") {
  it("keeps bridge, native status and typed Service outcome distinct") {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {.capacity = 2u};
    salts_plugin_ref ref = {0};
    StorePluginClient client = ERRORPLUGIN_STOREPLUGIN_PLUGIN_CLIENT_INIT;
    salts_plugin_lifecycle_info info = {0};
    Request_t request = {.id = 5u};
    Response_t response = {0};
    databind_11_ErrorPlugin_5_Store_4_Read__error typed_error =
        databind_11_ErrorPlugin_5_Store_4_Read__ERROR_INIT;
    int native_status = 77;
    bool quiescent = true;

    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_TYPED_ERROR_PLUGIN_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);

    {
      salts_plugin_lease catalog_lease = {0};
      const salts_plugin_manifest *manifest = NULL;
      const salts_plugin_export *catalog_entry = NULL;
      const salts_plugin_export *function_entry = NULL;
      data_bind_plugin_catalog *catalog = NULL;
      DataBindPluginOperationBinding operation =
          DATA_BIND_PLUGIN_OPERATION_BINDING_INIT;
      DataBindError catalog_error = DATA_BIND_ERROR_INIT;

      check_equal(salts_plugin_registry_acquire(
                      &registry, ref, &catalog_lease, &manifest),
                  SALTS_PLUGIN_OK);
      check_not_null(manifest);
      check_equal(salts_plugin_manifest_find_export(
                      manifest, DATA_BIND_PLUGIN_CATALOG_EXPORT_ID,
                      &catalog_entry),
                  SALTS_PLUGIN_OK);
      check_not_null(catalog_entry);
      check_equal(salts_plugin_export_require_interface(
                      catalog_entry,
                      DATA_BIND_PLUGIN_CATALOG_CONTRACT_ID,
                      DATA_BIND_PLUGIN_CATALOG_CONTRACT_VERSION,
                      0u,
                      data_bind_plugin_catalog_interface()),
                  SALTS_PLUGIN_OK);
      catalog =
          (data_bind_plugin_catalog *)catalog_entry->value.interface.value;
      check_true(data_bind_plugin_catalog_valid(catalog));
      check_equal(data_bind_plugin_catalog_operation_count(catalog),
                  (size_t)1u);
      check_equal(data_bind_plugin_catalog_operation_at(
                      catalog, 0u, &operation, &catalog_error),
                  DATA_BIND_OK);
      check_true(data_bind_plugin_operation_binding_valid(&operation));
      check_equal(operation.export_id, "ErrorPlugin.Store.Read");
      check_equal(operation.service_name, "Store");
      check_equal(operation.operation_name, "Read");
      check_equal(operation.error_count, (size_t)1u);
      check_not_null(operation.errors);
      if (operation.errors) {
        check_equal(operation.errors[0].idl_type_name, "NotFound");
        check_equal(operation.errors[0].kind_value, (uint32_t)1u);
      }
      check_equal(operation.error_param_index, (size_t)2u);
      {
        DataBindServiceNativeBinding native =
            DATA_BIND_SERVICE_NATIVE_BINDING_INIT(NULL, NULL, NULL);
        DataBindNativeExecution execution =
            (DataBindNativeExecution)DATA_BIND_NATIVE_EXECUTION_INIT;
        check_true(data_bind_plugin_operation_native_binding(
            &operation, &native));
        check_equal(native.error_count, (size_t)1u);
        check_equal(native.error_param_index, (size_t)2u);

        check_equal(salts_plugin_manifest_find_export(
                        manifest, operation.export_id, &function_entry),
                    SALTS_PLUGIN_OK);
        check_not_null(function_entry);
        check_true(data_bind_plugin_operation_execution_admit(
            &operation, function_entry, &execution));
        check_true(data_bind_native_execution_valid(&execution));
        check_true(execution.function == operation.function);
        check_equal(execution.abi->param_count, (size_t)3u);
        check_equal(
            cmeta_function_param_abi(execution.abi, 2u),
            CMETA_ABI_OBJECT_POINTER);
      }
      check_equal(salts_plugin_registry_release(
                      &registry, &catalog_lease),
                  SALTS_PLUGIN_OK);
    }

    check_equal(
        databind_plugin_client_11_ErrorPlugin_11_StorePlugin_open(
            &registry, ref, &client),
        SALTS_PLUGIN_OK);
    check_true(
        databind_plugin_client_11_ErrorPlugin_11_StorePlugin_valid(&client));

    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(response.value, 50u);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_11_ErrorPlugin_5_Store_4_Read__ERROR_NONE);

    request.id = 7u;
    response.value = 1234u;
    typed_error =
        (databind_11_ErrorPlugin_5_Store_4_Read__error)
            databind_11_ErrorPlugin_5_Store_4_Read__ERROR_INIT;
    native_status = 88;
    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_11_ErrorPlugin_5_Store_4_Read__ERROR_1);
    check_equal(typed_error.payload.error_1.id, 7u);
    check_equal(response.value, 1234u);

    request.id = 99u;
    typed_error.kind =
        databind_11_ErrorPlugin_5_Store_4_Read__ERROR_1;
    typed_error.payload.error_1.id = 999u;
    native_status = 66;
    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, -9);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_11_ErrorPlugin_5_Store_4_Read__ERROR_NONE);

    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(salts_plugin_registry_request_stop(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_BUSY);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_false(quiescent);

    check_equal(
        databind_plugin_client_11_ErrorPlugin_11_StorePlugin_close(&client),
        SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }

  it("does not touch caller status/error on pre-invoke validation failure") {
    StorePluginClient client = ERRORPLUGIN_STOREPLUGIN_PLUGIN_CLIENT_INIT;
    Request_t request = {.id = 1u};
    Response_t response = {0};
    databind_11_ErrorPlugin_5_Store_4_Read__error typed_error =
        databind_11_ErrorPlugin_5_Store_4_Read__ERROR_INIT;
    int native_status = 123;

    typed_error.kind =
        databind_11_ErrorPlugin_5_Store_4_Read__ERROR_1;
    typed_error.payload.error_1.id = 456u;

    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_INVALID_STATE);
    check_equal(native_status, 123);
    check_equal(
        (unsigned)typed_error.kind,
        (unsigned)databind_11_ErrorPlugin_5_Store_4_Read__ERROR_1);
    check_equal(typed_error.payload.error_1.id, 456u);

    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            NULL, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_INVALID_ARGUMENT);
    check_equal(native_status, 123);

    check_equal(
        databind_11_ErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, NULL, &native_status),
        SALTS_PLUGIN_INVALID_ARGUMENT);
    check_equal(native_status, 123);
  }
}
