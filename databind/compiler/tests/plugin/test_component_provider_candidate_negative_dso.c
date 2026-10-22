#include "contract_plugin.component.h"

#include <salts/component_provider_abi.h>
#include <salts/service_host.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef DATABIND_COMPONENT_PROVIDER_GOOD_DSO_PATH
#error "Provide the real generated Provider DSO"
#endif
#ifndef DATABIND_COMPONENT_PROVIDER_BAD_EXPORT_DSO_PATH
#error "Provide a Provider DSO with the wrong export ID"
#endif
#ifndef DATABIND_COMPONENT_PROVIDER_BAD_CONTRACT_DSO_PATH
#error "Provide a Provider DSO with the wrong contract ID"
#endif
#ifndef DATABIND_COMPONENT_PROVIDER_BAD_BINDING_DSO_PATH
#error "Provide a Provider DSO with the wrong binding ABI"
#endif
#ifndef DATABIND_COMPONENT_PROVIDER_BAD_KIND_DSO_PATH
#error "Provide a Provider DSO exporting FUNCTION instead of INTERFACE"
#endif

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "canonical Provider negative DSO test failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); \
    return __LINE__; \
  } \
} while (0)

typedef struct provider_generation_fixture {
  salts_service_host_generation generation;
  salts_configurator_deployment deployments[1];
  salts_configurator_instance instances[1];
  salts_component_dependency dependencies[1];
  size_t order[1];
  salts_service_host_module modules[1];
} provider_generation_fixture;

static salts_service_host_status build_generation(
    provider_generation_fixture *fixture, uint64_t generation_id,
    cmeta_plugin_registry *registry, cmeta_plugin_ref plugin,
    const char *export_id) {
  const salts_service_host_generation_storage storage = {
    fixture->deployments, 1u, fixture->instances, 1u,
    fixture->dependencies, 1u, fixture->order, 1u,
    fixture->modules, 1u
  };
  const salts_service_host_source source = {plugin, export_id, NULL, NULL};
  return salts_service_host_generation_build(
      &fixture->generation, generation_id, registry, &storage,
      NULL, 0u, &source, 1u, NULL, 0u);
}

static int invoke_valid_scope(const salts_service_host_scope *scope) {
  salts_configurator_service service = {0};
  databind_11_WasmRuntime_4_Calc_3_Add__component_service port =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_bind(NULL, NULL);
  AddRequest request = {0};
  AddResponse response = {0};
  request.left = UINT32_C(9);
  request.right = UINT32_C(5);
  if (salts_service_host_scope_find_service(
          scope,
          databind_11_WasmRuntime_4_Calc_3_Add__component_service_interface(),
          &service) != SALTS_SERVICE_HOST_OK)
    return 1;
  if (databind_11_WasmRuntime_4_Calc_3_Add__component_service_borrow_from_object(
          service.object, service.interfaces, &port) != CMETA_OK)
    return 2;
  if (databind_11_WasmRuntime_4_Calc_3_Add__component_service_invoke(
          &port, &request, &response) != 0)
    return 3;
  return response.sum == UINT32_C(14) &&
         response.product == UINT32_C(45) ? 0 : 4;
}

static int reject_candidate_without_disturbing_n(
    salts_service_host_runtime *runtime,
    const salts_service_host_scope *scope,
    cmeta_plugin_registry *registry,
    cmeta_plugin_ref rejected_plugin, cmeta_plugin_ref good_plugin,
    const char *canonical_export,
    salts_service_host_status expected_status,
    cmeta_plugin_status expected_plugin_status) {
  provider_generation_fixture failed = {0};
  cmeta_plugin_lifecycle_info rejected_info = {0};
  cmeta_plugin_lifecycle_info good_info = {0};
  salts_service_host_generation *retired = NULL;
  const salts_service_host_failure *failure;

  if (build_generation(&failed, UINT64_C(2), registry, rejected_plugin,
                       canonical_export) != expected_status)
    return 1;
  failure = salts_service_host_generation_failure(&failed.generation);
  if (failure == NULL || failure->source_index != 0u ||
      failure->plugin_status != expected_plugin_status ||
      failed.generation.state != SALTS_SERVICE_HOST_GENERATION_FAILED ||
      failed.generation.module_count != 0u)
    return 2;
  if (cmeta_plugin_registry_get_lifecycle(
          registry, rejected_plugin, &rejected_info) != CMETA_PLUGIN_OK ||
      rejected_info.active_leases != 0u)
    return 3;
  if (salts_service_host_runtime_publish(
          runtime, &failed.generation, &retired) !=
          SALTS_SERVICE_HOST_INVALID_STATE || retired != NULL)
    return 4;
  if (salts_service_host_scope_generation_id(scope) != UINT64_C(1) ||
      invoke_valid_scope(scope) != 0)
    return 5;
  if (cmeta_plugin_registry_get_lifecycle(
          registry, good_plugin, &good_info) != CMETA_PLUGIN_OK ||
      good_info.active_leases != 1u)
    return 6;
  return 0;
}

