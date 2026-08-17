#include "turbo_lua_executor.h"

#include "disruptor.h"
#include "turbo_error.h"
#include "turbo_thread.h"

#include <stdatomic.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
typedef DWORD turbo_lua_owner_id_t;
#else
#include <pthread.h>
typedef pthread_t turbo_lua_owner_id_t;
#endif

typedef struct turbo_lua_executor_entry {
  turbo_lua_executor_command_t command;
} turbo_lua_executor_entry_t;

struct turbo_lua_executor {
  struct lua_State *L;
  turbo_lua_owner_id_t owner_id;
  disruptor_t *queue;
  turbo_mutex_t control_lock;
  turbo_lua_executor_wake_fn wake;
  void *wake_context;
  size_t queue_capacity;
  int accepting;
  atomic_size_t pending_commands;
  atomic_size_t peak_pending_commands;
  atomic_uint_least64_t posted_commands;
  atomic_uint_least64_t executed_commands;
  atomic_uint_least64_t canceled_commands;
  atomic_uint_least64_t rejected_commands;
};

static turbo_lua_owner_id_t turbo_lua_executor_current_owner_id(void) {
#ifdef _WIN32
  return GetCurrentThreadId();
#else
  return pthread_self();
#endif
}

static int turbo_lua_executor_owner_id_equal(turbo_lua_owner_id_t left,
                                             turbo_lua_owner_id_t right) {
#ifdef _WIN32
  return left == right;
#else
  return pthread_equal(left, right) != 0;
#endif
}

static int turbo_lua_executor_is_power_of_two(size_t value) {
  return value != 0u && (value & (value - 1u)) == 0u;
}

static void turbo_lua_executor_update_peak(turbo_lua_executor_t *executor, size_t candidate) {
  size_t peak = atomic_load_explicit(&executor->peak_pending_commands, memory_order_relaxed);
  while (peak < candidate &&
         !atomic_compare_exchange_weak_explicit(&executor->peak_pending_commands, &peak, candidate,
                                                memory_order_relaxed, memory_order_relaxed)) {
  }
}

static size_t turbo_lua_executor_dispatch_pending(turbo_lua_executor_t *executor,
                                                  turbo_lua_executor_dispatch_reason_t reason,
                                                  size_t max_commands) {
  size_t processed = 0u;
  disruptor_cursor_t cursor = {0};

  while (processed < max_commands && disruptor_worker_try_claim(executor->queue, &cursor)) {
    const turbo_lua_executor_entry_t *entry =
        (const turbo_lua_executor_entry_t *)disruptor_show_entry(executor->queue, &cursor);
    turbo_lua_executor_command_t command = entry->command;

    atomic_fetch_sub_explicit(&executor->pending_commands, 1u, memory_order_relaxed);
    command.dispatch(executor->L, command.context, reason);
    disruptor_worker_release_entry(executor->queue, &cursor);
    if (reason == TURBO_LUA_EXECUTOR_EXECUTE) {
      atomic_fetch_add_explicit(&executor->executed_commands, 1u, memory_order_relaxed);
    } else {
      atomic_fetch_add_explicit(&executor->canceled_commands, 1u, memory_order_relaxed);
    }
    ++processed;
  }
  return processed;
}

int turbo_lua_executor_create(turbo_lua_executor_t **out, struct lua_State *L,
                              const turbo_lua_executor_config_t *config) {
  turbo_lua_executor_config_t effective =
      (turbo_lua_executor_config_t)TURBO_LUA_EXECUTOR_CONFIG_DEFAULT;
  turbo_lua_executor_t *executor;
  disruptor_config_t queue_config;

  if (out == NULL || *out != NULL || L == NULL) return TURBO_EINVAL;
  if (config != NULL) effective = *config;
  if (!turbo_lua_executor_is_power_of_two(effective.queue_capacity)) return TURBO_EINVAL;

  executor = (turbo_lua_executor_t *)calloc(1u, sizeof(*executor));
  if (executor == NULL) return TURBO_ENOMEM;

  executor->L = L;
  executor->owner_id = turbo_lua_executor_current_owner_id();
  executor->wake = effective.wake;
  executor->wake_context = effective.wake_context;
  executor->queue_capacity = effective.queue_capacity;
  executor->accepting = 1;
  atomic_init(&executor->pending_commands, 0u);
  atomic_init(&executor->peak_pending_commands, 0u);
  atomic_init(&executor->posted_commands, 0u);
  atomic_init(&executor->executed_commands, 0u);
  atomic_init(&executor->canceled_commands, 0u);
  atomic_init(&executor->rejected_commands, 0u);
  turbo_mutex_init(&executor->control_lock);

  queue_config.entry_size = sizeof(turbo_lua_executor_entry_t);
  queue_config.capacity = (uint64_t)effective.queue_capacity;
  queue_config.consumer_capacity = 1u;
  queue_config.mode = DISRUPTOR_MODE_WORKER_POOL;
  executor->queue = disruptor_create(&queue_config);
  if (executor->queue == NULL) {
    turbo_mutex_destroy(&executor->control_lock);
    free(executor);
    return TURBO_ENOMEM;
  }

  *out = executor;
  return TURBO_OK;
}

