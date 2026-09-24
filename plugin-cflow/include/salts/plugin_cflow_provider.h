#ifndef SALTS_PLUGIN_CFLOW_PROVIDER_H
#define SALTS_PLUGIN_CFLOW_PROVIDER_H

#include <salts/plugin.h>
#include <cflow/reactive.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Long-lived Plugin capability that creates one fresh CFlow Publisher per open.
 *
 * The provider is borrowed Plugin state. Each successful open transfers
 * ownership of a new cflow_publisher handle to the caller.
 */
#define SALTS_PLUGIN_CFLOW_PUBLISHER_PROVIDER_METHODS(X, I) \
    X(I, R1, bool, open, cflow_publisher *, out)

CMETA_INTERFACE(
    salts_plugin_cflow_publisher_provider,
    SALTS_PLUGIN_CFLOW_PUBLISHER_PROVIDER_METHODS);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_PLUGIN_CFLOW_PROVIDER_H */
