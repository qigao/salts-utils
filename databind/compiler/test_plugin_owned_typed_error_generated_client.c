#include "owned_error.plugin_client.h"
#include "tinytest.h"

#include <cstl/byte_buffer.h>
#include <tstr.h>

#include <string.h>

#ifndef GENERATED_OWNED_TYPED_ERROR_PLUGIN_PATH
#define GENERATED_OWNED_TYPED_ERROR_PLUGIN_PATH ""
#endif

spec("generated owned typed-error Plugin client") {
  it("moves string and bytes errors through one lease-safe envelope") {
    static const unsigned char expected[] = {0xdeu, 0xadu, 0xbeu, 0xefu};
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {.capacity = 2u};
    salts_plugin_ref ref = {0};
    StorePluginClient client = OWNEDERRORPLUGIN_STOREPLUGIN_PLUGIN_CLIENT_INIT;
    salts_plugin_lifecycle_info info = {0};
    Request_t request = {.id = 7u};
    Response_t response = {0};
    databind_16_OwnedErrorPlugin_5_Store_4_Read__error typed_error =
        databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_INIT;
    int native_status = 77;

    databind_16_OwnedErrorPlugin_5_Store_4_Read__error_init(&typed_error);
    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_OWNED_TYPED_ERROR_PLUGIN_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(
        databind_plugin_client_16_OwnedErrorPlugin_11_StorePlugin_open(
            &registry, ref, &client),
        SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal((unsigned)typed_error.kind,
                (unsigned)databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_1);
    check_not_null(typed_error.payload.error_1.detail);
    if (typed_error.payload.error_1.detail != NULL) {
      check_equal(tstr_len(typed_error.payload.error_1.detail),
                  strlen("owned-text"));
      check(memcmp(typed_error.payload.error_1.detail, "owned-text",
                   strlen("owned-text")) == 0);
    }

    /* Replacing an active text error must clear it before moving bytes in. */
    request.id = 8u;
    native_status = 66;
    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal((unsigned)typed_error.kind,
                (unsigned)databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_2);
    check_equal(stl_byte_buffer_size(&typed_error.payload.error_2.payload),
                sizeof(expected));
    if (stl_byte_buffer_data_const(&typed_error.payload.error_2.payload) != NULL)
      check(memcmp(
          stl_byte_buffer_data_const(&typed_error.payload.error_2.payload),
          expected, sizeof(expected)) == 0);

    /* Success replaces and clears the active bytes payload. */
    request.id = 5u;
    response.value = 0u;
    native_status = 55;
    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(response.value, 50u);
    check_equal((unsigned)typed_error.kind,
                (unsigned)databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_NONE);

    /* Native status is distinct and also publishes no typed error. */
    request.id = 99u;
    native_status = 44;
    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read_plugin_client_call(
            &client, &request, &response, &typed_error, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, -9);
    check_equal((unsigned)typed_error.kind,
                (unsigned)databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_NONE);

    /* A pre-invoke failure must not mutate caller-owned error storage. */
    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read__error_select(
            &typed_error,
            databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_1),
        DATA_BIND_OK);
    typed_error.payload.error_1.detail = tstr_dup("sentinel");
    check_not_null(typed_error.payload.error_1.detail);
    native_status = 123;
    {
      StorePluginClient invalid = OWNEDERRORPLUGIN_STOREPLUGIN_PLUGIN_CLIENT_INIT;
      check_equal(
          databind_16_OwnedErrorPlugin_5_Store_4_Read_plugin_client_call(
              &invalid, &request, &response, &typed_error, &native_status),
          SALTS_PLUGIN_INVALID_STATE);
    }
    check_equal(native_status, 123);
    check_equal((unsigned)typed_error.kind,
                (unsigned)databind_16_OwnedErrorPlugin_5_Store_4_Read__ERROR_1);
    check_not_null(typed_error.payload.error_1.detail);
    if (typed_error.payload.error_1.detail != NULL)
      check(memcmp(typed_error.payload.error_1.detail, "sentinel",
                   strlen("sentinel")) == 0);

    check_equal(
        databind_16_OwnedErrorPlugin_5_Store_4_Read__error_clear(&typed_error),
        DATA_BIND_OK);
    check_equal(
        databind_plugin_client_16_OwnedErrorPlugin_11_StorePlugin_close(
            &client),
        SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }
}
