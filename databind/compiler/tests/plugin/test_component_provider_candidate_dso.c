#include "contract_plugin.component.h"

#include <salts/component_runtime_abi.h>
#include <salts/plugin.h>
#include <salts/service_host.h>
#include <salts/thread.h>
#include <salts/clock.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Same canonical Contract-only Function ABI as generated PLUGIN; the
 * Component Service Interface adds only its receiver and does not invent
 * native value types. */
CMETA_FUNCTION_METADATA_AS_ABI_RESULT(
  expected_calc_add, "WasmRuntime.Calc.Add", unknown, &cmeta_type_int,
  CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
  (const AddRequest *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
   &databind_11_WasmRuntime_4_Calc_3_Add__component_req_ptr_type,
   CMETA_ABI_OBJECT_POINTER),
  (AddResponse *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,
   &databind_11_WasmRuntime_4_Calc_3_Add__component_resp_ptr_type,
   CMETA_ABI_OBJECT_POINTER));

typedef struct provider_worker_context {
  salts_service_host_runtime *runtime;
  const cmeta_interface_desc *interface_desc;
  cmeta_mutex_t gate;
  bool ready;
  bool stop;
  bool close_observed;
  bool failed;
  size_t invocations;
  size_t post_close_invocations;
} provider_worker_context;

static void provider_worker_fail(provider_worker_context *context) {
  cmeta_mutex_lock(&context->gate);
  context->failed = true;
  context->ready = true;
  cmeta_mutex_unlock(&context->gate);
}

static void provider_worker_main(void *user) {
  provider_worker_context *context = (provider_worker_context *)user;
  salts_service_host_scope scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_configurator_service service = {0};
  databind_11_WasmRuntime_4_Calc_3_Add__component_service port =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_bind(NULL, NULL);
  AddRequest request = {0};
  AddResponse response = {0};
  salts_service_host_status host_status;

  host_status = salts_service_host_scope_acquire(context->runtime, &scope);
  if (host_status != SALTS_SERVICE_HOST_OK) {
    provider_worker_fail(context);
    return;
  }
  host_status = salts_service_host_scope_find_service(
      &scope, context->interface_desc, &service);
  if (host_status != SALTS_SERVICE_HOST_OK ||
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_borrow_from_object(
          service.object, service.interfaces, &port) != CMETA_OK) {
    (void)salts_service_host_scope_release(&scope);
    provider_worker_fail(context);
    return;
  }

  request.left = 5u;
  request.right = 6u;
  cmeta_mutex_lock(&context->gate);
  context->ready = true;
  cmeta_mutex_unlock(&context->gate);

  for (;;) {
    bool stop;
    bool close_observed;
    int invoke_status;

    cmeta_mutex_lock(&context->gate);
    stop = context->stop;
    close_observed = context->close_observed;
    cmeta_mutex_unlock(&context->gate);
    if (stop) break;

    memset(&response, 0, sizeof(response));
    invoke_status = databind_11_WasmRuntime_4_Calc_3_Add__component_service_invoke(
        &port, &request, &response);

    cmeta_mutex_lock(&context->gate);
    if (invoke_status != 0 || response.sum != 11u || response.product != 30u)
      context->failed = true;
    else
      ++context->invocations;
    if (close_observed && invoke_status == 0 &&
        response.sum == 11u && response.product == 30u)
      ++context->post_close_invocations;
    cmeta_mutex_unlock(&context->gate);
    cmeta_thread_yield();
  }

  if (salts_service_host_scope_release(&scope) != SALTS_SERVICE_HOST_OK) {
    cmeta_mutex_lock(&context->gate);
    context->failed = true;
    cmeta_mutex_unlock(&context->gate);
  }
}

#define REQUIRE(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "IDL FR Component test failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #expr); \
    return 1; \
  } \
} while (0)

