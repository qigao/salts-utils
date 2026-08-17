#include "turbo_lua_worker.h"

#include "lauxlib.h"
#include "lua.h"
#include "tinytest.h"
#include "turbo_error.h"
#include "turbo_thread.h"

typedef struct test_lifecycle_context {
  turbo_lua_executor_t *executor;
  struct lua_State *L;
  int start_status;
  int stop_status;
  int start_owner;
  int stop_owner;
  int stop_calls;
  int stopped_lua_value;
} test_lifecycle_context_t;

typedef struct test_command_context {
  turbo_mutex_t lock;
  turbo_cond_t changed;
  test_lifecycle_context_t *lifecycle;
  turbo_lua_worker_t *worker;
  turbo_lua_executor_dispatch_reason_t reason;
  int started;
  int release;
  int completed;
  int block;
  int owner;
  int lua_status;
  int lua_value;
  int self_stop_status;
} test_command_context_t;

typedef struct test_stop_context {
  turbo_lua_worker_t *worker;
  int status;
} test_stop_context_t;

static int test_worker_start(struct lua_State *L,
                             turbo_lua_executor_t *executor, void *context) {
  test_lifecycle_context_t *test = (test_lifecycle_context_t *)context;
  test->L = L;
  test->executor = executor;
  test->start_owner = turbo_lua_executor_is_owner(executor);
  if (luaL_dostring(L, "managed_value = 40") != LUA_OK) return TURBO_EIO;
  return test->start_status;
}

static int test_worker_stop(struct lua_State *L,
                            turbo_lua_executor_t *executor, void *context) {
  test_lifecycle_context_t *test = (test_lifecycle_context_t *)context;
  test->stop_owner = turbo_lua_executor_is_owner(executor);
  ++test->stop_calls;
  lua_getglobal(L, "managed_value");
  test->stopped_lua_value = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  return test->stop_status;
}

static void test_command_init(test_command_context_t *command,
                              test_lifecycle_context_t *lifecycle) {
  *command = (test_command_context_t){0};
  command->lifecycle = lifecycle;
  command->lua_status = TURBO_EIO;
  command->self_stop_status = TURBO_EIO;
  turbo_mutex_init(&command->lock);
  turbo_cond_init(&command->changed);
}

static void test_command_clear(test_command_context_t *command) {
  turbo_cond_destroy(&command->changed);
  turbo_mutex_destroy(&command->lock);
}

static void test_command_wait_started(test_command_context_t *command) {
  turbo_mutex_lock(&command->lock);
  while (!command->started)
    turbo_cond_wait(&command->changed, &command->lock);
  turbo_mutex_unlock(&command->lock);
}

static void test_command_wait_completed(test_command_context_t *command) {
  turbo_mutex_lock(&command->lock);
  while (!command->completed)
    turbo_cond_wait(&command->changed, &command->lock);
  turbo_mutex_unlock(&command->lock);
}

static void test_command_release(test_command_context_t *command) {
  turbo_mutex_lock(&command->lock);
  command->release = 1;
  turbo_cond_broadcast(&command->changed);
  turbo_mutex_unlock(&command->lock);
}

static void test_worker_dispatch(
    struct lua_State *L, void *context,
    turbo_lua_executor_dispatch_reason_t reason) {
  test_command_context_t *command = (test_command_context_t *)context;
  command->reason = reason;
  command->owner =
      turbo_lua_executor_is_owner(command->lifecycle->executor);
  if (reason == TURBO_LUA_EXECUTOR_EXECUTE) {
    command->lua_status =
        luaL_dostring(L, "managed_value = managed_value + 2");
    lua_getglobal(L, "managed_value");
    command->lua_value = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
  }

  turbo_mutex_lock(&command->lock);
  command->started = 1;
  turbo_cond_broadcast(&command->changed);
  while (command->block && !command->release)
    turbo_cond_wait(&command->changed, &command->lock);
  command->completed = 1;
  turbo_cond_broadcast(&command->changed);
  turbo_mutex_unlock(&command->lock);
}

static void test_self_stop_dispatch(
    struct lua_State *L, void *context,
    turbo_lua_executor_dispatch_reason_t reason) {
  test_command_context_t *command = (test_command_context_t *)context;
  (void)L;
  command->reason = reason;
  command->self_stop_status = turbo_lua_worker_stop(
      command->worker, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL);
  turbo_mutex_lock(&command->lock);
  command->started = 1;
  command->completed = 1;
  turbo_cond_broadcast(&command->changed);
  turbo_mutex_unlock(&command->lock);
}

static void test_stop_worker(void *context) {
  test_stop_context_t *stop = (test_stop_context_t *)context;
  stop->status = turbo_lua_worker_stop(
      stop->worker, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL);
}

