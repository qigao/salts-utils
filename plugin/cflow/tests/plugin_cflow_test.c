#include <salts/plugin_cflow.h>
#include <tinytest.h>

#include <string.h>

#ifndef PLUGIN_CFLOW_FIXTURE_PATH
#error "PLUGIN_CFLOW_FIXTURE_PATH is required"
#endif

static void count_task(void *user) {
    int *count = (int *)user;
    ++*count;
}

static salts_plugin_registry make_registry(void) {
    salts_plugin_registry registry = {0};
    salts_plugin_registry_config config = {1u};

    check_equal(salts_plugin_registry_init(&registry, &config),
                SALTS_PLUGIN_OK);
    check_not_null(registry.impl);
    return registry;
}

static void destroy_registry(salts_plugin_registry *registry) {
    check_equal(salts_plugin_registry_destroy(registry),
                SALTS_PLUGIN_OK);
    check_null(registry->impl);
}

spec("Salts PluginCFlow typed bindings") {
describe("typed admission") {
    it("requires a started plugin and exact CFlow provider capabilities") {
        salts_plugin_registry registry = make_registry();
        salts_plugin_ref ref = {0};
        salts_plugin_cflow_executor_binding executor = {0};
        salts_plugin_lifecycle_info info = {0};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_cflow_acquire_executor(
                        &registry, ref, "executor",
                        CMETA_EXEC_CAP_MANUAL, &executor),
                    SALTS_PLUGIN_INVALID_STATE);
        check_false(salts_plugin_lease_valid(executor.lease));
        check_null(executor.executor);

        check_equal(salts_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_cflow_acquire_executor(
                        &registry, ref, "executor_bad_caps",
                        CMETA_EXEC_CAP_MANUAL, &executor),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_false(salts_plugin_lease_valid(executor.lease));
        check_null(executor.executor);

        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)0u);

        check_equal(salts_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);
        {
            bool quiescent = false;
            check_equal(salts_plugin_registry_poll_quiescent(
                            &registry, ref, &quiescent),
                        SALTS_PLUGIN_OK);
            check_true(quiescent);
        }
        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }
}

describe("lifecycle bridge") {
    it("pins the DSO while Publisher Executor and Scheduler work is live") {
        salts_plugin_registry registry = make_registry();
        salts_plugin_ref ref = {0};
        salts_plugin_cflow_publisher_binding publisher = {0};
        salts_plugin_cflow_executor_binding executor = {0};
        salts_plugin_cflow_scheduler_binding scheduler = {0};
        salts_plugin_cflow_executor_binding rejected = {0};
        salts_plugin_lifecycle_info info = {0};
        cflow_publish_context publish_context = {0};
        cflow_schedule_result scheduled;
        cflow_step step;
        bool quiescent = false;
        int publisher_value = 0;
        int task_count = 0;

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_cflow_acquire_publisher(
                        &registry, ref, "publisher",
                        CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
                        &publisher),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_acquire_executor(
                        &registry, ref, "executor",
                        CMETA_EXEC_CAP_MANUAL | CMETA_EXEC_CAP_SERIAL,
                        &executor),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_acquire_scheduler(
                        &registry, ref, "scheduler",
                        CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY,
                        &scheduler),
                    SALTS_PLUGIN_OK);

        check_true(cflow_publisher_valid(publisher.publisher));
        check_true(cflow_executor_valid(executor.executor));
        check_true(cflow_scheduler_valid(scheduler.scheduler));
        check_equal(cflow_publisher_capabilities(publisher.publisher),
                    (uint64_t)CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES);
        check_equal(cflow_executor_capabilities(executor.executor),
                    (uint64_t)(CMETA_EXEC_CAP_MANUAL |
                               CMETA_EXEC_CAP_SERIAL));
        check_equal(cflow_scheduler_capabilities(scheduler.scheduler),
                    (uint64_t)CMETA_SCHED_CAP_CALLER_DRIVEN_ZERO_DELAY);

        step = cflow_publisher_resume(
            publisher.publisher, &publish_context, &publisher_value);
        check_equal(step.kind, CFLOW_STEP_VALUE);
        check_equal(publisher_value, 11);

        check_equal(cflow_executor_try_post(
                        executor.executor, count_task, &task_count),
                    CFLOW_ADMISSION_ACCEPTED);
        scheduled = cflow_scheduler_try_post_after(
            scheduler.scheduler, 0u, count_task, &task_count);
        check_equal(scheduled.status, CFLOW_ADMISSION_ACCEPTED);
        check_true(scheduled.task_id != 0u);

        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)3u);

        check_equal(salts_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_cflow_acquire_executor(
                        &registry, ref, "executor",
                        CMETA_EXEC_CAP_MANUAL, &rejected),
                    SALTS_PLUGIN_INVALID_STATE);
        check_false(salts_plugin_lease_valid(rejected.lease));

        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_BUSY);
        check_equal(salts_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_false(quiescent);

        check_equal(cflow_executor_run_ready(executor.executor),
                    (size_t)1u);
        check_equal(cflow_scheduler_run_ready(scheduler.scheduler),
                    (size_t)1u);
        check_equal(task_count, 2);

        check_equal(salts_plugin_cflow_release_scheduler(&scheduler),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_release_executor(&executor),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_release_publisher(&publisher),
                    SALTS_PLUGIN_OK);

        check_null(scheduler.scheduler);
        check_null(executor.executor);
        check_null(publisher.publisher);

        check_equal(salts_plugin_registry_get_lifecycle(
                        &registry, ref, &info),
                    SALTS_PLUGIN_OK);
        check_equal(info.active_leases, (size_t)0u);

        check_equal(salts_plugin_registry_poll_quiescent(
                        &registry, ref, &quiescent),
                    SALTS_PLUGIN_OK);
        check_true(quiescent);

        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }

    it("keeps release explicit and rejects repeated release") {
        salts_plugin_registry registry = make_registry();
        salts_plugin_ref ref = {0};
        salts_plugin_cflow_publisher_binding publisher = {0};

        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_CFLOW_FIXTURE_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_start(&registry, ref),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_acquire_publisher(
                        &registry, ref, "publisher",
                        CFLOW_PUBLISHER_CAP_CONSTRUCTS_VALUES,
                        &publisher),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_release_publisher(&publisher),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_cflow_release_publisher(&publisher),
                    SALTS_PLUGIN_INVALID_ARGUMENT);

        check_equal(salts_plugin_registry_request_stop(
                        &registry, ref),
                    SALTS_PLUGIN_OK);
        {
            bool quiescent = false;
            check_equal(salts_plugin_registry_poll_quiescent(
                            &registry, ref, &quiescent),
                        SALTS_PLUGIN_OK);
            check_true(quiescent);
        }
        check_equal(salts_plugin_registry_unload(&registry, ref),
                    SALTS_PLUGIN_OK);
        destroy_registry(&registry);
    }
}
}