static int unload_provider(
    cmeta_plugin_registry *registry, cmeta_plugin_ref plugin) {
  bool quiet = false;
  if (cmeta_plugin_registry_request_stop(
          registry, plugin) != CMETA_PLUGIN_OK ||
      cmeta_plugin_registry_poll_quiescent(
          registry, plugin, &quiet) != CMETA_PLUGIN_OK ||
      !quiet ||
      cmeta_plugin_registry_unload(registry, plugin) != CMETA_PLUGIN_OK)
    return 1;
  return 0;
}

int main(void) {
  const cmeta_plugin_registry_config config = {5u};
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_ref good = {0}, bad_export = {0}, bad_contract = {0};
  cmeta_plugin_ref bad_binding = {0}, bad_kind = {0};
  cmeta_plugin_lifecycle_info info = {0};
  provider_generation_fixture generation_n = {0}, generation_n1 = {0};
  salts_service_host_runtime runtime = SALTS_SERVICE_HOST_RUNTIME_INIT;
  salts_service_host_scope scope_n = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_scope scope_n1 = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_generation *retired = NULL;
  const char *const canonical_export = "component-provider";

  REQUIRE(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, DATABIND_COMPONENT_PROVIDER_GOOD_DSO_PATH, &good) ==
      CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, DATABIND_COMPONENT_PROVIDER_BAD_EXPORT_DSO_PATH, &bad_export) ==
      CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, DATABIND_COMPONENT_PROVIDER_BAD_CONTRACT_DSO_PATH, &bad_contract) ==
      CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, DATABIND_COMPONENT_PROVIDER_BAD_BINDING_DSO_PATH, &bad_binding) ==
      CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, DATABIND_COMPONENT_PROVIDER_BAD_KIND_DSO_PATH, &bad_kind) ==
      CMETA_PLUGIN_OK);

  REQUIRE(cmeta_plugin_registry_start(&registry, good) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, bad_export) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, bad_contract) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, bad_binding) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, bad_kind) == CMETA_PLUGIN_OK);

  REQUIRE(build_generation(
      &generation_n, UINT64_C(1), &registry, good, canonical_export) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_init(&runtime) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &generation_n.generation, &retired) == SALTS_SERVICE_HOST_OK);
  REQUIRE(retired == NULL);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &scope_n) == SALTS_SERVICE_HOST_OK);
  REQUIRE(invoke_valid_scope(&scope_n) == 0);

  /* Four real DSO failures must be failure-atomic: N remains callable, failed
   * N+1 cannot publish, and every attempted module lease is returned. */
  REQUIRE(reject_candidate_without_disturbing_n(
      &runtime, &scope_n, &registry, bad_export, good, canonical_export,
      SALTS_SERVICE_HOST_PLUGIN_ERROR, CMETA_PLUGIN_UNKNOWN_EXPORT) == 0);
  REQUIRE(reject_candidate_without_disturbing_n(
      &runtime, &scope_n, &registry, bad_contract, good, canonical_export,
      SALTS_SERVICE_HOST_PLUGIN_ERROR, CMETA_PLUGIN_INCOMPATIBLE_CONTRACT) == 0);
  REQUIRE(reject_candidate_without_disturbing_n(
      &runtime, &scope_n, &registry, bad_kind, good, canonical_export,
      SALTS_SERVICE_HOST_PLUGIN_ERROR, CMETA_PLUGIN_INCOMPATIBLE_CONTRACT) == 0);
  REQUIRE(reject_candidate_without_disturbing_n(
      &runtime, &scope_n, &registry, bad_binding, good, canonical_export,
      SALTS_SERVICE_HOST_PROVIDER_ERROR, CMETA_PLUGIN_OK) == 0);

  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, good, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(build_generation(
      &generation_n1, UINT64_C(2), &registry, good, canonical_export) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, good, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 2u);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &generation_n1.generation, &retired) == SALTS_SERVICE_HOST_OK);
  REQUIRE(retired == &generation_n.generation);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &scope_n1) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_scope_generation_id(&scope_n1) == UINT64_C(2));
  REQUIRE(invoke_valid_scope(&scope_n) == 0);
  REQUIRE(invoke_valid_scope(&scope_n1) == 0);
  REQUIRE(salts_service_host_generation_drain(&runtime, retired) ==
      SALTS_SERVICE_HOST_BUSY);
  REQUIRE(cmeta_plugin_registry_unload(&registry, good) == CMETA_PLUGIN_BUSY);

  REQUIRE(salts_service_host_scope_release(&scope_n) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(&runtime, retired) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, good, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(invoke_valid_scope(&scope_n1) == 0);

  REQUIRE(salts_service_host_runtime_close(&runtime, &retired) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(retired == &generation_n1.generation);
  REQUIRE(salts_service_host_generation_drain(&runtime, retired) ==
      SALTS_SERVICE_HOST_BUSY);
  REQUIRE(salts_service_host_scope_release(&scope_n1) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(&runtime, retired) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_destroy(&runtime) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, good, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 0u);

  REQUIRE(unload_provider(&registry, bad_kind) == 0);
  REQUIRE(unload_provider(&registry, bad_binding) == 0);
  REQUIRE(unload_provider(&registry, bad_contract) == 0);
  REQUIRE(unload_provider(&registry, bad_export) == 0);
  REQUIRE(unload_provider(&registry, good) == 0);
  REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
  return 0;
}
