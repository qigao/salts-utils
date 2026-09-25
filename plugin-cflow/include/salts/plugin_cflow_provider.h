#ifndef SALTS_PLUGIN_CFLOW_PROVIDER_H
#define SALTS_PLUGIN_CFLOW_PROVIDER_H

#include <salts/plugin.h>
#include <cflow/reactive.h>
#include <cflow/executor.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Long-lived Plugin capability that creates one fresh CFlow Publisher per open.
 *
 * The provider is borrowed Plugin state. Each successful open transfers
 * ownership of a new cflow_publisher handle to the caller.
 *
 * Provider contract:
 * - out must be zero/invalid on entry;
 * - failure leaves out invalid;
 * - success returns a valid fresh Publisher;
 * - Publisher destroy is quiescent with respect to Plugin-owned callbacks/code:
 *   after destroy returns, no later callback may enter the Plugin DSO through
 *   that Publisher.
 *
 * The final rule is required because PluginCFlow releases the DSO lease only
 * after Publisher cancel/destroy completes.
 */
#define SALTS_PLUGIN_CFLOW_PUBLISHER_PROVIDER_METHODS(X, I) \
    X(I, R1, bool, open, cflow_publisher *, out)

CMETA_INTERFACE(
    salts_plugin_cflow_publisher_provider,
    SALTS_PLUGIN_CFLOW_PUBLISHER_PROVIDER_METHODS);

/*
 * Long-lived Plugin capability that creates one fresh CFlow Executor per open.
 *
 * Each successful open transfers ownership of a valid cflow_executor handle.
 * Destroy must fully quiesce accepted work and guarantee that no later callback
 * can enter Plugin-owned code before PluginCFlow releases the DSO lease.
 */
#define SALTS_PLUGIN_CFLOW_EXECUTOR_PROVIDER_METHODS(X, I) \
    X(I, R1, bool, open, cflow_executor *, out)

CMETA_INTERFACE(
    salts_plugin_cflow_executor_provider,
    SALTS_PLUGIN_CFLOW_EXECUTOR_PROVIDER_METHODS);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_CFLOW_PROVIDER_H */
