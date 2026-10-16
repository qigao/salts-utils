#include "contract_multi.component.h"

#include <salts/service_host.h>
#include <salts/plugin.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "generated strict ServiceHost: %s:%d: %s\n", \
        __FILE__, __LINE__, #expr); \
    return 1; \
  } \
} while (0)

/*
 * Generated native IDL methods must be admitted by the actual strict
 * ServiceHostCandidate source, not by a second ComponentPlugin owner.
 * Both generations use ONE immutable, real DSO with a distinct Plugin lease
 * per generation; old borrowed Scopes stay live during N -> N+1 publication.
 */
static int check_port(
    const salts_service_host_scope *scope,
    const cmeta_interface_desc *interface_desc,
    uint64_t expected_generation) {
  salts_configurator_service service = {0};
  databind_12_ComponentOps_4_Calc_3_Add__component_service port =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_bind(NULL, NULL);
  MathRequest request = {0};
  MathResponse result = {0};

  if (salts_service_host_scope_generation_id(scope) != expected_generation ||
      salts_service_host_scope_find_service(
          scope, interface_desc, &service) != SALTS_SERVICE_HOST_OK ||
      databind_12_ComponentOps_4_Calc_3_Add__component_service_borrow_from_object(
          service.object, service.interfaces, &port) != CMETA_OK)
    return 0;

  request.lhs = 7u;
  request.rhs = 6u;
  if (databind_12_ComponentOps_4_Calc_3_Add__component_service_invoke_Add(
          &port, &request, &result) != 0 ||
      result.value != 13u)
    return 0;
  result.value = 0u;
  if (databind_12_ComponentOps_4_Calc_3_Add__component_service_invoke_Multiply(
          &port, &request, &result) != 0 ||
      result.value != 42u)
    return 0;
  return 1;
}

