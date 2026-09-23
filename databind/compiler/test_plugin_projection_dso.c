#include <salts/plugin.h>
#include <tinytest.h>

#include "image.generated.h"

#ifndef DATABIND_GENERATED_PLUGIN_PATH
#error "DATABIND_GENERATED_PLUGIN_PATH is required"
#endif

static salts_plugin_registry make_registry(void) {
  salts_plugin_registry registry = {0};
  salts_plugin_registry_config config = {1u};
  check_equal(
      salts_plugin_registry_init(&registry, &config),
      SALTS_PLUGIN_OK);
  return registry;
}

spec("generated DataBind PLUGIN DSO") {
describe("Service Function publication") {
  it("loads and invokes generated FunctionMeta/FunctionAbi publication") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_lease lease = {0};
    const salts_plugin_manifest *manifest = NULL;
    const salts_plugin_export *entry = NULL;
    DecodeRequest_t request = {0};
    DecodeResponse_t response = {0};
    void *params[2];
    int business_status = -99;
    bool quiescent = false;

    request.id = 21u;
    params[0] = &request;
    params[1] = &response;

    check_equal(
        salts_plugin_registry_load(
            &registry, DATABIND_GENERATED_PLUGIN_PATH, &ref),
        SALTS_PLUGIN_OK);
    check_true(salts_plugin_ref_valid(ref));

    check_equal(
        salts_plugin_registry_start(&registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_acquire(
            &registry, ref, &lease, &manifest),
        SALTS_PLUGIN_OK);
    check_not_null(manifest);
    check_equal(manifest->plugin_id, "Image");
    check_equal(manifest->export_count, (size_t)2u);

    check_equal(
        salts_plugin_manifest_find_export(
            manifest, "Codec.Decode", &entry),
        SALTS_PLUGIN_OK);
    check_not_null(entry);
    check_equal(
        salts_plugin_export_require_function(
            entry, "Codec", 3u, 0u),
        SALTS_PLUGIN_OK);

    check_true(cmeta_function_desc_valid(
        entry->value.function.desc));
    check_true(cmeta_function_abi_desc_valid(
        entry->value.function.abi));
    check_true(
        entry->value.function.abi->function ==
        entry->value.function.desc);

    check_true(entry->value.function.invoke(
        entry->value.function.context,
        &business_status,
        params,
        2u));
    check_equal(business_status, 0);
    check_equal(response.width, 42u);

    check_equal(
        salts_plugin_registry_release(&registry, &lease),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_request_stop(&registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_poll_quiescent(
            &registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(
        salts_plugin_registry_unload(&registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }
}
}
