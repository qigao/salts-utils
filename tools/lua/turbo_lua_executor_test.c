#include "turbo_lua_executor.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "tinytest.h"
#include "turbo_error.h"
#include "turbo_thread.h"

#include <stdatomic.h>
#include <stddef.h>

enum { TEST_EXECUTOR_CAPACITY = 64, TEST_PRODUCER_COUNT = 2, TEST_COMMANDS_PER_PRODUCER = 16 };

typedef struct test_command_context {
  atomic_int *executed;
  atomic_int *canceled;
} test_command_context_t;

typedef struct test_producer_context {
  turbo_lua_executor_t *executor;
  test_command_context_t *commands;
  size_t command_count;
  atomic_int result;
} test_producer_context_t;

typedef struct test_non_owner_context {
  turbo_lua_executor_t *executor;
  int is_owner;
  int state_status;
  int poll_status;
  int shutdown_status;
  int destroy_status;
  struct lua_State *state;
  size_t polled;
  size_t shutdown_processed;
} test_non_owner_context_t;

static void test_dispatch(struct lua_State *L, void *context,
                          turbo_lua_executor_dispatch_reason_t reason) {
  test_command_context_t *command = (test_command_context_t *)context;

  if (reason == TURBO_LUA_EXECUTOR_CANCEL) {
    atomic_fetch_add_explicit(command->canceled, 1, memory_order_relaxed);
    return;
  }
  lua_getglobal(L, "executor_count");
  lua_pushinteger(L, lua_tointeger(L, -1) + 1);
  lua_setglobal(L, "executor_count");
  lua_pop(L, 1);
  atomic_fetch_add_explicit(command->executed, 1, memory_order_relaxed);
}

static void test_producer(void *context) {
  test_producer_context_t *producer = (test_producer_context_t *)context;

  for (size_t index = 0u; index < producer->command_count; ++index) {
    turbo_lua_executor_command_t command = {test_dispatch, &producer->commands[index]};
    int status = turbo_lua_executor_try_post(producer->executor, &command);
    if (status != TURBO_OK) {
      atomic_store_explicit(&producer->result, status, memory_order_relaxed);
      return;
    }
  }
  atomic_store_explicit(&producer->result, TURBO_OK, memory_order_relaxed);
}

static void test_wake(void *context) {
  atomic_int *wake_count = (atomic_int *)context;
  atomic_fetch_add_explicit(wake_count, 1, memory_order_relaxed);
}

static void test_non_owner_controls(void *context) {
  test_non_owner_context_t *test = (test_non_owner_context_t *)context;
  test->is_owner = turbo_lua_executor_is_owner(test->executor);
  test->state_status = turbo_lua_executor_owner_state(test->executor, &test->state);
  test->poll_status = turbo_lua_executor_poll(test->executor, 1u, &test->polled);
  test->shutdown_status = turbo_lua_executor_shutdown(
      test->executor, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL,
      &test->shutdown_processed);
  test->destroy_status = turbo_lua_executor_destroy(test->executor);
}

