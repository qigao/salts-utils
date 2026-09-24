#ifndef SALTS_PLUGIN_CFLOW_H
#define SALTS_PLUGIN_CFLOW_H

#include <salts/plugin_cflow_provider.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct salts_plugin_cflow_publisher_handle {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_publisher publisher;
} salts_plugin_cflow_publisher_handle;

static inline bool salts_plugin_cflow_publisher_handle_valid(
    const salts_plugin_cflow_publisher_handle *handle) {
    return handle != NULL &&
           handle->registry != NULL &&
           salts_plugin_lease_valid(handle->lease) &&
           cflow_publisher_valid(&handle->publisher);
}

/*
 * Acquire one PublisherProvider export and create a fresh Publisher under the
 * same Plugin lease.
 *
 * expected_output_type may be NULL. When non-NULL, the fresh Publisher's
 * output type must be semantically equal through CMeta type identity.
 */
salts_plugin_status salts_plugin_cflow_publisher_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_type_desc *expected_output_type,
    salts_plugin_cflow_publisher_handle *out_handle);

/*
 * Destroy/cancel the fresh Publisher first, then release its Plugin lease.
 *
 * This API is for directly-owned Publisher handles. Do not move the Publisher
 * into a raw cflow_subscription and then call this function while that
 * Subscription is still live; the later subscription wrapper owns that lease
 * transfer explicitly.
 */
salts_plugin_status salts_plugin_cflow_publisher_release(
    salts_plugin_cflow_publisher_handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_CFLOW_H */
