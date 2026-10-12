/* Failure-atomic admission of real wrong Provider DSOs against the NEW
 * Salts-owned ServiceHostCandidate. Generated IDL Provider ABI is identical.
 * A successfully recovered N+1 publication follows four N+1 refusals. */
#include "calc_component_plugin.h"
#include <salts/service_host.h>
#include <stdint.h>
#include <stdio.h>

#ifndef COMPONENT_IDL_GOOD_DSO_PATH
#error "Provide the exact real provider DSO"
#endif
#ifndef COMPONENT_IDL_BAD_CONTRACT_DSO_PATH
#error "Provide the wrong-contract DSO"
#endif
#ifndef COMPONENT_IDL_BAD_BINDING_DSO_PATH
#error "Provide the wrong-binding DSO"
#endif
#ifndef COMPONENT_IDL_BAD_KIND_DSO_PATH
#error "Provide the wrong-kind DSO"
#endif
#define REQUIRE(COND) do { \
    if (!(COND)) { \
        fprintf(stderr, "Component DSO rejection: %s (%s:%d)\n", \
                #COND, __FILE__, __LINE__); \
        return __LINE__; \
    } \
} while (0)

typedef struct candidate_fixture {
    salts_service_host_generation generation;
    salts_configurator_deployment deployments[1];
    salts_configurator_instance instances[1];
    salts_component_dependency dependencies[1];
    size_t activation_order[1];
    salts_service_host_module modules[1];
} candidate_fixture;

static salts_service_host_status build_candidate(
    candidate_fixture *fixture, uint64_t id,
    cmeta_plugin_registry *registry, cmeta_plugin_ref ref,
    const char *export_id) {
    const salts_service_host_generation_storage storage = {
        fixture->deployments, 1u, fixture->instances, 1u,
        fixture->dependencies, 1u, fixture->activation_order, 1u,
        fixture->modules, 1u
    };
    const salts_service_host_source source = {
        ref, export_id, NULL, NULL
    };
    return salts_service_host_generation_build(
        &fixture->generation, id, registry, &storage,
        NULL, 0u, &source, 1u, NULL, 0u);
}

