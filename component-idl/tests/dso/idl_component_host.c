/* P2.0: actual .so/.dll/.dylib loaded by the existing Salts Plugin
 * registry; no host linkage to the provider DSO and no duplicate loader. */
#include "calc_component_plugin.h"
#include <salts/component_plugin.h>
#include <stdint.h>
#include <stdio.h>

#ifndef COMPONENT_IDL_DSO_PATH
#error "The provider DSO path is required"
#endif

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Component IDL DSO check failed: %s at %s:%d\n", \
                    #condition, __FILE__, __LINE__); \
            return __LINE__; \
        } \
    } while (0)

typedef struct component_idl_generation_fixture {
    salts_component_plugin_generation generation;
    salts_component_deployment deployments[1];
    salts_component_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_component_plugin_module modules[1];
} component_idl_generation_fixture;

static salts_component_plugin_status build_generation(
    component_idl_generation_fixture *fixture, uint64_t generation_id,
    cmeta_plugin_registry *registry, cmeta_plugin_ref provider_ref) {
    const salts_component_plugin_generation_storage storage = {
        fixture->deployments, 1u,
        fixture->instances, 1u,
        fixture->dependencies, 1u,
        fixture->activation_order, 1u,
        fixture->modules, 1u
    };
    const salts_component_plugin_source source = {
        provider_ref, sidlc_11_WasmRuntime_10_Calculator_PROVIDER_EXPORT_ID, NULL, NULL
    };
    return salts_component_plugin_generation_build(
        &fixture->generation, generation_id, registry,
        &storage, NULL, 0u, &source, 1u, NULL, 0u);
}

static int use_generation_scope(const salts_component_plugin_scope *scope) {
    salts_component_service service = {0};
    sidlc_11_WasmRuntime_10_Calculator_4_Calc iface =
        sidlc_11_WasmRuntime_10_Calculator_4_Calc_bind(NULL, NULL);
    AddRequest request = {0};
    AddResponse response = {0};
    request.left = UINT32_C(9);
    request.right = UINT32_C(5);
    if (salts_component_plugin_scope_find_service(
            scope, sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface(),
            &service) != SALTS_COMPONENT_PLUGIN_OK)
        return -1;
    if (sidlc_11_WasmRuntime_10_Calculator_4_Calc_borrow_from_object(
            service.object, service.interfaces, &iface) != CMETA_OK)
        return -2;
    if (sidlc_11_WasmRuntime_10_Calculator_4_Calc_Add(
            &iface, &request, &response) != 0)
        return -3;
    return response.sum == UINT32_C(31) &&
                   response.product == UINT32_C(45) ? 0 : -4;
}

int main(void) {
    const cmeta_plugin_registry_config config = {1u};
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_ref provider_ref = {0};
    cmeta_plugin_lifecycle_info lifecycle = {0};
    component_idl_generation_fixture g1 = {0}, g2 = {0};
    salts_component_plugin_runtime runtime = SALTS_COMPONENT_PLUGIN_RUNTIME_INIT;
    salts_component_plugin_scope scope1 = SALTS_COMPONENT_PLUGIN_SCOPE_INIT;
    salts_component_plugin_scope scope2 = SALTS_COMPONENT_PLUGIN_SCOPE_INIT;
    salts_component_plugin_generation *previous = NULL;
    bool quiet = false;

    REQUIRE(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_load(
        &registry, COMPONENT_IDL_DSO_PATH, &provider_ref) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_start(
        &registry, provider_ref) == CMETA_PLUGIN_OK);

    REQUIRE(build_generation(&g1, UINT64_C(1), &registry, provider_ref) ==
        SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(build_generation(&g2, UINT64_C(2), &registry, provider_ref) ==
        SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, provider_ref, &lifecycle) == CMETA_PLUGIN_OK);
    REQUIRE(lifecycle.active_leases == 2u);

    REQUIRE(salts_component_plugin_runtime_init(&runtime) ==
        SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(salts_component_plugin_runtime_publish(
        &runtime, &g1.generation, &previous) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(previous == NULL);
    REQUIRE(salts_component_plugin_scope_acquire(
        &runtime, &scope1) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(salts_component_plugin_scope_generation_id(&scope1) == UINT64_C(1));
    REQUIRE(use_generation_scope(&scope1) == 0);

    REQUIRE(salts_component_plugin_runtime_publish(
        &runtime, &g2.generation, &previous) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(previous == &g1.generation);
    REQUIRE(salts_component_plugin_scope_acquire(
        &runtime, &scope2) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(salts_component_plugin_scope_generation_id(&scope2) == UINT64_C(2));
    REQUIRE(use_generation_scope(&scope1) == 0);
    REQUIRE(use_generation_scope(&scope2) == 0);
    REQUIRE(salts_component_plugin_generation_drain(
        &runtime, &g1.generation) == SALTS_COMPONENT_PLUGIN_BUSY);

    REQUIRE(salts_component_plugin_scope_release(
        &scope1) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(salts_component_plugin_generation_drain(
        &runtime, &g1.generation) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, provider_ref, &lifecycle) == CMETA_PLUGIN_OK);
    REQUIRE(lifecycle.active_leases == 1u);
    REQUIRE(use_generation_scope(&scope2) == 0);

    REQUIRE(salts_component_plugin_runtime_close(
        &runtime, &previous) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(previous == &g2.generation);
    REQUIRE(salts_component_plugin_generation_drain(
        &runtime, &g2.generation) == SALTS_COMPONENT_PLUGIN_BUSY);
    REQUIRE(salts_component_plugin_scope_release(
        &scope2) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(salts_component_plugin_generation_drain(
        &runtime, &g2.generation) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, provider_ref, &lifecycle) == CMETA_PLUGIN_OK);
    REQUIRE(lifecycle.active_leases == 0u);
    REQUIRE(salts_component_plugin_runtime_destroy(
        &runtime) == SALTS_COMPONENT_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_request_stop(
        &registry, provider_ref) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_poll_quiescent(
        &registry, provider_ref, &quiet) == CMETA_PLUGIN_OK);
    REQUIRE(quiet);
    REQUIRE(cmeta_plugin_registry_unload(
        &registry, provider_ref) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
    return 0;
}
