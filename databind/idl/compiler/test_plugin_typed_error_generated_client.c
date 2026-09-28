#include <salts/plugin.h>
#include <tinytest.h>

#include "error.plugin_client.h"

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
