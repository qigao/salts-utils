#include <salts/plugin_cflow.h>
#include <tinytest.h>

#ifndef PLUGIN_CFLOW_FUNCTION_FIXTURE_PATH
#error "PLUGIN_CFLOW_FUNCTION_FIXTURE_PATH is required"
#endif

static salts_plugin_registry make_registry(void) {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {1u};
    check_equal(salts_plugin_registry_init(&registry, &config), SALTS_PLUGIN_OK);
    return registry;
}

static void stop_unload_destroy(
    salts_plugin_registry *registry,
    salts_plugin_ref ref) {
    bool quiescent = false;
    check_equal(salts_plugin_registry_request_stop(registry, ref), SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_poll_quiescent(
                    registry, ref, &quiescent), SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(salts_plugin_registry_unload(registry, ref), SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(registry), SALTS_PLUGIN_OK);
}

spec("Salts PluginCFlow reflected Function projection") {
  it("admits a plugin Function through the Salts 1.6 projection and retains its lease") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_function_handle handle = {0};
    cflow_function_projection_status projection_status =
        CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;
    salts_plugin_lifecycle_info info = {0};
    cflow_graph graph = {0};
    cflow_result result = {0};
    const int input = 21;

    check_equal(salts_plugin_registry_load(
                    &registry, PLUGIN_CFLOW_FUNCTION_FIXTURE_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref), SALTS_PLUGIN_OK);

    check_equal(salts_plugin_cflow_function_acquire(
                    &registry, ref,
                    "test.function.double", "test.function",
                    1u, 0u, CFLOW_OP_MAP,
                    &handle, &projection_status),
                SALTS_PLUGIN_OK);
    check_equal(projection_status, CFLOW_FUNCTION_PROJECTION_OK);
    check_true(salts_plugin_cflow_function_handle_valid(&handle));

    check_equal(salts_plugin_registry_get_lifecycle(&registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    cflow_graph_init(&graph, &cmeta_type_int);
    check_true(cflow_graph_add_function_projection(
        &graph, &handle.projection));
    check_true(cflow_eval_array(&graph, &input, 1u, &result));
    check_equal(result.count, (size_t)1u);
    check_true(cmeta_type_equal(result.type, &cmeta_type_int));
    check_equal(*(const int *)result.data, 42);

    check_equal(salts_plugin_registry_request_stop(&registry, ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_unload(&registry, ref),
                SALTS_PLUGIN_BUSY);

    cflow_result_destroy(&result);
    cflow_graph_destroy(&graph);
    check_equal(salts_plugin_cflow_function_release(&handle),
                SALTS_PLUGIN_OK);

    {
      bool quiescent = false;
      check_equal(salts_plugin_registry_poll_quiescent(
                      &registry, ref, &quiescent), SALTS_PLUGIN_OK);
      check_true(quiescent);
    }
    check_equal(salts_plugin_registry_unload(&registry, ref), SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_destroy(&registry), SALTS_PLUGIN_OK);
  }

  it("preserves canonical unsupported-operator rejection and does not leak a lease") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_function_handle handle = {0};
    cflow_function_projection_status projection_status =
        CFLOW_FUNCTION_PROJECTION_OK;
    salts_plugin_lifecycle_info info = {0};

    check_equal(salts_plugin_registry_load(
                    &registry, PLUGIN_CFLOW_FUNCTION_FIXTURE_PATH, &ref),
                SALTS_PLUGIN_OK);
    check_equal(salts_plugin_registry_start(&registry, ref), SALTS_PLUGIN_OK);

    check_equal(salts_plugin_cflow_function_acquire(
                    &registry, ref,
                    "test.function.double", "test.function",
                    1u, 0u, CFLOW_OP_FILTER,
                    &handle, &projection_status),
                SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
    check_equal(projection_status,
                CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR);
    check_false(salts_plugin_cflow_function_handle_valid(&handle));

    check_equal(salts_plugin_registry_get_lifecycle(&registry, ref, &info),
                SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)0u);

    stop_unload_destroy(&registry, ref);
  }
}
