#include <salts/plugin_cflow.h>
#include <cflow/lower.h>
#include <tinytest.h>

#ifndef PLUGIN_CFLOW_FIXTURE_PATH
#error "PLUGIN_CFLOW_FIXTURE_PATH is required"
#endif

typedef struct subscription_sink_state {
    int value;
    size_t value_count;
    size_t done_count;
    size_t error_count;
} subscription_sink_state;

static bool subscription_sink_value(
    void *user,
    const cmeta_type_desc *type,
    const void *value) {
    subscription_sink_state *state =
        (subscription_sink_state *)user;
    if (state == NULL || value == NULL ||
        !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    state->value = *(const int *)value;
    ++state->value_count;
    return true;
}

static void subscription_sink_error(
    void *user, const char *message) {
    subscription_sink_state *state =
        (subscription_sink_state *)user;
    (void)message;
    if (state != NULL) ++state->error_count;
}

static void subscription_sink_done(void *user) {
    subscription_sink_state *state =
        (subscription_sink_state *)user;
    if (state != NULL) ++state->done_count;
}

static salts_plugin_registry subscription_registry(void) {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {1u};
    check_equal(
        salts_plugin_registry_init(&registry, &config),
        SALTS_PLUGIN_OK);
    return registry;
}

static void subscription_stop_unload(
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

spec("Salts PluginCFlow Subscription lease transfer") {
  it("moves the Plugin lease with Publisher ownership into Subscription") {
    salts_plugin_registry registry = subscription_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_publisher_handle source = {0};
    salts_plugin_cflow_subscription_handle subscription = {0};
    salts_plugin_lifecycle_info info = {0};
    cflow_graph surface = {0};
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    subscription_sink_state sink_state = {0};
    cflow_subscriber_callbacks callbacks = {
        subscription_sink_value,
        subscription_sink_error,
        subscription_sink_done,
        &sink_state
    };
    cflow_subscriber sink =
        cflow_subscriber_from_callbacks(&callbacks);
    cflow_status_result result;
    bool quiescent = true;

    graph.root = CMETA_INVALID_ID;

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
            &source),
        SALTS_PLUGIN_OK);

    cflow_graph_init(&surface, &cmeta_type_int);
    check_true(cflow_graph_normalize(&graph, &surface));
    check_true(cflow_scheduler_test_init(&scheduler));

    result = salts_plugin_cflow_subscribe(
        &subscription, &source, &graph, &scheduler, &sink);
    check_equal(result.status, CFLOW_STATUS_OK);
    check_false(
        salts_plugin_cflow_publisher_handle_valid(&source));
    check_true(
        salts_plugin_cflow_subscription_handle_valid(&subscription));

    check_equal(
        salts_plugin_registry_get_lifecycle(
            &registry, ref, &info),
        SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    result = salts_plugin_cflow_subscription_request_result(
        &subscription, 1u);
    check_equal(result.status, CFLOW_STATUS_OK);
    check_greater(
        cflow_scheduler_run_until_idle(&scheduler, 0u),
        (size_t)0u);

    check_equal(sink_state.value_count, (size_t)1u);
    check_equal(sink_state.value, 10);
    check_equal(sink_state.error_count, (size_t)0u);
    check_equal(sink_state.done_count, (size_t)1u);

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
        salts_plugin_cflow_subscription_close(&subscription),
        SALTS_PLUGIN_OK);
    check_false(
        salts_plugin_cflow_subscription_handle_valid(&subscription));

    check_equal(
        salts_plugin_registry_poll_quiescent(
            &registry, ref, &quiescent),
        SALTS_PLUGIN_OK);
    check_true(quiescent);
    check_equal(
        salts_plugin_registry_unload(&registry, ref),
        SALTS_PLUGIN_OK);

    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    cflow_graph_destroy(&surface);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }

  it("keeps Publisher and lease with caller when subscription admission fails") {
    salts_plugin_registry registry = subscription_registry();
    salts_plugin_ref ref = {0};
    salts_plugin_cflow_publisher_handle source = {0};
    salts_plugin_cflow_subscription_handle subscription = {0};
    salts_plugin_lifecycle_info info = {0};
    cflow_graph surface = {0};
    cflow_graph graph = {0};
    cflow_scheduler scheduler = {0};
    cflow_status_result result;

    graph.root = CMETA_INVALID_ID;

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
            &source),
        SALTS_PLUGIN_OK);

    cflow_graph_init(&surface, &cmeta_type_size);
    check_true(cflow_graph_normalize(&graph, &surface));
    check_true(cflow_scheduler_test_init(&scheduler));

    result = salts_plugin_cflow_subscribe(
        &subscription, &source, &graph, &scheduler, NULL);
    check_equal(result.status, CFLOW_STATUS_TYPE_MISMATCH);
    check_true(
        salts_plugin_cflow_publisher_handle_valid(&source));
    check_false(
        salts_plugin_cflow_subscription_handle_valid(&subscription));

    check_equal(
        salts_plugin_registry_get_lifecycle(
            &registry, ref, &info),
        SALTS_PLUGIN_OK);
    check_equal(info.active_leases, (size_t)1u);

    check_equal(
        salts_plugin_cflow_publisher_release(&source),
        SALTS_PLUGIN_OK);
    subscription_stop_unload(&registry, ref);

    cflow_scheduler_destroy(&scheduler);
    cflow_graph_destroy(&graph);
    cflow_graph_destroy(&surface);
    check_equal(
        salts_plugin_registry_destroy(&registry),
        SALTS_PLUGIN_OK);
  }
}
