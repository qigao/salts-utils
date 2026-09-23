#include <salts/plugin.h>

#include <cflow/executor.h>
#include <cflow/publishers.h>
#include <cflow/scheduler.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct plugin_cflow_fixture_state {
    cflow_publisher publisher;
    cflow_executor executor;
    cflow_scheduler scheduler;
    int values[2];
    bool started;
} plugin_cflow_fixture_state;

static plugin_cflow_fixture_state fixture_state = {
    .values = {11, 22}
};

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_start(void *self) {
    plugin_cflow_fixture_state *state =
        (plugin_cflow_fixture_state *)self;

    if (state == NULL || state->started)
        return SALTS_PLUGIN_INVALID_STATE;

    if (!cflow_publisher_from_array(
            &state->publisher, &cmeta_type_int,
            state->values, 2u))
        return SALTS_PLUGIN_ALLOCATION_FAILED;

    if (!cflow_executor_manual_init_with_capacity(
            &state->executor, 8u)) {
        cflow_publisher_destroy(&state->publisher);
        return SALTS_PLUGIN_ALLOCATION_FAILED;
    }

    if (!cflow_scheduler_manual_init_with_capacity(
            &state->scheduler, 8u)) {
        cflow_executor_destroy(&state->executor);
        cflow_publisher_destroy(&state->publisher);
        return SALTS_PLUGIN_ALLOCATION_FAILED;
    }

    state->started = true;
    return SALTS_PLUGIN_OK;
}

static salts_plugin_status SALTS_PLUGIN_CALL
fixture_request_stop(void *self) {
    plugin_cflow_fixture_state *state =
        (plugin_cflow_fixture_state *)self;
    bool executor_ok;
    bool scheduler_ok;

    if (state == NULL || !state->started)
        return SALTS_PLUGIN_INVALID_STATE;

    if (cflow_publisher_valid(&state->publisher))
        cflow_publisher_cancel(&state->publisher);

    executor_ok = cflow_executor_shutdown(&state->executor);
    scheduler_ok = cflow_scheduler_shutdown(&state->scheduler);
    return executor_ok && scheduler_ok
        ? SALTS_PLUGIN_OK
        : SALTS_PLUGIN_INVALID_STATE;
}

static bool SALTS_PLUGIN_CALL
fixture_is_quiescent(const void *self) {
    const plugin_cflow_fixture_state *state =
        (const plugin_cflow_fixture_state *)self;

    if (state == NULL || !state->started)
        return false;

    return cflow_executor_pending(
               (cflow_executor *)&state->executor) == 0u &&
           cflow_scheduler_pending(
               (cflow_scheduler *)&state->scheduler) == 0u;
}

static void SALTS_PLUGIN_CALL
fixture_destroy(void *self) {
    plugin_cflow_fixture_state *state =
        (plugin_cflow_fixture_state *)self;

    if (state == NULL)
        return;

    if (cflow_publisher_valid(&state->publisher))
        cflow_publisher_destroy(&state->publisher);
    if (cflow_executor_valid(&state->executor))
        cflow_executor_destroy(&state->executor);
    if (cflow_scheduler_valid(&state->scheduler))
        cflow_scheduler_destroy(&state->scheduler);

    state->started = false;
}

static const salts_plugin_export fixture_exports[] = {
    {
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
        .export_id = "publisher",
        .contract_id = "salts.cflow.publisher",
        .interface_desc = &cflow_publisher_interface_meta,
        .interface_value = &fixture_state.publisher,
        .callable = NULL,
    },
    {
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = CMETA_EXEC_CAP_MANUAL | CMETA_EXEC_CAP_SERIAL,
        .export_id = "executor",
        .contract_id = "salts.cflow.executor",
        .interface_desc = &cflow_executor_interface_meta,
        .interface_value = &fixture_state.executor,
        .callable = NULL,
    },
    {
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY,
        .export_id = "scheduler",
        .contract_id = "salts.cflow.scheduler",
        .interface_desc = &cflow_scheduler_interface_meta,
        .interface_value = &fixture_state.scheduler,
        .callable = NULL,
    },
    {
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = CMETA_EXEC_CAP_MANUAL,
        .export_id = "executor_bad_caps",
        .contract_id = "salts.cflow.executor",
        .interface_desc = &cflow_executor_interface_meta,
        .interface_value = &fixture_state.executor,
        .callable = NULL,
    }
};

static const salts_plugin_manifest fixture_manifest = {
    .struct_size = SALTS_PLUGIN_MANIFEST_V1_SIZE,
    .abi_version = SALTS_PLUGIN_ABI_VERSION,
    .plugin_id = "test.plugin.cflow",
    .version = {1u, 0u, 0u},
    .exports = fixture_exports,
    .export_count = sizeof(fixture_exports) / sizeof(fixture_exports[0]),
    .self = &fixture_state,
    .start = fixture_start,
    .request_stop = fixture_request_stop,
    .is_quiescent = fixture_is_quiescent,
    .destroy = fixture_destroy
};

SALTS_PLUGIN_QUERY_EXPORT
const salts_plugin_manifest *SALTS_PLUGIN_CALL
salts_plugin_query(uint32_t host_abi) {
    return host_abi == SALTS_PLUGIN_ABI_VERSION
        ? &fixture_manifest
        : NULL;
}
