#include "calc_component.h"
#include <salts/component.h>
#include <stdint.h>
#include <string.h>

/* Native business code retains its own state; generated code declares
 * the Interface, ComponentDesc and canonical Provider Binding only. */
typedef struct calculator_state { int object; } calculator_state;
static calculator_state calculator = {17};

static int add_business(void *self,
                        const AddRequest *request, AddResponse *response) {
    if (!self || !request || !response) return -1;
    response->sum = request->left + request->right;
    response->product = request->left * request->right;
    return 0;
}

CMETA_IMPLEMENTS(sidlc_11_WasmRuntime_10_Calculator_4_Calc,
                 calculator_business, 0u, .Add = add_business);

static cmeta_status project(
    void *context, const cmeta_object_ref *object,
    const cmeta_interface_desc *expected, cmeta_interface_projection *out) {
    (void)context;
    if (!object || !out ||
        !cmeta_interface_desc_equal(
            expected, sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface()))
        return CMETA_TRAIT_MISSING;
    out->size = sizeof(*out);
    out->interface = sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface();
    out->self = object->object;
    out->dispatch = &calculator_business_vtable;
    return CMETA_OK;
}

static const cmeta_object_interface_provider interfaces = {
    sizeof(cmeta_object_interface_provider), NULL, project
};

static cmeta_status create(
    void *context, const cmeta_data_desc *config_data,
    const void *config_value, const salts_component_dependency *deps,
    size_t count, cmeta_object_ref *out) {
    calculator_state *state = (calculator_state *)context;
    if (!state || !out || config_data || config_value || deps || count)
        return CMETA_INVALID_ARGUMENT;
    return cmeta_object_borrow(out, &state->object, &cmeta_data_int, NULL);
}

int main(void) {
    salts_component_provider_binding binding =
        sidlc_11_WasmRuntime_10_Calculator_bind(
            &calculator, &interfaces, create, NULL, NULL);
    salts_component_deployment deployment = {&binding, NULL, NULL};
    salts_component_instance instance = {0};
    salts_component_context context = SALTS_COMPONENT_CONTEXT_INIT;
    salts_component_service service = {0};
    size_t order[1] = {0};
    sidlc_11_WasmRuntime_10_Calculator_4_Calc iface =
        sidlc_11_WasmRuntime_10_Calculator_4_Calc_bind(NULL, NULL);
    AddRequest request = {0};
    AddResponse response = {0};
    int result;

    if (!salts_component_provider_binding_valid(&binding) ||
        !cmeta_component_desc_valid(
            &sidlc_11_WasmRuntime_10_Calculator__component_meta) ||
        strcmp(binding.component->stable_id, "WasmRuntime.Calculator") != 0)
        return 1;
    if (salts_component_context_init(
            &context, &deployment, 1u, NULL, 0u, &instance, 1u,
            NULL, 0u, order, 1u) != SALTS_COMPONENT_OK)
        return 2;
    if (salts_component_context_resolve(&context) != SALTS_COMPONENT_OK)
        return 3;
    if (salts_component_context_start(&context) != SALTS_COMPONENT_OK)
        return 4;
    if (salts_component_context_find_service(
            &context,
            sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface(),
            &service) != SALTS_COMPONENT_OK)
        return 5;
    if (sidlc_11_WasmRuntime_10_Calculator_4_Calc_borrow_from_object(
            service.object, service.interfaces, &iface) != CMETA_OK)
        return 6;
    request.left = UINT32_C(9);
    request.right = UINT32_C(5);
    result = sidlc_11_WasmRuntime_10_Calculator_4_Calc_Add(
        &iface, &request, &response);
    if (result != 0 || response.sum != UINT32_C(14) ||
        response.product != UINT32_C(45))
        return 7;
    if (salts_component_context_stop(&context) != SALTS_COMPONENT_OK)
        return 8;
    return 0;
}
