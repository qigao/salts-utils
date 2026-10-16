/* A real generated two-Provider N -> N+1 ServiceHost handoff.
 * Providers come from the existing unchanged SaltsUtils Contract/NativeSourceIR. */
#include "contract_plugin.component.h"
#include "contract_multi.component.h"
#include <salts/service_host.h>
#include <salts/component_runtime_abi.h>
#include <salts/plugin.h>
#include <stdint.h>
#include <stdio.h>

#define REQUIRE(x) do { if (!(x)) { \
  fprintf(stderr, "generated ServiceHost switch %s:%d: %s\n", \
          __FILE__, __LINE__, #x); return 1; \
} } while (0)

typedef struct generated_host_bundle {
  salts_service_host_generation generation;
  salts_configurator_deployment deployments[1];
  salts_configurator_instance instances[1];
  salts_component_dependency dependencies[1];
  size_t order[1];
  salts_service_host_module modules[1];
} generated_host_bundle;

static salts_service_host_status generation_build(
    generated_host_bundle *bundle, uint64_t id,
    cmeta_plugin_registry *registry, cmeta_plugin_ref plugin) {
  const salts_service_host_generation_storage storage = {
    bundle->deployments, 1u,
    bundle->instances, 1u,
    bundle->dependencies, 1u,
    bundle->order, 1u,
    bundle->modules, 1u
  };
  const salts_service_host_source src = {plugin, "component-provider", NULL, NULL};
  return salts_service_host_generation_build(
    &bundle->generation, id, registry, &storage,
    NULL, 0u, &src, 1u, NULL, 0u);
}