spec("managed Lua worker") {
  static turbo_lua_worker_t *worker = NULL;

  before_each() { worker = NULL; }

  after_each() {
    check_int_eq(turbo_lua_worker_destroy(worker), TURBO_OK);
    worker = NULL;
  }

  it("should execute Lua commands only on its dedicated owner thread") {
    test_lifecycle_context_t lifecycle = {0};
    test_command_context_t command;
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;
    turbo_lua_executor_command_t posted;
    turbo_lua_executor_stats_t stats = {0};

    test_command_init(&command, &lifecycle);
    config.on_start = test_worker_start;
    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_OK);
    check_true(turbo_lua_worker_is_running(worker));
    check_true(lifecycle.start_owner);
    check_false(turbo_lua_executor_is_owner(lifecycle.executor));

    posted.dispatch = test_worker_dispatch;
    posted.context = &command;
    check_int_eq(turbo_lua_worker_try_post(worker, &posted), TURBO_OK);
    test_command_wait_completed(&command);
    check_int_eq(command.reason, TURBO_LUA_EXECUTOR_EXECUTE);
    check_true(command.owner);
    check_int_eq(command.lua_status, LUA_OK);
    check_int_eq(command.lua_value, 42);

    check_int_eq(turbo_lua_worker_stop(
                     worker, TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN),
                 TURBO_OK);
    check_false(turbo_lua_worker_is_running(worker));
    check_true(lifecycle.stop_owner);
    check_int_eq(lifecycle.stop_calls, 1);
    check_int_eq(lifecycle.stopped_lua_value, 42);
    check_int_eq(turbo_lua_worker_try_post(worker, &posted), TURBO_ECANCELED);
    check_int_eq(turbo_lua_worker_get_stats(worker, &stats), TURBO_OK);
    check_uint_eq(stats.posted_commands, 1u);
    check_uint_eq(stats.executed_commands, 1u);
    test_command_clear(&command);
  }

  it("should expose bounded backpressure while its owner is busy") {
    test_lifecycle_context_t lifecycle = {0};
    test_command_context_t blocker;
    test_command_context_t rejected;
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;
    turbo_lua_executor_command_t command;

    test_command_init(&blocker, &lifecycle);
    test_command_init(&rejected, &lifecycle);
    blocker.block = 1;
    config.queue_capacity = 1u;
    config.on_start = test_worker_start;
    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_OK);

    command.dispatch = test_worker_dispatch;
    command.context = &blocker;
    check_int_eq(turbo_lua_worker_try_post(worker, &command), TURBO_OK);
    test_command_wait_started(&blocker);
    command.context = &rejected;
    check_int_eq(turbo_lua_worker_try_post(worker, &command), TURBO_ENOSPC);
    test_command_release(&blocker);
    test_command_wait_completed(&blocker);

    check_int_eq(turbo_lua_worker_stop(
                     worker, TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN),
                 TURBO_OK);
    test_command_clear(&rejected);
    test_command_clear(&blocker);
  }

  it("should cancel queued commands after an explicit stop request") {
    test_lifecycle_context_t lifecycle = {0};
    test_command_context_t blocker;
    test_command_context_t queued;
    test_stop_context_t stop = {0};
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;
    turbo_lua_executor_command_t command;
    turbo_thread_t stop_thread = NULL;

    test_command_init(&blocker, &lifecycle);
    test_command_init(&queued, &lifecycle);
    blocker.block = 1;
    config.queue_capacity = 2u;
    config.on_start = test_worker_start;
    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_OK);

    command.dispatch = test_worker_dispatch;
    command.context = &blocker;
    check_int_eq(turbo_lua_worker_try_post(worker, &command), TURBO_OK);
    test_command_wait_started(&blocker);
    command.context = &queued;
    check_int_eq(turbo_lua_worker_try_post(worker, &command), TURBO_OK);

    stop.worker = worker;
    stop.status = TURBO_EIO;
    check_int_eq(turbo_thread_create(&stop_thread, test_stop_worker, &stop),
                 TURBO_OK);
    while (turbo_lua_worker_is_running(worker)) turbo_thread_yield();
    test_command_release(&blocker);
    check_int_eq(turbo_thread_join(&stop_thread), TURBO_OK);
    test_command_wait_completed(&queued);

    check_int_eq(stop.status, TURBO_OK);
    check_int_eq(blocker.reason, TURBO_LUA_EXECUTOR_EXECUTE);
    check_int_eq(queued.reason, TURBO_LUA_EXECUTOR_CANCEL);
    check_true(lifecycle.stop_owner);
    test_command_clear(&queued);
    test_command_clear(&blocker);
  }

  it("should reject a blocking stop from the Lua owner thread") {
    test_lifecycle_context_t lifecycle = {0};
    test_command_context_t command;
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;
    turbo_lua_executor_command_t posted;

    test_command_init(&command, &lifecycle);
    config.on_start = test_worker_start;
    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_OK);
    command.worker = worker;
    posted.dispatch = test_self_stop_dispatch;
    posted.context = &command;
    check_int_eq(turbo_lua_worker_try_post(worker, &posted), TURBO_OK);
    test_command_wait_completed(&command);
    check_int_eq(command.self_stop_status, TURBO_EPERM);
    check_true(turbo_lua_worker_is_running(worker));
    check_int_eq(turbo_lua_worker_stop(
                     worker, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL),
                 TURBO_OK);
    test_command_clear(&command);
  }

  it("should clean up partial owner state when startup fails") {
    test_lifecycle_context_t lifecycle = {0};
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;

    lifecycle.start_status = TURBO_EIO;
    config.on_start = test_worker_start;
    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_EIO);
    check_null(worker);
    check_true(lifecycle.start_owner);
    check_true(lifecycle.stop_owner);
    check_int_eq(lifecycle.stop_calls, 1);
  }

  it("should run an owner cleanup hook without a startup hook") {
    test_lifecycle_context_t lifecycle = {0};
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;

    config.on_stop = test_worker_stop;
    config.context = &lifecycle;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_OK);
    check_int_eq(turbo_lua_worker_stop(
                     worker, TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN),
                 TURBO_OK);
    check_true(lifecycle.stop_owner);
    check_int_eq(lifecycle.stop_calls, 1);
  }

  it("should reject invalid bounded queue and polling configuration") {
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;

    config.queue_capacity = 3u;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_EINVAL);
    check_null(worker);

    config = (turbo_lua_worker_config_t)TURBO_LUA_WORKER_CONFIG_DEFAULT;
    config.poll_batch_size = 0u;
    check_int_eq(turbo_lua_worker_create(&worker, &config), TURBO_EINVAL);
    check_null(worker);
  }
}