int main(void) {
  const cmeta_plugin_registry_config config = {1u};
  cmeta_plugin_registry registry = {0};
  cmeta_plugin_ref ref = {0};
  cmeta_plugin_lifecycle_info info = {0};
  salts_service_host_generation generation = {0};
  salts_service_host_runtime runtime = SALTS_SERVICE_HOST_RUNTIME_INIT;
  salts_service_host_scope scope = SALTS_SERVICE_HOST_SCOPE_INIT;
  salts_service_host_generation *previous = NULL;
  salts_configurator_deployment deployments[1] = {{0}};
  salts_configurator_instance instances[1] = {{0}};
  salts_component_dependency dependencies[1] = {{0}};
  size_t order[1] = {0};
  salts_service_host_module modules[1] = {{0}};
  const salts_service_host_generation_storage storage = {
    deployments, 1u, instances, 1u, dependencies, 1u,
    order, 1u, modules, 1u
  };
  salts_service_host_source source = {0};
  salts_configurator_service service = {0};
  databind_11_WasmRuntime_4_Calc_3_Add__component_service port =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_bind(NULL, NULL);
  AddRequest request = {0};
  AddResponse response = {0};
  bool quiet = false;
  provider_worker_context worker = {0};
  cmeta_thread_t worker_thread = NULL;

  /* Verify the actual source-built library's runtime epoch and layouts. */
  REQUIRE(salts_configurator_abi_version() ==
      SALTS_COMPONENT_RUNTIME_ABI_VERSION);
  REQUIRE(salts_service_host_abi_version() ==
      SALTS_COMPONENT_RUNTIME_ABI_VERSION);
  salts_service_host_abi_v1_0_required();
  REQUIRE(salts_configurator_abi_context_size() ==
      sizeof(salts_configurator_context));
  REQUIRE(salts_service_host_abi_scope_size() ==
      sizeof(salts_service_host_scope));

  REQUIRE(cmeta_plugin_registry_init(&registry, &config) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_load(
      &registry, GENERATED_COMPONENT_PROVIDER_PATH, &ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_start(&registry, ref) == CMETA_PLUGIN_OK);

  source.plugin = ref;
  source.export_id = "component-provider";
  REQUIRE(salts_service_host_generation_build(
      &generation, UINT64_C(1), &registry, &storage,
      NULL, 0u, &source, 1u, NULL, 0u) == SALTS_SERVICE_HOST_OK);

  const cmeta_interface_desc *iface =
      databind_11_WasmRuntime_4_Calc_3_Add__component_service_interface();
  REQUIRE(cmeta_interface_desc_valid(iface));
  REQUIRE(iface->method_count == 1u);
  REQUIRE(cmeta_interface_method_reflection_valid(&iface->methods[0]));
  REQUIRE(iface->methods[0].abi->return_carrier == CMETA_ABI_SCALAR);
  REQUIRE(iface->methods[0].function->result_flags == CMETA_RESULT_VALUE);
  REQUIRE(iface->methods[0].function->param_count == 2u);
  REQUIRE(cmeta_function_abi_contract_compatible(
      &expected_calc_add__function_abi_meta, iface->methods[0].abi));
  REQUIRE(cmeta_type_equal(
      iface->methods[0].function->params[0].type,
      &databind_11_WasmRuntime_4_Calc_3_Add__component_req_ptr_type));
  REQUIRE(cmeta_type_desc_valid(&AddRequest_native_cmeta_type));
  REQUIRE(cmeta_type_desc_valid(&AddResponse_native_cmeta_type));
#if defined(DATABIND_COMPONENT_PROVIDER_STRICT_SOURCE)
  REQUIRE(cmeta_interface_borrowed_service_abi_valid(iface));
#endif
  REQUIRE(iface->methods[0].function->params[0].flags ==
      (CMETA_PARAM_IN | CMETA_PARAM_BORROWED));
  REQUIRE(iface->methods[0].function->params[1].flags ==
      (CMETA_PARAM_OUT | CMETA_PARAM_BORROWED));
  REQUIRE(cmeta_component_desc_valid(
      generation.components.deployments[0].provider->component));
  REQUIRE(strcmp(generation.components.deployments[0]
      .provider->component->stable_id, "WasmRuntime.Calculator") == 0);

  REQUIRE(salts_service_host_runtime_init(&runtime) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_publish(
      &runtime, &generation, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == NULL);
  REQUIRE(salts_service_host_scope_acquire(
      &runtime, &scope) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_scope_find_service(
      &scope, iface, &service) == SALTS_SERVICE_HOST_OK);
  REQUIRE(databind_11_WasmRuntime_4_Calc_3_Add__component_service_borrow_from_object(
      service.object, service.interfaces, &port) == CMETA_OK);

  request.left = 5u;
  request.right = 6u;
  REQUIRE(databind_11_WasmRuntime_4_Calc_3_Add__component_service_invoke(
      &port, &request, &response) == 0);
  REQUIRE(response.sum == 11u);
  REQUIRE(response.product == 30u);

  /* A second thread owns a separate live Scope and continuously invokes the
   * same Provider while the owner closes admission and attempts to drain.
   * This exercises the canonical runtime's synchronization, lease protection,
   * and the contract that already-acquired scopes remain valid after close. */
  worker.runtime = &runtime;
  worker.interface_desc = iface;
  cmeta_mutex_init(&worker.gate);
  REQUIRE(worker.gate != NULL);
  REQUIRE(cmeta_thread_create(&worker_thread, provider_worker_main, &worker) ==
      SALTS_OK);
  {
    const uint64_t deadline = cmeta_hrtime() + UINT64_C(5000000000);
    for (;;) {
      bool ready;
      bool failed;
      size_t calls;
      cmeta_mutex_lock(&worker.gate);
      ready = worker.ready;
      failed = worker.failed;
      calls = worker.invocations;
      cmeta_mutex_unlock(&worker.gate);
      if (ready && (failed || calls != 0u)) {
        REQUIRE(!failed);
        REQUIRE(calls != 0u);
        break;
      }
      REQUIRE(cmeta_hrtime() < deadline);
      cmeta_thread_yield();
    }
  }

  REQUIRE(salts_service_host_runtime_close(
      &runtime, &previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(previous == &generation);
  cmeta_mutex_lock(&worker.gate);
  worker.close_observed = true;
  cmeta_mutex_unlock(&worker.gate);
  {
    const uint64_t deadline = cmeta_hrtime() + UINT64_C(5000000000);
    for (;;) {
      size_t calls_after_close;
      cmeta_mutex_lock(&worker.gate);
      calls_after_close = worker.post_close_invocations;
      cmeta_mutex_unlock(&worker.gate);
      if (calls_after_close != 0u) break;
      REQUIRE(cmeta_hrtime() < deadline);
      cmeta_thread_yield();
    }
  }
  REQUIRE(salts_service_host_generation_drain(
      &runtime, previous) == SALTS_SERVICE_HOST_BUSY);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 1u);
  REQUIRE(cmeta_plugin_registry_request_stop(
      &registry, ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_unload(
      &registry, ref) == CMETA_PLUGIN_BUSY);

  cmeta_mutex_lock(&worker.gate);
  worker.stop = true;
  cmeta_mutex_unlock(&worker.gate);
  REQUIRE(cmeta_thread_join(&worker_thread) == SALTS_OK);
  cmeta_thread_destroy(&worker_thread);
  cmeta_mutex_lock(&worker.gate);
  REQUIRE(!worker.failed);
  REQUIRE(worker.post_close_invocations != 0u);
  cmeta_mutex_unlock(&worker.gate);

  /* Both caller-owned borrowed service views must expire before unload. */
  REQUIRE(salts_service_host_scope_release(&scope) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_generation_drain(
      &runtime, previous) == SALTS_SERVICE_HOST_OK);
  REQUIRE(salts_service_host_runtime_destroy(&runtime) ==
      SALTS_SERVICE_HOST_OK);
  REQUIRE(cmeta_plugin_registry_get_lifecycle(
      &registry, ref, &info) == CMETA_PLUGIN_OK);
  REQUIRE(info.active_leases == 0u);
  REQUIRE(cmeta_plugin_registry_poll_quiescent(
      &registry, ref, &quiet) == CMETA_PLUGIN_OK);
  REQUIRE(quiet);
  REQUIRE(cmeta_plugin_registry_unload(
      &registry, ref) == CMETA_PLUGIN_OK);
  REQUIRE(cmeta_plugin_registry_destroy(
      &registry) == CMETA_PLUGIN_OK);
  cmeta_mutex_destroy(&worker.gate);
  return 0;
}