int turbo_lua_executor_try_post(turbo_lua_executor_t *executor,
                                const turbo_lua_executor_command_t *command) {
  disruptor_cursor_t cursor = {0};
  turbo_lua_executor_entry_t *entry;
  size_t pending;
  int publish_status;

  if (executor == NULL || command == NULL || command->dispatch == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&executor->control_lock);
  if (!executor->accepting) {
    atomic_fetch_add_explicit(&executor->rejected_commands, 1u, memory_order_relaxed);
    turbo_mutex_unlock(&executor->control_lock);
    return TURBO_ECANCELED;
  }
  if (!disruptor_publisher_try_claim(executor->queue, &cursor)) {
    atomic_fetch_add_explicit(&executor->rejected_commands, 1u, memory_order_relaxed);
    turbo_mutex_unlock(&executor->control_lock);
    return TURBO_ENOSPC;
  }

  entry = (turbo_lua_executor_entry_t *)disruptor_acquire_entry(executor->queue, &cursor);
  entry->command = *command;
  pending = atomic_fetch_add_explicit(&executor->pending_commands, 1u, memory_order_relaxed) + 1u;
  publish_status = disruptor_publisher_publish(executor->queue, &cursor);
  if (!publish_status) {
    atomic_fetch_sub_explicit(&executor->pending_commands, 1u, memory_order_relaxed);
    atomic_fetch_add_explicit(&executor->rejected_commands, 1u, memory_order_relaxed);
    executor->accepting = 0;
    turbo_mutex_unlock(&executor->control_lock);
    return TURBO_EIO;
  }
  turbo_lua_executor_update_peak(executor, pending);
  atomic_fetch_add_explicit(&executor->posted_commands, 1u, memory_order_relaxed);
  turbo_mutex_unlock(&executor->control_lock);

  if (executor->wake != NULL) executor->wake(executor->wake_context);
  return TURBO_OK;
}

int turbo_lua_executor_poll(turbo_lua_executor_t *executor, size_t max_commands,
                            size_t *out_processed) {
  if (executor == NULL || max_commands == 0u || out_processed == NULL) return TURBO_EINVAL;
  if (!turbo_lua_executor_is_owner(executor)) return TURBO_EPERM;
  *out_processed =
      turbo_lua_executor_dispatch_pending(executor, TURBO_LUA_EXECUTOR_EXECUTE, max_commands);
  return TURBO_OK;
}

int turbo_lua_executor_shutdown(turbo_lua_executor_t *executor,
                                turbo_lua_executor_shutdown_mode_t mode, size_t *out_processed) {
  turbo_lua_executor_dispatch_reason_t reason;

  if (executor == NULL || out_processed == NULL ||
      (mode != TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN && mode != TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL)) {
    return TURBO_EINVAL;
  }
  if (!turbo_lua_executor_is_owner(executor)) return TURBO_EPERM;

  reason = mode == TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN ? TURBO_LUA_EXECUTOR_EXECUTE
                                                     : TURBO_LUA_EXECUTOR_CANCEL;
  turbo_mutex_lock(&executor->control_lock);
  executor->accepting = 0;
  turbo_mutex_unlock(&executor->control_lock);

  *out_processed = turbo_lua_executor_dispatch_pending(executor, reason, SIZE_MAX);
  return TURBO_OK;
}

int turbo_lua_executor_destroy(turbo_lua_executor_t *executor) {
  size_t canceled = 0u;
  int status;

  if (executor == NULL) return TURBO_OK;
  if (!turbo_lua_executor_is_owner(executor)) return TURBO_EPERM;
  status = turbo_lua_executor_shutdown(executor, TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL, &canceled);
  if (status != TURBO_OK) return status;
  disruptor_destroy(executor->queue);
  turbo_mutex_destroy(&executor->control_lock);
  free(executor);
  return TURBO_OK;
}

int turbo_lua_executor_get_stats(turbo_lua_executor_t *executor, turbo_lua_executor_stats_t *out) {
  turbo_lua_executor_stats_t snapshot;

  if (executor == NULL || out == NULL) return TURBO_EINVAL;
  snapshot.queue_capacity = executor->queue_capacity;
  snapshot.pending_commands =
      atomic_load_explicit(&executor->pending_commands, memory_order_relaxed);
  snapshot.peak_pending_commands =
      atomic_load_explicit(&executor->peak_pending_commands, memory_order_relaxed);
  snapshot.posted_commands = atomic_load_explicit(&executor->posted_commands, memory_order_relaxed);
  snapshot.executed_commands =
      atomic_load_explicit(&executor->executed_commands, memory_order_relaxed);
  snapshot.canceled_commands =
      atomic_load_explicit(&executor->canceled_commands, memory_order_relaxed);
  snapshot.rejected_commands =
      atomic_load_explicit(&executor->rejected_commands, memory_order_relaxed);
  turbo_mutex_lock(&executor->control_lock);
  snapshot.accepting = executor->accepting;
  turbo_mutex_unlock(&executor->control_lock);
  *out = snapshot;
  return TURBO_OK;
}

int turbo_lua_executor_is_owner(const turbo_lua_executor_t *executor) {
  return executor != NULL && turbo_lua_executor_owner_id_equal(
                                 executor->owner_id,
                                 turbo_lua_executor_current_owner_id());
}

int turbo_lua_executor_owner_state(const turbo_lua_executor_t *executor,
                                   struct lua_State **out) {
  if (executor == NULL || out == NULL || *out != NULL) return TURBO_EINVAL;
  if (!turbo_lua_executor_is_owner(executor)) return TURBO_EPERM;
  *out = executor->L;
  return TURBO_OK;
}