spec("Lua executor") {
  static lua_State *L;
  static turbo_lua_executor_t *executor;

  before_each() {
    L = luaL_newstate();
    executor = NULL;
    check_not_null(L);
    if (L != NULL) {
      lua_pushinteger(L, 0);
      lua_setglobal(L, "executor_count");
    }
  }

  after_each() {
    check_int_eq(turbo_lua_executor_destroy(executor), TURBO_OK);
    executor = NULL;
    if (L != NULL) lua_close(L);
    L = NULL;
  }

  it("should reject invalid capacities without publishing an executor") {
    turbo_lua_executor_config_t config = {3u, NULL, NULL};

    check_int_eq(turbo_lua_executor_create(&executor, L, &config), TURBO_EINVAL);
    check_null(executor);
  }

  it("should execute posted commands in bounded poll batches") {
    atomic_int executed;
    atomic_int canceled;
    test_command_context_t contexts[3];
    turbo_lua_executor_stats_t stats;
    size_t processed = 0u;

    atomic_init(&executed, 0);
    atomic_init(&canceled, 0);
    check_int_eq(turbo_lua_executor_create(&executor, L, NULL), TURBO_OK);
    check_true(turbo_lua_executor_is_owner(executor));
    {
      struct lua_State *borrowed = NULL;
      check_int_eq(turbo_lua_executor_owner_state(executor, &borrowed), TURBO_OK);
      check_ptr_eq(borrowed, L);
    }
    for (size_t index = 0u; index < 3u; ++index) {
      turbo_lua_executor_command_t command;
      contexts[index].executed = &executed;
      contexts[index].canceled = &canceled;
      command.dispatch = test_dispatch;
      command.context = &contexts[index];
      check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_OK);
    }

    check_int_eq(turbo_lua_executor_poll(executor, 2u, &processed), TURBO_OK);
    check_size_eq(processed, 2u);
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed), 2);
    check_int_eq(turbo_lua_executor_poll(executor, 2u, &processed), TURBO_OK);
    check_size_eq(processed, 1u);
    lua_getglobal(L, "executor_count");
    check_int_eq((int)lua_tointeger(L, -1), 3);
    lua_pop(L, 1);

    check_int_eq(turbo_lua_executor_get_stats(executor, &stats), TURBO_OK);
    check_size_eq(stats.pending_commands, 0u);
    check_size_eq(stats.peak_pending_commands, 3u);
    check_size_eq(stats.posted_commands, 3u);
    check_size_eq(stats.executed_commands, 3u);
    check_size_eq(stats.canceled_commands, 0u);
  }

  it("should reject overflow and cancel every accepted command at shutdown") {
    turbo_lua_executor_config_t config = {2u, NULL, NULL};
    atomic_int executed;
    atomic_int canceled;
    test_command_context_t contexts[3];
    turbo_lua_executor_command_t command;
    turbo_lua_executor_stats_t stats;
    size_t processed = 0u;

    atomic_init(&executed, 0);
    atomic_init(&canceled, 0);
    check_int_eq(turbo_lua_executor_create(&executor, L, &config), TURBO_OK);
    for (size_t index = 0u; index < 3u; ++index) {
      contexts[index].executed = &executed;
      contexts[index].canceled = &canceled;
    }
    command.dispatch = test_dispatch;
    command.context = &contexts[0];
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_OK);
    command.context = &contexts[1];
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_OK);
    command.context = &contexts[2];
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_ENOSPC);

    check_int_eq(
        turbo_lua_executor_shutdown(executor, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL, &processed),
        TURBO_OK);
    check_size_eq(processed, 2u);
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed), 0);
    check_int_eq(atomic_load_explicit(&canceled, memory_order_relaxed), 2);
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_ECANCELED);
    check_int_eq(turbo_lua_executor_get_stats(executor, &stats), TURBO_OK);
    check_size_eq(stats.rejected_commands, 2u);
    check_false(stats.accepting);
  }

  it("should cancel queued commands when destroyed before Lua closes") {
    atomic_int executed;
    atomic_int canceled;
    test_command_context_t context;
    turbo_lua_executor_command_t command;

    atomic_init(&executed, 0);
    atomic_init(&canceled, 0);
    context.executed = &executed;
    context.canceled = &canceled;
    command.dispatch = test_dispatch;
    command.context = &context;
    check_int_eq(turbo_lua_executor_create(&executor, L, NULL), TURBO_OK);
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_OK);

    check_int_eq(turbo_lua_executor_destroy(executor), TURBO_OK);
    executor = NULL;
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed), 0);
    check_int_eq(atomic_load_explicit(&canceled, memory_order_relaxed), 1);
  }

  it("should reject Lua control and state access from a non-owner thread") {
    test_non_owner_context_t context = {0};
    turbo_thread_t thread = NULL;

    check_int_eq(turbo_lua_executor_create(&executor, L, NULL), TURBO_OK);
    context.executor = executor;
    context.polled = 17u;
    context.shutdown_processed = 19u;
    check_int_eq(turbo_thread_create(&thread, test_non_owner_controls, &context), 0);
    check_int_eq(turbo_thread_join(&thread), 0);

    check_false(context.is_owner);
    check_int_eq(context.state_status, TURBO_EPERM);
    check_null(context.state);
    check_int_eq(context.poll_status, TURBO_EPERM);
    check_size_eq(context.polled, 17u);
    check_int_eq(context.shutdown_status, TURBO_EPERM);
    check_size_eq(context.shutdown_processed, 19u);
    check_int_eq(context.destroy_status, TURBO_EPERM);
    check_true(turbo_lua_executor_is_owner(executor));
  }

  it("should accept multiple producer threads and execute only while polling") {
    atomic_int executed;
    atomic_int canceled;
    test_command_context_t command_contexts[TEST_PRODUCER_COUNT][TEST_COMMANDS_PER_PRODUCER];
    test_producer_context_t producers[TEST_PRODUCER_COUNT];
    turbo_thread_t threads[TEST_PRODUCER_COUNT];
    turbo_lua_executor_config_t config = {TEST_EXECUTOR_CAPACITY, NULL, NULL};
    size_t processed = 0u;

    atomic_init(&executed, 0);
    atomic_init(&canceled, 0);
    check_int_eq(turbo_lua_executor_create(&executor, L, &config), TURBO_OK);
    for (size_t producer = 0u; producer < TEST_PRODUCER_COUNT; ++producer) {
      for (size_t index = 0u; index < TEST_COMMANDS_PER_PRODUCER; ++index) {
        command_contexts[producer][index].executed = &executed;
        command_contexts[producer][index].canceled = &canceled;
      }
      producers[producer].executor = executor;
      producers[producer].commands = command_contexts[producer];
      producers[producer].command_count = TEST_COMMANDS_PER_PRODUCER;
      atomic_init(&producers[producer].result, TURBO_EIO);
      threads[producer] = NULL;
      check_int_eq(turbo_thread_create(&threads[producer], test_producer, &producers[producer]), 0);
    }
    for (size_t producer = 0u; producer < TEST_PRODUCER_COUNT; ++producer) {
      check_int_eq(turbo_thread_join(&threads[producer]), 0);
      check_int_eq(atomic_load_explicit(&producers[producer].result, memory_order_relaxed),
                   TURBO_OK);
    }
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed), 0);

    check_int_eq(turbo_lua_executor_poll(executor, TEST_EXECUTOR_CAPACITY, &processed), TURBO_OK);
    check_size_eq(processed, TEST_PRODUCER_COUNT * TEST_COMMANDS_PER_PRODUCER);
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed),
                 TEST_PRODUCER_COUNT * TEST_COMMANDS_PER_PRODUCER);
    check_int_eq(atomic_load_explicit(&canceled, memory_order_relaxed), 0);
  }

  it("should invoke the wake hook once per accepted command") {
    atomic_int wake_count;
    atomic_int executed;
    atomic_int canceled;
    test_command_context_t context;
    turbo_lua_executor_command_t command;
    turbo_lua_executor_config_t config;
    size_t processed = 0u;

    atomic_init(&wake_count, 0);
    atomic_init(&executed, 0);
    atomic_init(&canceled, 0);
    context.executed = &executed;
    context.canceled = &canceled;
    command.dispatch = test_dispatch;
    command.context = &context;
    config.queue_capacity = 2u;
    config.wake = test_wake;
    config.wake_context = &wake_count;
    check_int_eq(turbo_lua_executor_create(&executor, L, &config), TURBO_OK);
    check_int_eq(turbo_lua_executor_try_post(executor, &command), TURBO_OK);
    check_int_eq(atomic_load_explicit(&wake_count, memory_order_relaxed), 1);
    check_int_eq(
        turbo_lua_executor_shutdown(executor, TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN, &processed),
        TURBO_OK);
    check_size_eq(processed, 1u);
    check_int_eq(atomic_load_explicit(&executed, memory_order_relaxed), 1);
  }
}