int main(void) {
  const cmeta_plugin_registry_config registry_config = {1u};
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_ref ref = {0};
  cmeta_plugin_lifecycle_info info = {0};
  salts_service_host_generation current = SALTS_SERVICE_HOST_GENERATION_INIT;
  salts_service_host_generation candidate = SALTS_SERVICE_HOST_GENERATION_INIT;
  salts_service_host_runtime runtime = SALTS_SERVICE_HOST_RUNTIME_INIT;
  salts_service_host_scope old_scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_scope new_scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_generation *previous = NULL;
  salts_configurator_deployment current_deployments[1] = {0};
  salts_configurator_instance current_instances[1] = {0};
  salts_component_dependency current_dependencies[1] = {0};
  size_t current_order[1] = {0};
  salts_service_host_module current_modules[1] = {0};
  salts_configurator_deployment candidate_deployments[1] = {0};
  salts_configurator_instance candidate_instances[1] = {0};
  salts_component_dependency candidate_dependencies[1] = {0};
  size_t candidate_order[1] = {0};
  salts_service_host_module candidate_modules[1] = {0};
  const salts_service_host_generation_storage current_storage = {
    current_deployments, 1u,
    current_instances, 1u,
    current_dependencies, 1u,
    current_order, 1u,
    current_modules, 1u
  };
  const salts_service_host_generation_storage candidate_storage = {
    candidate_deployments, 1u,
    candidate_instances, 1u,
    candidate_dependencies, 1u,
    candidate_order, 1u,
    candidate_modules, 1u
  };
  salts_service_host_source provider = {0};
  const cmeta_interface_desc *iface =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_interface();
  bool quiet = false;

  REQUIRE(cmeta_interface_desc_valid(iface));
  REQUIRE(cmeta_interface_borrowed_service_abi_valid(iface));
  REQUIRE(iface->method_count == 2u);
  REQUIRE(strcmp(iface->methods[0].name, "invoke_Add") == 0);
  REQUIRE(strcmp(iface->methods[1].name, "invoke_Multiply") == 0);

  REQUIRE(cmeta_plugin_registry_init(&registry, &registry_config) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, GENERATED_COMPONENT_MULTI_PROVIDER_PATH, &ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, ref) == CMETA_PLUGIN_OK);
  provider.plugin = ref;
  provider.export_id = "component-provider";

  REQUIRE(salts_service_host_generation_build(
      &current, UINT64_C(100), &registry, &current_storage,
      NULL, 0u, &provider, 1u, NULL, 0u) == SALTS_SERVICE_HOST_OK);
  REQUIRE(current.state == SALTS_SERVICE_HOST_GENERATION_BUILT);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(salts_service_host_runtime_init(&runtime) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &current, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == NULL);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &old_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(check_port(&old_scope, iface, UINT64_C(100)));

  /* Failed N+1 admission must not withdraw N, create an extra live module
   * lease, poison the immutable generated FR2 provider, or authorize a
   * fallback publication. The *same* bounded generation storage is reused
   * after its FAILED-state cleanup and must then accept the real export. */
  {
    salts_service_host_source invalid = provider;
    const salts_service_host_failure *failure;

    invalid.export_id = "missing-component-provider";
    REQUIRE(salts_service_host_generation_build(
        &candidate, UINT64_C(101), &registry, &candidate_storage,
        NULL, 0u, &invalid, 1u, NULL, 0u) ==
        SALTS_SERVICE_HOST_PLUGIN_ERROR);
    REQUIRE(candidate.state == SALTS_SERVICE_HOST_GENERATION_FAILED);
    failure = salts_service_host_generation_failure(&candidate);
    REQUIRE(failure != NULL);
    REQUIRE(failure->source_index == 0u);
    REQUIRE(failure->plugin_status == CMETA_PLUGIN_UNKNOWN_EXPORT);
    REQUIRE(cmeta_plugin_registry_get_lifecycle(
        &registry, ref, &info) == CMETA_PLUGIN_OK);
    REQUIRE(info.active_leases == 1u);

    /* A failed candidate cannot evict N or change previous on failure. */
    previous = &current;
    REQUIRE(salts_service_host_runtime_publish(
        &runtime, &candidate, &previous) ==
        SALTS_SERVICE_HOST_INVALID_STATE);
    REQUIRE(previous == &current);
    REQUIRE(current.state == SALTS_SERVICE_HOST_GENERATION_PUBLISHED);
    REQUIRE(runtime.attached_generations == 1u);
    REQUIRE(check_port(&old_scope, iface, UINT64_C(100)));

    /* Clear the diagnostic output only; keep N's actual Scope alive. */
    previous = NULL;
  }

  REQUIRE(salts_service_host_generation_build(
      &candidate, UINT64_C(101), &registry, &candidate_storage,
      NULL, 0u, &provider, 1u, NULL, 0u) == SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 2u);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &candidate, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == &current);
  REQUIRE(current.state == SALTS_SERVICE_HOST_GENERATION_DRAINING);
  REQUIRE(runtime.attached_generations == 2u);
  REQUIRE(check_port(&old_scope, iface, UINT64_C(100)));

  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &new_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(check_port(&new_scope, iface, UINT64_C(101)));
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &current) == SALTS_SERVICE_HOST_BUSY);
  REQUIRE(salts_service_host_scope_release(&old_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &current) == SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(check_port(&new_scope, iface, UINT64_C(101)));

  REQUIRE(salts_service_host_runtime_close(
      &runtime, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == &candidate);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &candidate) == SALTS_SERVICE_HOST_BUSY);
  REQUIRE(cmeta_plugin_registry_request_stop(
      &registry, ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_unload(&registry, ref) == CMETA_PLUGIN_BUSY);
  REQUIRE(salts_service_host_scope_release(&new_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &candidate) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_destroy(&runtime) == SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 0u);
  REQUIRE(cmeta_plugin_registry_poll_quiescent(
      &registry, ref, &quiet) == CMETA_PLUGIN_OK);
  REQUIRE(quiet);
  REQUIRE(cmeta_plugin_registry_unload(&registry, ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
  return 0;
}