int main(void) {
  const cmeta_plugin_registry_config config = {2u};
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_ref old_plugin = {0}, new_plugin = {0};
  cmeta_plugin_lifecycle_info info = {0};
  generated_host_bundle old_generation = {0}, new_generation = {0};
  salts_service_host_runtime runtime = SALTS_SERVICE_HOST_RUNTIME_INIT;
  salts_service_host_scope old_scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_scope new_scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_generation *previous = NULL;
  salts_configurator_service service = {0};
  const cmeta_interface_desc *old_iface =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_interface();
  const cmeta_interface_desc *new_iface =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_interface();
  databind_11_WasmRuntime_4_Calc_3_Add__component_service old_port =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_bind(NULL, NULL);
  databind_12_ComponentOps_4_Calc_3_Add__component_service new_port =
      databind_12_ComponentOps_4_Calc_3_Add__component_service_bind(NULL, NULL);
  AddRequest old_request = {0};
  AddResponse old_response = {0};
  MathRequest new_request = {0};
  MathResponse new_response = {0};
  bool quiet = false;

  salts_service_host_abi_v1_0_required();
  REQUIRE(salts_service_host_abi_version() == SALTS_COMPONENT_RUNTIME_ABI_VERSION);
  REQUIRE(salts_configurator_abi_version() == SALTS_COMPONENT_RUNTIME_ABI_VERSION);
  REQUIRE(salts_service_host_abi_scope_size() == sizeof(salts_service_host_scope));
  REQUIRE(salts_configurator_abi_context_size() == sizeof(salts_configurator_context));
  REQUIRE(SALTS_SERVICE_HOST_MAX_ATTACHED_GENERATIONS == 2u);
  REQUIRE(cmeta_interface_borrowed_service_abi_valid(old_iface));
  REQUIRE(cmeta_interface_borrowed_service_abi_valid(new_iface));
  REQUIRE(new_iface->method_count == 2u);
  REQUIRE(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);

  REQUIRE(cmeta_plugin_registry_load(
      &registry, GENERATED_COMPONENT_PROVIDER_PATH, &old_plugin) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, GENERATED_COMPONENT_MULTI_PROVIDER_PATH, &new_plugin) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, old_plugin) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, new_plugin) == CMETA_PLUGIN_OK);

  REQUIRE(generation_build(
      &old_generation, UINT64_C(41), &registry, old_plugin) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_init(&runtime) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &old_generation.generation, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == NULL);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &old_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_scope_generation_id(&old_scope) == UINT64_C(41));
  REQUIRE(salts_service_host_scope_find_service(
      &old_scope, old_iface, &service) == SALTS_SERVICE_HOST_OK);
  REQUIRE(databind_11_WasmRuntime_4_Calc_3_Add__component_service_borrow_from_object(
      service.object, service.interfaces, &old_port) == CMETA_OK);

  old_request.left = 5u;
  old_request.right = 6u;
  REQUIRE(databind_11_WasmRuntime_4_Calc_3_Add__component_service_invoke(
      &old_port, &old_request, &old_response) == 0);
  REQUIRE(old_response.sum == 11u && old_response.product == 30u);

  /* A live borrowed Scope must not be moved or released by an address-copy. */
  salts_service_host_scope invalid_copy = old_scope;
  REQUIRE(salts_service_host_scope_release(&invalid_copy) ==
          SALTS_SERVICE_HOST_INVALID_STATE);

  REQUIRE(generation_build(
      &new_generation, UINT64_C(42), &registry, new_plugin) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &new_generation.generation, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == &old_generation.generation);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &new_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_scope_generation_id(&new_scope) == UINT64_C(42));
  REQUIRE(salts_service_host_scope_find_service(
      &new_scope, new_iface, &service) == SALTS_SERVICE_HOST_OK);
  REQUIRE(databind_12_ComponentOps_4_Calc_3_Add__component_service_borrow_from_object(
      service.object, service.interfaces, &new_port) == CMETA_OK);
  new_request.lhs = 8u;
  new_request.rhs = 5u;
  REQUIRE(databind_12_ComponentOps_4_Calc_3_Add__component_service_invoke_Add(
      &new_port, &new_request, &new_response) == 0);
  REQUIRE(new_response.value == 13u);
  new_response.value = 0u;
  REQUIRE(databind_12_ComponentOps_4_Calc_3_Add__component_service_invoke_Multiply(
      &new_port, &new_request, &new_response) == 0);
  REQUIRE(new_response.value == 40u);

  /* Old N is still executable after N+1 publication while its original
   * Scope pins old DSO. New requests bind only to the new generation. */
  old_response.sum = 0u;
  old_response.product = 0u;
  REQUIRE(databind_11_WasmRuntime_4_Calc_3_Add__component_service_invoke(
      &old_port, &old_request, &old_response) == 0);
  REQUIRE(old_response.sum == 11u && old_response.product == 30u);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &old_generation.generation) == SALTS_SERVICE_HOST_BUSY);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, old_plugin, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(cmeta_plugin_registry_unload(
      &registry, old_plugin) == CMETA_PLUGIN_BUSY);
  REQUIRE(salts_service_host_scope_release(&old_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &old_generation.generation) == SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, old_plugin, &info) == CMETA_PLUGIN_OK && info.active_leases == 0u);
  REQUIRE(cmeta_plugin_registry_request_stop(
      &registry, old_plugin) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_poll_quiescent(
      &registry, old_plugin, &quiet) == CMETA_PLUGIN_OK && quiet);
  REQUIRE(cmeta_plugin_registry_unload(&registry, old_plugin) == CMETA_PLUGIN_OK);

  REQUIRE(salts_service_host_runtime_close(
      &runtime, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == &new_generation.generation);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &new_generation.generation) == SALTS_SERVICE_HOST_BUSY);
  REQUIRE(salts_service_host_scope_release(&new_scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, &new_generation.generation) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_destroy(&runtime) == SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, new_plugin, &info) == CMETA_PLUGIN_OK && info.active_leases == 0u);
  REQUIRE(cmeta_plugin_registry_request_stop(
      &registry, new_plugin) == CMETA_PLUGIN_OK);
  quiet = false;
  REQUIRE(cmeta_plugin_registry_poll_quiescent(
      &registry, new_plugin, &quiet) == CMETA_PLUGIN_OK && quiet);
  REQUIRE(cmeta_plugin_registry_unload(&registry, new_plugin) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
  puts("generated FR2 old N/new N+1 strict Scope/lease/unload PASS");
  return 0;
}
