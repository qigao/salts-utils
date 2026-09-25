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

typedef struct salts_plugin_cflow_executor_handle {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_executor executor;
} salts_plugin_cflow_executor_handle;

static inline bool salts_plugin_cflow_executor_handle_valid(
    const salts_plugin_cflow_executor_handle *handle) {
    return handle != NULL &&
           handle->registry != NULL &&
           salts_plugin_lease_valid(handle->lease) &&
           cflow_executor_valid(&handle->executor);
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

salts_plugin_status salts_plugin_cflow_function_release(
    salts_plugin_cflow_function_handle *handle);

salts_plugin_status salts_plugin_cflow_publisher_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_type_desc *expected_output_type,
    salts_plugin_cflow_publisher_handle *out_handle);

salts_plugin_status salts_plugin_cflow_publisher_release(
    salts_plugin_cflow_publisher_handle *handle);

/*
 * Acquire one ExecutorProvider export and create a fresh Executor under the
 * same Plugin lease. The fresh executor remains directly owned by the handle.
 */
salts_plugin_status salts_plugin_cflow_executor_acquire(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    salts_plugin_cflow_executor_handle *out_handle);

/*
 * Destroy the Executor first, then release the Plugin lease. All accepted work
 * must have settled before destroy returns according to the Executor contract.
 */
salts_plugin_status salts_plugin_cflow_executor_release(
    salts_plugin_cflow_executor_handle *handle);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_CFLOW_H */