static int use_good_scope(const salts_service_host_scope *scope) {
    salts_configurator_service service = {0};
    sidlc_11_WasmRuntime_10_Calculator_4_Calc iface =
        sidlc_11_WasmRuntime_10_Calculator_4_Calc_bind(NULL, NULL);
    AddRequest request = {0};
    AddResponse response = {0};
    request.left = UINT32_C(9);
    request.right = UINT32_C(5);
    if (salts_service_host_scope_find_service(
            scope, sidlc_11_WasmRuntime_10_Calculator_4_Calc_interface(),
            &service) != SALTS_SERVICE_HOST_OK)
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

static int reject_without_disturbing_n(
    salts_service_host_runtime *runtime,
    const salts_service_host_scope *scope,
    cmeta_plugin_registry *registry,
    cmeta_plugin_ref rejected_ref, cmeta_plugin_ref good_ref,
    const char *export_id, salts_service_host_status status,
    cmeta_plugin_status plugin_status) {
    candidate_fixture failed = {0};
    cmeta_plugin_lifecycle_info rejected_info = {0};
    cmeta_plugin_lifecycle_info good_info = {0};
    const salts_service_host_failure *failure;
    salts_service_host_generation *retired = NULL;
    const bool same_plugin =
        rejected_ref.slot == good_ref.slot &&
        rejected_ref.generation == good_ref.generation;

    if (build_candidate(&failed, UINT64_C(2), registry, rejected_ref,
                        export_id) != status)
        return 1;
    failure = salts_service_host_generation_failure(&failed.generation);
    if (failure == NULL || failure->source_index != 0u ||
        failure->plugin_status != plugin_status ||
        failed.generation.state != SALTS_SERVICE_HOST_GENERATION_FAILED ||
        failed.generation.module_count != 0u)
        return 2;
    if (cmeta_plugin_registry_get_lifecycle(
            registry, rejected_ref, &rejected_info) != CMETA_PLUGIN_OK ||
        rejected_info.active_leases != (same_plugin ? 1u : 0u))
        return 3;
    if (salts_service_host_runtime_publish(
            runtime, &failed.generation, &retired) !=
        SALTS_SERVICE_HOST_INVALID_STATE || retired != NULL)
        return 4;
    if (salts_service_host_scope_generation_id(scope) != UINT64_C(1) ||
        use_good_scope(scope) != 0)
        return 5;
    if (cmeta_plugin_registry_get_lifecycle(
            registry, good_ref, &good_info) != CMETA_PLUGIN_OK ||
        good_info.active_leases != 1u)
        return 6;
    return 0;
}

static int unload_provider(
    cmeta_plugin_registry *registry, cmeta_plugin_ref ref) {
    bool quiet = false;
    if (cmeta_plugin_registry_request_stop(
            registry, ref) != CMETA_PLUGIN_OK ||
        cmeta_plugin_registry_poll_quiescent(
            registry, ref, &quiet) != CMETA_PLUGIN_OK ||
        !quiet || cmeta_plugin_registry_unload(
            registry, ref) != CMETA_PLUGIN_OK)
        return 1;
    return 0;
}

int main(void) {
    const cmeta_plugin_registry_config config = {4u};
    cmeta_plugin_registry registry = {0};
    cmeta_plugin_ref good = {0}, wrong_contract = {0};
    cmeta_plugin_ref wrong_binding = {0}, wrong_kind = {0};
    cmeta_plugin_lifecycle_info info = {0};
    candidate_fixture n = {0}, recovered = {0};
    salts_service_host_runtime runtime = SALTS_SERVICE_HOST_RUNTIME_INIT;
    salts_service_host_scope scope = SALTS_SERVICE_HOST_SCOPE_INIT;
    salts_service_host_scope next_scope = SALTS_SERVICE_HOST_SCOPE_INIT;
    salts_service_host_generation *retired = NULL;
    const char *const canonical_export =
        sidlc_11_WasmRuntime_10_Calculator_PROVIDER_EXPORT_ID;

    REQUIRE(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_load(
        &registry, COMPONENT_IDL_GOOD_DSO_PATH, &good) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_load(
        &registry, COMPONENT_IDL_BAD_CONTRACT_DSO_PATH,
        &wrong_contract) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_load(
        &registry, COMPONENT_IDL_BAD_BINDING_DSO_PATH,
        &wrong_binding) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_load(
        &registry, COMPONENT_IDL_BAD_KIND_DSO_PATH,
        &wrong_kind) == CMETA_PLUGIN_OK);

    REQUIRE(cmeta_plugin_registry_start(&registry, good) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_start(
        &registry, wrong_contract) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_start(
        &registry, wrong_binding) == CMETA_PLUGIN_OK);
    REQUIRE(cmeta_plugin_registry_start(
        &registry, wrong_kind) == CMETA_PLUGIN_OK);

    REQUIRE(build_candidate(
        &n, UINT64_C(1), &registry, good, canonical_export) ==
        SALTS_SERVICE_HOST_OK);
    REQUIRE(salts_service_host_runtime_init(
        &runtime) == SALTS_SERVICE_HOST_OK);
    REQUIRE(salts_service_host_runtime_publish(
        &runtime, &n.generation, &retired) == SALTS_SERVICE_HOST_OK);
    REQUIRE(retired == NULL);
    REQUIRE(salts_service_host_scope_acquire(
        &runtime, &scope) == SALTS_SERVICE_HOST_OK);
    REQUIRE(use_good_scope(&scope) == 0);

    /* Each genuine DSO failure must release its attempted lease, preserve
     * N and refuse publication of an incomplete Generation N+1. */
    REQUIRE(reject_without_disturbing_n(
        &runtime, &scope, &registry, good, good,
        "missing-idl-export", SALTS_SERVICE_HOST_PLUGIN_ERROR,
        CMETA_PLUGIN_UNKNOWN_EXPORT) == 0);
    REQUIRE(reject_without_disturbing_n(
        &runtime, &scope, &registry, wrong_contract, good,
        canonical_export, SALTS_SERVICE_HOST_PLUGIN_ERROR,
        CMETA_PLUGIN_INCOMPATIBLE_CONTRACT) == 0);
    REQUIRE(reject_without_disturbing_n(
        &runtime, &scope, &registry, wrong_kind, good,
        canonical_export, SALTS_SERVICE_HOST_PLUGIN_ERROR,
        CMETA_PLUGIN_INCOMPATIBLE_CONTRACT) == 0);
    REQUIRE(reject_without_disturbing_n(
        &runtime, &scope, &registry, wrong_binding, good,
        canonical_export, SALTS_SERVICE_HOST_PROVIDER_ERROR,
        CMETA_PLUGIN_OK) == 0);

    /* Four failed N+1 admissions must not consume the monotonically
     * increasing publication generation ID or destroy the valid old scope.
     * Publish a RECOVERED real Provider DSO generation afterwards. */
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, good, &info) == CMETA_PLUGIN_OK);
    REQUIRE(info.active_leases == 1u);
    REQUIRE(build_candidate(
        &recovered, UINT64_C(2), &registry, good, canonical_export) ==
        SALTS_SERVICE_HOST_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, good, &info) == CMETA_PLUGIN_OK);
    REQUIRE(info.active_leases == 2u);
    REQUIRE(salts_service_host_runtime_publish(
        &runtime, &recovered.generation, &retired) == SALTS_SERVICE_HOST_OK);
    REQUIRE(retired == &n.generation);
    REQUIRE(salts_service_host_scope_acquire(
        &runtime, &next_scope) == SALTS_SERVICE_HOST_OK);
    REQUIRE(salts_service_host_scope_generation_id(&next_scope) ==
        UINT64_C(2));
    REQUIRE(use_good_scope(&scope) == 0);
    REQUIRE(use_good_scope(&next_scope) == 0);
    REQUIRE(salts_service_host_generation_drain(
        &runtime, retired) == SALTS_SERVICE_HOST_BUSY);
    REQUIRE(cmeta_plugin_registry_unload(&registry, good) ==
        CMETA_PLUGIN_BUSY);

    REQUIRE(salts_service_host_scope_release(
        &scope) == SALTS_SERVICE_HOST_OK);
    REQUIRE(salts_service_host_generation_drain(
        &runtime, retired) == SALTS_SERVICE_HOST_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, good, &info) == CMETA_PLUGIN_OK);
    REQUIRE(info.active_leases == 1u);
    REQUIRE(use_good_scope(&next_scope) == 0);

    REQUIRE(salts_service_host_runtime_close(
        &runtime, &retired) == SALTS_SERVICE_HOST_OK);
    REQUIRE(retired == &recovered.generation);
    REQUIRE(salts_service_host_generation_drain(
        &runtime, retired) == SALTS_SERVICE_HOST_BUSY);
    REQUIRE(salts_service_host_scope_release(
        &next_scope) == SALTS_SERVICE_HOST_OK);
    REQUIRE(salts_service_host_generation_drain(
        &runtime, retired) == SALTS_SERVICE_HOST_OK);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, good, &info) == CMETA_PLUGIN_OK);
    REQUIRE(info.active_leases == 0u);
    REQUIRE(salts_service_host_runtime_destroy(
        &runtime) == SALTS_SERVICE_HOST_OK);

    REQUIRE(unload_provider(&registry, wrong_kind) == 0);
    REQUIRE(unload_provider(&registry, wrong_binding) == 0);
    REQUIRE(unload_provider(&registry, wrong_contract) == 0);
    REQUIRE(unload_provider(&registry, good) == 0);
    REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
    return 0;
}
