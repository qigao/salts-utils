#ifndef SALTS_PLUGIN_CFLOW_H
#define SALTS_PLUGIN_CFLOW_H

#include <salts/plugin.h>

#include <cflow/executor.h>
#include <cflow/reactive.h>
#include <cflow/scheduler.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_PLUGIN_CFLOW_CONTRACT_VERSION 1u
#define SALTS_PLUGIN_CFLOW_PUBLISHER_CONTRACT_ID "salts.cflow.publisher"
#define SALTS_PLUGIN_CFLOW_EXECUTOR_CONTRACT_ID "salts.cflow.executor"
#define SALTS_PLUGIN_CFLOW_SCHEDULER_CONTRACT_ID "salts.cflow.scheduler"

/*
 * Lease-owned borrowed provider bindings.
 *
 * The plugin remains the owner of the CFlow interface value. A live binding
 * pins the plugin DSO through its Plugin lease. Callers may invoke the provider
 * according to the underlying CFlow interface contract but must not call the
 * owning D0 destroy method, move/copy the provider into another owner, or
 * retain the provider pointer after release.
 */
typedef struct salts_plugin_cflow_publisher_binding {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_publisher *publisher;
} salts_plugin_cflow_publisher_binding;

typedef struct salts_plugin_cflow_executor_binding {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_executor *executor;
} salts_plugin_cflow_executor_binding;

typedef struct salts_plugin_cflow_scheduler_binding {
    salts_plugin_registry *registry;
    salts_plugin_lease lease;
    cflow_scheduler *scheduler;
} salts_plugin_cflow_scheduler_binding;

salts_plugin_status salts_plugin_cflow_acquire_publisher(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_publisher_binding *out);

salts_plugin_status salts_plugin_cflow_release_publisher(
    salts_plugin_cflow_publisher_binding *binding);

salts_plugin_status salts_plugin_cflow_acquire_executor(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_executor_binding *out);

salts_plugin_status salts_plugin_cflow_release_executor(
    salts_plugin_cflow_executor_binding *binding);

salts_plugin_status salts_plugin_cflow_acquire_scheduler(
    salts_plugin_registry *registry,
    salts_plugin_ref plugin,
    const char *export_id,
    uint64_t required_capabilities,
    salts_plugin_cflow_scheduler_binding *out);

salts_plugin_status salts_plugin_cflow_release_scheduler(
    salts_plugin_cflow_scheduler_binding *binding);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_CFLOW_H */
