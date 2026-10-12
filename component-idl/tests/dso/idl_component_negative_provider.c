/* Test-only real Plugin DSO with intentionally failed Component admission.
 * Retains the canonical generated ComponentDesc/export ID and original Salts
 * provider ABI; no alternative loader or runtime. */
#include "calc_component_plugin.h"

#if (defined(COMPONENT_IDL_NEGATIVE_CONTRACT) + \
     defined(COMPONENT_IDL_NEGATIVE_BINDING) + \
     defined(COMPONENT_IDL_NEGATIVE_KIND)) != 1
#error "Select exactly one Component Provider negative test mode"
#endif

static int negative_state = 17;
static cmeta_status SALTS_COMPONENT_CALL negative_create(
    void *context, const cmeta_data_desc *data, const void *config,
    const salts_component_dependency *deps, size_t count,
    cmeta_object_ref *out) {
    if (!context || !out || data || config || deps || count)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out, context, &cmeta_data_int, NULL);
}
static cmeta_status negative_project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected,
    cmeta_interface_projection *out) {
    (void)context; (void)object; (void)expected; (void)out;
    return CMETA_TRAIT_MISSING;
}
static const cmeta_object_interface_provider negative_interfaces = {
    sizeof(cmeta_object_interface_provider), NULL, negative_project
};
static const salts_component_provider_binding negative_binding = {
    sizeof(salts_component_provider_binding),
#if defined(COMPONENT_IDL_NEGATIVE_BINDING)
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION + UINT32_C(1),
#else
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
#endif
    &sidlc_11_WasmRuntime_10_Calculator__component_meta,
    &negative_state, &negative_interfaces, negative_create, NULL, NULL
};
static const salts_component_provider_binding *negative_get_binding(void *self) {
    return (const salts_component_provider_binding *)self;
}
CMETA_IMPLEMENTS(salts_component_provider, negative_provider_impl, 0u,
                 .get_binding = negative_get_binding);
static salts_component_provider negative_interface = {
    (void *)&negative_binding, &negative_provider_impl_vtable
};

#if defined(COMPONENT_IDL_NEGATIVE_KIND)
/* A valid FUNCTION export carrying a real reflected FunctionDesc/FunctionAbi.
 * It has the expected semantic export ID but is NOT a Provider INTERFACE. */
static bool CMETA_PLUGIN_CALL negative_invoke(
    void *context, void *result, void *const *params, size_t count) {
    (void)context; (void)result; (void)params; (void)count;
    return false;
}
static const cmeta_plugin_export negative_exports[] = {{
    CMETA_PLUGIN_EXPORT_SIZE, CMETA_PLUGIN_EXPORT_FUNCTION,
    SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, UINT64_C(0),
    sidlc_11_WasmRuntime_10_Calculator_PROVIDER_EXPORT_ID,
    SALTS_COMPONENT_PROVIDER_CONTRACT_ID,
    { .function = {
        &salts_component_provider_get_binding__function_meta,
        &salts_component_provider_get_binding__function_abi_meta,
        NULL, negative_invoke
    } }
}};
static const cmeta_plugin_manifest negative_manifest = {
    CMETA_PLUGIN_MANIFEST_SIZE, CMETA_PLUGIN_ABI_VERSION,
    "WasmRuntime.Calculator.NegativeKind",
    {1u,0u,0u}, negative_exports, 1u,
    NULL,NULL,NULL,NULL,NULL
};
CMETA_PLUGIN_QUERY_EXPORT const cmeta_plugin_manifest *CMETA_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    return host_abi == CMETA_PLUGIN_ABI_VERSION ? &negative_manifest : NULL;
}
#else
#if defined(COMPONENT_IDL_NEGATIVE_CONTRACT)
#define NEGATIVE_PLUGIN_ID "WasmRuntime.Calculator.NegativeContract"
#define NEGATIVE_CONTRACT_ID "salts.component.provider.wrong.v1"
#else
#define NEGATIVE_PLUGIN_ID "WasmRuntime.Calculator.NegativeBinding"
#define NEGATIVE_CONTRACT_ID SALTS_COMPONENT_PROVIDER_CONTRACT_ID
#endif
#define NEGATIVE_EXPORTS(X) \
    X(interface, (salts_component_provider, &negative_interface), \
      sidlc_11_WasmRuntime_10_Calculator_PROVIDER_EXPORT_ID, \
      NEGATIVE_CONTRACT_ID, SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION, 0)
CMETA_PLUGIN_DECLARE(negative_provider_plugin, NEGATIVE_PLUGIN_ID, (1,0,0),
                     NEGATIVE_EXPORTS, CMETA_PLUGIN_PASSIVE());
#endif
