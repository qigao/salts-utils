#include <salts/plugin.h>
#include <tinytest.h>

#include "image.plugin.h"

#ifndef BAD_CONTRACT_PLUGIN_PATH
#error "BAD_CONTRACT_PLUGIN_PATH is required"
#endif
#ifndef BAD_CONTRACT_ID_PLUGIN_PATH
#error "BAD_CONTRACT_ID_PLUGIN_PATH is required"
#endif
#ifndef MISSING_EXPORT_PLUGIN_PATH
#error "MISSING_EXPORT_PLUGIN_PATH is required"
#endif
#ifndef BAD_IDENTITY_PLUGIN_PATH
#error "BAD_IDENTITY_PLUGIN_PATH is required"
#endif

static void expect_client_open_failure(
    const char *path, salts_plugin_status expected) {
  salts_plugin_registry registry = {0};
  salts_plugin_registry_config config = {.capacity = 1u};
  salts_plugin_ref ref = {0};
  salts_plugin_lifecycle_info lifecycle = {0};
  databind_5_Image_14_ImageProcessor_plugin_client client =
      databind_5_Image_14_ImageProcessor_PLUGIN_CLIENT_INIT;
  bool quiescent = false;

  check_equal(salts_plugin_registry_init(&registry, &config),
              SALTS_PLUGIN_OK);
  check_equal(salts_plugin_registry_load(&registry, path, &ref),
              SALTS_PLUGIN_OK);
  check_equal(salts_plugin_registry_start(&registry, ref),
              SALTS_PLUGIN_OK);

  check_equal(
      databind_5_Image_14_ImageProcessor_plugin_client_open(
          &registry, ref, &client),
      expected);
  check_null(client.registry);
  check_false(salts_plugin_lease_valid(client.lease));

  check_equal(salts_plugin_registry_get_lifecycle(
                  &registry, ref, &lifecycle),
              SALTS_PLUGIN_OK);
  check_equal(lifecycle.active_leases, (size_t)0u);

  check_equal(salts_plugin_registry_request_stop(&registry, ref),
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

spec("generated Plugin client admission") {
  it("rejects semantic Function mismatch") {
    expect_client_open_failure(
        BAD_CONTRACT_PLUGIN_PATH,
        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
  }

  it("rejects wrong Service contract identity") {
    expect_client_open_failure(
        BAD_CONTRACT_ID_PLUGIN_PATH,
        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
  }

  it("rejects missing operation export identity") {
    expect_client_open_failure(
        MISSING_EXPORT_PLUGIN_PATH,
        SALTS_PLUGIN_UNKNOWN_EXPORT);
  }

  it("rejects wrong Component/plugin identity") {
    expect_client_open_failure(
        BAD_IDENTITY_PLUGIN_PATH,
        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
  }
}
