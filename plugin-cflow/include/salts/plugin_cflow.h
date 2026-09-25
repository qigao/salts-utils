#ifndef SALTS_PLUGIN_CFLOW_H
#define SALTS_PLUGIN_CFLOW_H

#include <salts/plugin_cflow_provider.h>
#include <cflow/function_projection.h>

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
 * A function projection retains the Plugin lease for as long as the CFlow
 * callable may enter plugin-owned code or borrow plugin-owned reflection.
 */
typedef struct salts_plugin_cflow_function_handle {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_function_projection projection;
} salts_plugin_cflow_function_handle;

static inline bool salts_plugin_cflow_function_handle_valid(
    const salts_plugin_cflow_function_handle *handle) {
    return handle != NULL &&
           handle->registry != NULL &&
           salts_plugin_lease_valid(handle->lease) &&
           cflow_function_projection_valid(&handle->projection);
}

/*
 * Acquire one reflected Function export and admit it through Salts 1.6's
 * canonical FunctionDesc -> CFlow projection boundary.
 *
 * No Plugin-private function-shape rules are introduced here. projection_status
 * receives the canonical CFlow admission result when non-NULL. Unsupported
 * reflected shapes are returned as SALTS_PLUGIN_INCOMPATIBLE_CONTRACT while
 * preserving the exact CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_* reason.
 */
salts_plugin_status salts_plugin_cflow_function_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    cflow_op op,
    salts_plugin_cflow_function_handle *out_handle,
    cflow_function_projection_status *projection_status);

/*
 * Release the Plugin lease retained by a function projection.
 *
 * Any Graph/Plan that copied projection.callable must already be destroyed or
 * otherwise quiescent before this call; copied callables borrow plugin code and
 * reflection under this lease.
 */
salts_plugin_status salts_plugin_cflow_function_release(
    salts_plugin_cflow_function_handle *handle);

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
