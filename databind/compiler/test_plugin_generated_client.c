#include <salts/plugin.h>
#include <tinytest.h>

#include "image.plugin_client.h"

#ifndef GENERATED_DATABIND_PLUGIN_PATH
#error "GENERATED_DATABIND_PLUGIN_PATH is required"
#endif

typedef databind_plugin_client_5_Image_14_ImageProcessor
    ImageProcessorPluginClient;

static salts_plugin_registry make_registry(void) {
  salts_plugin_registry registry = {0};
  salts_plugin_registry_config config = {.capacity = 2u};
  check_equal(salts_plugin_registry_init(&registry, &config),
              SALTS_PLUGIN_OK);
  return registry;
}

spec("generated DataBind Plugin client") {
  it("holds one lease across repeated typed calls") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    ImageProcessorPluginClient client = {0};
    salts_plugin_lifecycle_info info = {0};
    DecodeRequest_t decode_request = {.width = 12u};
    DecodeResponse_t decode_response = {0};
    EncodeRequest_t encode_request = {.pixels = 80u};
    EncodeResponse_t encode_response = {0};
    int native_status = -1;
    bool quiescent = true;

    check_equal(salts_plugin_registry_load(
                    &registry, GENERATED_DATABIND_PLUGIN_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref),
                SALTS_PLUGIN_OK);

    check_equal(
        databind_plugin_client_5_Image_14_ImageProcessor_open(
            &registry, ref, &client),
        SALTS_PLUGIN_OK);
    check_true(
        databind_plugin_client_5_Image_14_ImageProcessor_valid(&client));

    check_equal(salts_plugin_registry_get_lifecycle(
                    &registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(
        databind_5_Image_5_Codec_6_Decode_plugin_client_call(
            &client, &decode_request, &decode_response, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(decode_response.pixels, 48u);

    decode_request.width = 7u;
    decode_response = (DecodeResponse_t){0};
    native_status = -1;
    check_equal(
        databind_5_Image_5_Codec_6_Decode_plugin_client_call(
            &client, &decode_request, &decode_response, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(decode_response.pixels, 28u);

    check_equal(
        databind_5_Image_5_Codec_6_Encode_plugin_client_call(
            &client, &encode_request, &encode_response, &native_status),
        SALTS_PLUGIN_OK);
    check_equal(native_status, 0);
    check_equal(encode_response.bytes, 20u);

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
        databind_plugin_client_5_Image_14_ImageProcessor_close(&client),
        SALTS_PLUGIN_OK);
    check_false(
        databind_plugin_client_5_Image_14_ImageProcessor_valid(&client));

    check_equal(salts_plugin_registry_poll_quiescent(
                    &registry, ref, &quiescent),
                SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_OK);

    check_equal(
        databind_plugin_client_5_Image_14_ImageProcessor_open(
            &registry, ref, &client),
        SALTS_PLUGIN_STALE);
    check_false(
        databind_plugin_client_5_Image_14_ImageProcessor_valid(&client));

    check_equal(salts_plugin_registry_destroy(&registry),
                SALTS_PLUGIN_OK);
  }

  it("rejects invalid direct-call arguments without touching business status") {
    ImageProcessorPluginClient client = {0};
    DecodeRequest_t request = {.width = 1u};
    DecodeResponse_t response = {0};
    int native_status = 77;

    check_equal(
        databind_5_Image_5_Codec_6_Decode_plugin_client_call(
            NULL, &request, &response, &native_status),
        SALTS_PLUGIN_INVALID_ARGUMENT);
    check_equal(native_status, 77);

    check_equal(
        databind_5_Image_5_Codec_6_Decode_plugin_client_call(
            &client, &request, &response, &native_status),
        SALTS_PLUGIN_INVALID_STATE);
    check_equal(native_status, 0);

    check_equal(
        databind_plugin_client_5_Image_14_ImageProcessor_close(&client),
        SALTS_PLUGIN_INVALID_ARGUMENT);
  }
}
