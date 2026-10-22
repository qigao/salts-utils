/* A real DSO that deliberately exports the Provider's canonical export
 * ID and contract as FUNCTION rather than INTERFACE. ServiceHost must reject
 * it during admission before interpreting it as a ComponentProvider. */
#include <salts/component_provider_abi.h>
#include <salts/plugin.h>

#include <stdint.h>

static bool CMETA_PLUGIN_CALL negative_provider_invoke(
    void *context, void *result, void *const *params, size_t count) {
  (void)context;
  (void)result;
  (void)params;
  (void)count;
  return false;
}

static const cmeta_plugin_export negative_provider_exports[] = {
  {
    CMETA_PLUGIN_EXPORT_SIZE,
    CMETA_PLUGIN_EXPORT_FUNCTION,
    SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION,
    UINT64_C(0),
    "component-provider",
    SALTS_COMPONENT_PROVIDER_CONTRACT_ID,
    {
      .function = {
        &salts_component_provider_get_binding__function_meta,
        &salts_component_provider_get_binding__function_abi_meta,
        NULL,
        negative_provider_invoke
      }
    }
  }
};

static const cmeta_plugin_manifest negative_provider_manifest = {
  CMETA_PLUGIN_MANIFEST_SIZE,
  CMETA_PLUGIN_ABI_VERSION,
  "WasmRuntime.Calculator.NegativeKind",
  {1u, 0u, 0u},
  negative_provider_exports,
  1u,
  NULL, NULL, NULL, NULL, NULL
};

CMETA_PLUGIN_QUERY_EXPORT const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
  return host_abi == CMETA_PLUGIN_ABI_VERSION
      ? &negative_provider_manifest : NULL;
}
