/* A real dynamically-loaded Component Provider from the canonical IDL.
 * Business logic and create/projection authority remain user-supplied C11.
 * This P2.0 fixture publishes via the original, immutable Salts ABI. */
#include "calc_component_plugin.h"
#include <stdint.h>

static int business_state = 17;

static int business_add(void *self,
                        const AddRequest *request, AddResponse *response) {
    const int *state = (const int *)self;
    if (!state || !request || !response) return -1;
    response->sum = request->left + request->right + (uint32_t)*state;
    response->product = request->left * request->right;
    return 0;
}

CMETA_IMPLEMENTS(
    sidlc_11_WasmRuntime_10_Calculator_4_Calc,
    dso_calculator_business, 0u,
    .Add = business_add);

static cmeta_status business_project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected, cmeta_interface_projection *out) {
    (void)context;
    if (object == NULL || out == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_interface_desc_equal(
            expected, sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface()))
        return CMETA_TRAIT_MISSING;
    out->size = sizeof(*out);
    out->interface = sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface();
    out->self = object->object;
    out->dispatch = &dso_calculator_business_vtable;
    return CMETA_OK;
}

static const cmeta_object_interface_provider business_interfaces = {
    sizeof(cmeta_object_interface_provider),
    NULL, business_project
};

static cmeta_status SALTS_COMPONENT_CALL business_create(
    void *context, const cmeta_data_desc *config_data,
    const void *config_value, const salts_component_dependency *dependencies,
    size_t dependency_count, cmeta_object_ref *out_instance) {
    if (!context || !out_instance || config_data || config_value ||
        dependency_count != 0u || dependencies != NULL)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out_instance, context, &cmeta_data_int, NULL);
}

/* Only canonical provider-binding fields, with immutable DSO-owned storage.
 * The generated ComponentDesc is the same exact IDL-generated metadata used
 * by the qualified static C11 test. */
static const salts_component_provider_binding business_binding = {
    sizeof(salts_component_provider_binding),
    SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION,
    &sidlc_11_WasmRuntime_10_Calculator__component_meta,
    &business_state, &business_interfaces, business_create, NULL, NULL
};

/* Provider get_binding, native vtable, canonical Plugin INTERFACE row and
 * passive manifest are generated directly from the canonical IdlComponent.
 * Concrete business function, projection and factory remain caller-owned. */
sidlc_11_WasmRuntime_10_Calculator_DECLARE_PROVIDER_PLUGIN(
    idl_calculator_provider, (1,0,0), business_binding);
