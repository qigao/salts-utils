#include "contract_multi.component.h"
#include <salts/component_plugin.h>
#include <salts/plugin.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "multi-operation FR2: %s:%d: %s\n", \
      __FILE__, __LINE__, #x); return 1; \
} } while (0)

/* The generated FR2 entries must have the same FunctionAbi carriers,
 * directions, borrow semantics and source-record identity as the original
 * canonical Contract-only Plugin Function operation. No second type ID. */
CMETA_FUNCTION_METADATA_AS_ABI_RESULT(
  expected_add, "ComponentOps.Calc.Add", unknown, &cmeta_type_int,
  CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
  (const MathRequest *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
   &databind_12_ComponentOps_4_Calc_3_Add__component_req_ptr_type,
   CMETA_ABI_OBJECT_POINTER),
  (MathResponse *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,
   &databind_12_ComponentOps_4_Calc_3_Add__component_resp_ptr_type,
   CMETA_ABI_OBJECT_POINTER));
CMETA_FUNCTION_METADATA_AS_ABI_RESULT(
  expected_multiply, "ComponentOps.Calc.Multiply", unknown, &cmeta_type_int,
  CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
  (const MathRequest *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
   &databind_12_ComponentOps_4_Calc_8_Multiply__component_req_ptr_type,
   CMETA_ABI_OBJECT_POINTER),
  (MathResponse *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,
   &databind_12_ComponentOps_4_Calc_8_Multiply__component_resp_ptr_type,
   CMETA_ABI_OBJECT_POINTER));

int main(void) {
  const cmeta_plugin_registry_config config = {1u};
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_ref ref = {0};
  cmeta_plugin_lifecycle_info info = {0};
  salts_component_plugin_generation gen = {0};
  salts_component_plugin_runtime runtime = SALTS_COMPONENT_PLUGIN_RUNTIME_INIT;
  salts_component_plugin_scope scope = SALTS_COMPONENT_PLUGIN_SCOPE_INIT;
  salts_component_plugin_generation *retired = NULL;
  salts_component_deployment deployments[1] = {0};
  salts_component_instance instances[1] = {0};
  salts_component_dependency dependencies[1] = {0};
  size_t order[1] = {0};
  salts_component_plugin_module modules[1] = {0};
  const salts_component_plugin_generation_storage storage = {
    deployments, 1u, instances, 1u, dependencies, 1u,
    order, 1u, modules, 1u
  };
  salts_component_plugin_source source = {0};
  salts_component_service service = {0};
  databind_12_ComponentOps_4_Calc__component_service port =
      databind_12_ComponentOps_4_Calc__component_service_bind(NULL, NULL);
  const cmeta_interface_desc *iface =
      databind_12_ComponentOps_4_Calc__component_service_interface();
  MathRequest request = {0};
  MathResponse result = {0};
  bool quiet = false;

  CHECK(cmeta_interface_desc_valid(iface));
  CHECK(cmeta_interface_borrowed_service_abi_valid(iface));
  CHECK(iface->method_count == 2u);
  CHECK(strcmp(iface->methods[0].name, "invoke_Add") == 0);
  CHECK(strcmp(iface->methods[1].name, "invoke_Multiply") == 0);
  CHECK(cmeta_function_abi_contract_compatible(
      &expected_add__function_abi_meta, iface->methods[0].abi));
  CHECK(cmeta_function_abi_contract_compatible(
      &expected_multiply__function_abi_meta, iface->methods[1].abi));
  CHECK(cmeta_type_equal(
      iface->methods[0].function->params[0].type,
      iface->methods[1].function->params[0].type));
  CHECK(cmeta_type_equal(
      iface->methods[0].function->params[1].type,
      iface->methods[1].function->params[1].type));
  CHECK(cmeta_type_desc_valid(&MathRequest_native_cmeta_type));
  CHECK(cmeta_type_desc_valid(&MathResponse_native_cmeta_type));

  CHECK(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);
  CHECK(cmeta_plugin_registry_load(
      &registry, GENERATED_COMPONENT_MULTI_PROVIDER_PATH, &ref) == CMETA_PLUGIN_OK);
  CHECK(cmeta_plugin_registry_start(&registry, ref) == CMETA_PLUGIN_OK);
  source.plugin = ref;
  source.export_id = "component-provider";
  CHECK(salts_component_plugin_generation_build(
      &gen, UINT64_C(41), &registry, &storage,
      NULL, 0u, &source, 1u, NULL, 0u) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(cmeta_component_desc_valid(
      gen.components.deployments[0].provider->component));
  CHECK(strcmp(gen.components.deployments[0]
      .provider->component->stable_id, "ComponentOps.MathProvider") == 0);

  CHECK(salts_component_plugin_runtime_init(&runtime) ==
      SALTS_COMPONENT_PLUGIN_OK);
  CHECK(salts_component_plugin_runtime_publish(
      &runtime, &gen, &retired) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(retired == NULL);
  CHECK(salts_component_plugin_scope_acquire(
      &runtime, &scope) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(salts_component_plugin_scope_find_service(
      &scope, iface, &service) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(databind_12_ComponentOps_4_Calc__component_service_borrow_from_object(
      service.object, service.interfaces, &port) == CMETA_OK);

  request.lhs = 8u;
  request.rhs = 5u;
  CHECK(databind_12_ComponentOps_4_Calc__component_service_invoke_Add(
      &port, &request, &result) == 0);
  CHECK(result.value == 13u);
  result.value = 0u;
  CHECK(databind_12_ComponentOps_4_Calc__component_service_invoke_Multiply(
      &port, &request, &result) == 0);
  CHECK(result.value == 40u);

  CHECK(salts_component_plugin_runtime_close(
      &runtime, &retired) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(retired == &gen);
  CHECK(salts_component_plugin_generation_drain(
      &runtime, retired) == SALTS_COMPONENT_PLUGIN_BUSY);
  CHECK(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  CHECK(info.active_leases == 1u);
  CHECK(cmeta_plugin_registry_request_stop(
      &registry, ref) == CMETA_PLUGIN_OK);
  CHECK(cmeta_plugin_registry_unload(&registry, ref) == CMETA_PLUGIN_BUSY);
  CHECK(salts_component_plugin_scope_release(
      &scope) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(salts_component_plugin_generation_drain(
      &runtime, retired) == SALTS_COMPONENT_PLUGIN_OK);
  CHECK(salts_component_plugin_runtime_destroy(&runtime) ==
      SALTS_COMPONENT_PLUGIN_OK);
  CHECK(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  CHECK(info.active_leases == 0u);
  CHECK(cmeta_plugin_registry_poll_quiescent(
      &registry, ref, &quiet) == CMETA_PLUGIN_OK);
  CHECK(quiet);
  CHECK(cmeta_plugin_registry_unload(&registry, ref) == CMETA_PLUGIN_OK);
  CHECK(cmeta_plugin_registry_destroy(&registry) == CMETA_PLUGIN_OK);
  return 0;
}
