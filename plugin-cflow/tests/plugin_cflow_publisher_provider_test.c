#include <salts/plugin_cflow.h>
#include <tinytest.h>

#ifndef PLUGIN_CFLOW_FIXTURE_PATH
#error "PLUGIN_CFLOW_FIXTURE_PATH is required"
#endif

static salts_plugin_registry make_registry(void) {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {1u};
    check_equal(
        salts_plugin_registry_init(&registry, &config),
        SALTS_PLUGIN_OK);
    return registry;
}

static void stop_and_unload(
    salts_plugin_registry *registry,
    salts_plugin_ref ref) {
    bool quiescent = false;
    check_equal(
        salts_plugin_registry_request_stop(registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_poll_quiescent(
            registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(
        salts_plugin_registry_unload(registry, ref),
        SALTS_PLUGIN_OK);
}

spec("Salts PluginCFlow PublisherProvider admission") {
  it("creates independent fresh Publishers under Plugin leases") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_publisher_handle first = {0};
    salts_plugin_cflow_publisher_handle second = {0};
    salts_plugin_lifecycle_info info = {0};
    cflow_publish_context context = {0};
    cflow_step step;
    int first_value = 0;
    int second_value = 0;
    bool quiescent = true;

    check_equal(
        salts_plugin_registry_load(
            &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_start(&registry, ref),
        SALTS_PLUGIN_OK);

    check_equal(
        salts_plugin_cflow_publisher_acquire(
            &registry, ref,
            "test.channel.source.publisher",
            "test.channel.source",
            1u, 0u,
            &cmeta_type_int,
            &first),
        SALTS_PLUGIN_OK);
    check_true(
        salts_plugin_cflow_publisher_handle_valid(&first));

    check_equal(
        salts_plugin_cflow_publisher_acquire(
            &registry, ref,
            "test.channel.source.publisher",
            "test.channel.source",
            1u, 0u,
            &cmeta_type_int,
            &second),
        SALTS_PLUGIN_OK);
    check_true(
        salts_plugin_cflow_publisher_handle_valid(&second));

    check_equal(
        salts_plugin_registry_get_lifecycle(
            &registry, ref, &info),
        SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)2u);

    step = cflow_publisher_resume(
        &first.publisher, &context, &first_value);
    check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);
    step = cflow_publisher_resume(
        &second.publisher, &context, &second_value);
    check_equal(step.kind, CFLOW_STEP_VALUE_AND_DONE);

    check_equal(first_value, 10);
    check_equal(second_value, 11);

    check_equal(
        salts_plugin_registry_request_stop(&registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_unload(&registry, ref),
        SALTS_PLUGIN_BUSY);
    check_equal(
        salts_plugin_registry_poll_quiescent(
            &registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_false(quiescent);

    check_equal(
        salts_plugin_cflow_publisher_release(&first),
        SALTS_PLUGIN_OK);
    check_false(
        salts_plugin_cflow_publisher_handle_valid(&first));

    check_equal(
        salts_plugin_registry_poll_quiescent(
            &registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_false(quiescent);

    check_equal(
        salts_plugin_cflow_publisher_release(&second),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_poll_quiescent(
            &registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_true(quiescent);

    check_equal(
        salts_plugin_registry_unload(&registry, ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }

  it("rejects output-type mismatch without leaking a lease") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_publisher_handle handle = {0};
    salts_plugin_lifecycle_info info = {0};

    check_equal(
        salts_plugin_registry_load(
            &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_start(&registry, ref),
        SALTS_PLUGIN_OK);

    check_equal(
        salts_plugin_cflow_publisher_acquire(
            &registry, ref,
            "test.channel.source.publisher",
            "test.channel.source",
            1u, 0u,
            &cmeta_type_size,
            &handle),
        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
    check_false(
        salts_plugin_cflow_publisher_handle_valid(&handle));

    check_equal(
        salts_plugin_registry_get_lifecycle(
            &registry, ref, &info),
        SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)0u);

    stop_and_unload(&registry, ref);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }

  it("releases the lease when provider open fails") {
    salts_plugin_registry registry = make_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_publisher_handle handle = {0};
    salts_plugin_lifecycle_info info = {0};

    check_equal(
        salts_plugin_registry_load(
            &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
        SALTS_PLUGIN_OK);
    check_equal(
        salts_plugin_registry_start(&registry, ref),
        SALTS_PLUGIN_OK);

    check_equal(
        salts_plugin_cflow_publisher_acquire(
            &registry, ref,
            "test.channel.fail.publisher",
            "test.channel.fail",
            1u, 0u,
            NULL,
            &handle),
        SALTS_PLUGIN_INVALID_STATE);
    check_false(
        salts_plugin_cflow_publisher_handle_valid(&handle));

    check_equal(
        salts_plugin_registry_get_lifecycle(
            &registry, ref, &info),
        SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)0u);

    stop_and_unload(&registry, ref);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }
}
