#include "turbo_lua_worker.h"

#include "lauxlib.h"
#include "lualib.h"
#include "turbo_error.h"
#include "turbo_thread.h"

#include <stdint.h>
#include <stdlib.h>

typedef enum turbo_lua_worker_state {
  TURBO_LUA_WORKER_STARTING = 0,
  TURBO_LUA_WORKER_RUNNING,
  TURBO_LUA_WORKER_STOPPING,
  TURBO_LUA_WORKER_STOPPED
} turbo_lua_worker_state_t;

struct turbo_lua_worker {
  turbo_mutex_t lock;
  turbo_cond_t changed;
  turbo_thread_t thread;
  turbo_lua_worker_config_t config;
  struct lua_State *L;
  turbo_lua_executor_t *executor;
  turbo_lua_executor_stats_t final_stats;
  turbo_lua_worker_state_t state;
  turbo_lua_executor_shutdown_mode_t shutdown_mode;
  uint64_t wake_sequence;
  int startup_complete;
  int startup_status;
  int run_status;
  int stop_requested;
  int thread_started;
  int joined;
  int has_final_stats;
};

static void turbo_lua_worker_wake(void *context) {
  turbo_lua_worker_t *worker = (turbo_lua_worker_t *)context;
  turbo_mutex_lock(&worker->lock);
  ++worker->wake_sequence;
  turbo_cond_signal(&worker->changed);
  turbo_mutex_unlock(&worker->lock);
}

static void turbo_lua_worker_publish_startup(turbo_lua_worker_t *worker,
                                             int status) {
  turbo_mutex_lock(&worker->lock);
  worker->startup_status = status;
  worker->startup_complete = 1;
  if (status == TURBO_OK) {
    worker->state = TURBO_LUA_WORKER_RUNNING;
  } else {
    worker->state = TURBO_LUA_WORKER_STOPPING;
    worker->stop_requested = 1;
    worker->shutdown_mode = TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL;
  }
  turbo_cond_broadcast(&worker->changed);
  turbo_mutex_unlock(&worker->lock);
}

static int turbo_lua_worker_first_error(int current, int candidate) {
  return current == TURBO_OK ? candidate : current;
}

static void turbo_lua_worker_entry(void *context) {
  turbo_lua_worker_t *worker = (turbo_lua_worker_t *)context;
  turbo_lua_executor_config_t executor_config =
      TURBO_LUA_EXECUTOR_CONFIG_DEFAULT;
  struct lua_State *L = NULL;
  turbo_lua_executor_t *executor = NULL;
  turbo_lua_executor_shutdown_mode_t shutdown_mode =
      TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL;
  turbo_lua_executor_stats_t final_stats = {0};
  int lifecycle_ready = 0;
  int has_final_stats = 0;
  int status = TURBO_OK;

  L = luaL_newstate();
  if (L == NULL) {
    turbo_lua_worker_publish_startup(worker, TURBO_ENOMEM);
    status = TURBO_ENOMEM;
    goto finish;
  }
  if (worker->config.open_standard_libraries) luaL_openlibs(L);

  executor_config.queue_capacity = worker->config.queue_capacity;
  executor_config.wake = turbo_lua_worker_wake;
  executor_config.wake_context = worker;
  status = turbo_lua_executor_create(&executor, L, &executor_config);
  if (status != TURBO_OK) {
    turbo_lua_worker_publish_startup(worker, status);
    goto finish;
  }

  turbo_mutex_lock(&worker->lock);
  worker->L = L;
  worker->executor = executor;
  turbo_mutex_unlock(&worker->lock);
  lifecycle_ready = 1;

  if (worker->config.on_start != NULL) {
    status = worker->config.on_start(L, executor, worker->config.context);
  }
  turbo_lua_worker_publish_startup(worker, status);

  while (status == TURBO_OK) {
    size_t processed = 0u;
    uint64_t observed_wake;
    int should_stop = 0;

    turbo_mutex_lock(&worker->lock);
    if (worker->stop_requested) {
      shutdown_mode = worker->shutdown_mode;
      turbo_mutex_unlock(&worker->lock);
      break;
    }
    observed_wake = worker->wake_sequence;
    turbo_mutex_unlock(&worker->lock);

    for (size_t index = 0u; index < worker->config.poll_batch_size; ++index) {
      size_t one_processed = 0u;
      status = turbo_lua_executor_poll(executor, 1u, &one_processed);
      if (status != TURBO_OK) break;
      processed += one_processed;
      if (one_processed == 0u) break;

      turbo_mutex_lock(&worker->lock);
      should_stop = worker->stop_requested;
      if (should_stop) shutdown_mode = worker->shutdown_mode;
      turbo_mutex_unlock(&worker->lock);
      if (should_stop) break;
    }
    if (status != TURBO_OK || should_stop) break;
    if (processed == worker->config.poll_batch_size) turbo_thread_yield();

    turbo_mutex_lock(&worker->lock);
    while (!worker->stop_requested && processed == 0u &&
           observed_wake == worker->wake_sequence) {
      turbo_cond_wait(&worker->changed, &worker->lock);
    }
    turbo_mutex_unlock(&worker->lock);
  }

  if (lifecycle_ready && worker->config.on_stop != NULL) {
    int callback_status =
        worker->config.on_stop(L, executor, worker->config.context);
    status = turbo_lua_worker_first_error(status, callback_status);
  }
  if (executor != NULL) {
    size_t processed = 0u;
    int shutdown_status =
        turbo_lua_executor_shutdown(executor, shutdown_mode, &processed);
    status = turbo_lua_worker_first_error(status, shutdown_status);
    has_final_stats =
        turbo_lua_executor_get_stats(executor, &final_stats) == TURBO_OK;
    status = turbo_lua_worker_first_error(
        status, turbo_lua_executor_destroy(executor));
    executor = NULL;
  }

finish:
  if (L != NULL) lua_close(L);
  turbo_mutex_lock(&worker->lock);
  worker->L = NULL;
  worker->executor = NULL;
  if (has_final_stats) worker->final_stats = final_stats;
  worker->has_final_stats = has_final_stats;
  worker->run_status = status;
  worker->state = TURBO_LUA_WORKER_STOPPED;
  turbo_cond_broadcast(&worker->changed);
  turbo_mutex_unlock(&worker->lock);
}

static void turbo_lua_worker_release(turbo_lua_worker_t *worker) {
  turbo_cond_destroy(&worker->changed);
  turbo_mutex_destroy(&worker->lock);
  free(worker);
}

int turbo_lua_worker_create(turbo_lua_worker_t **out,
                            const turbo_lua_worker_config_t *config) {
  turbo_lua_worker_config_t effective =
      (turbo_lua_worker_config_t)TURBO_LUA_WORKER_CONFIG_DEFAULT;
  turbo_lua_worker_t *worker;
  int thread_status;
  int startup_status;

  if (out == NULL || *out != NULL) return TURBO_EINVAL;
  if (config != NULL) effective = *config;
  if (effective.queue_capacity == 0u ||
      (effective.queue_capacity & (effective.queue_capacity - 1u)) != 0u ||
      effective.poll_batch_size == 0u) {
    return TURBO_EINVAL;
  }

  worker = (turbo_lua_worker_t *)calloc(1u, sizeof(*worker));
  if (worker == NULL) return TURBO_ENOMEM;
  worker->config = effective;
  worker->state = TURBO_LUA_WORKER_STARTING;
  worker->shutdown_mode = TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL;
  worker->startup_status = TURBO_EIO;
  worker->run_status = TURBO_EIO;
  turbo_mutex_init(&worker->lock);
  turbo_cond_init(&worker->changed);

  thread_status =
      turbo_thread_create(&worker->thread, turbo_lua_worker_entry, worker);
  if (thread_status != TURBO_OK) {
    turbo_lua_worker_release(worker);
    return thread_status;
  }
  worker->thread_started = 1;

  turbo_mutex_lock(&worker->lock);
  while (!worker->startup_complete)
    turbo_cond_wait(&worker->changed, &worker->lock);
  startup_status = worker->startup_status;
  turbo_mutex_unlock(&worker->lock);

  if (startup_status != TURBO_OK) {
    if (turbo_thread_join(&worker->thread) != TURBO_OK) {
      turbo_mutex_lock(&worker->lock);
      while (worker->state != TURBO_LUA_WORKER_STOPPED)
        turbo_cond_wait(&worker->changed, &worker->lock);
      turbo_mutex_unlock(&worker->lock);
      turbo_thread_destroy(&worker->thread);
    }
    worker->joined = 1;
    turbo_lua_worker_release(worker);
    return startup_status;
  }

  *out = worker;
  return TURBO_OK;
}

int turbo_lua_worker_try_post(turbo_lua_worker_t *worker,
                              const turbo_lua_executor_command_t *command) {
  turbo_lua_executor_t *executor;
  if (worker == NULL || command == NULL || command->dispatch == NULL)
    return TURBO_EINVAL;

  turbo_mutex_lock(&worker->lock);
  if (worker->state != TURBO_LUA_WORKER_RUNNING || worker->executor == NULL) {
    turbo_mutex_unlock(&worker->lock);
    return TURBO_ECANCELED;
  }
  executor = worker->executor;
  turbo_mutex_unlock(&worker->lock);

  return turbo_lua_executor_try_post(executor, command);
}

int turbo_lua_worker_stop(turbo_lua_worker_t *worker,
                          turbo_lua_executor_shutdown_mode_t mode) {
  int join_status = TURBO_OK;
  int run_status;

  if (worker == NULL ||
      (mode != TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN &&
       mode != TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL)) {
    return TURBO_EINVAL;
  }

  turbo_mutex_lock(&worker->lock);
  if (worker->executor != NULL &&
      turbo_lua_executor_is_owner(worker->executor)) {
    turbo_mutex_unlock(&worker->lock);
    return TURBO_EPERM;
  }
  if (!worker->stop_requested) {
    worker->stop_requested = 1;
    worker->shutdown_mode = mode;
    if (worker->state == TURBO_LUA_WORKER_RUNNING)
      worker->state = TURBO_LUA_WORKER_STOPPING;
    turbo_cond_broadcast(&worker->changed);
  }
  turbo_mutex_unlock(&worker->lock);

  if (worker->thread_started && !worker->joined) {
    join_status = turbo_thread_join(&worker->thread);
    if (join_status == TURBO_OK) worker->joined = 1;
  }

  turbo_mutex_lock(&worker->lock);
  run_status = worker->run_status;
  turbo_mutex_unlock(&worker->lock);
  return join_status != TURBO_OK ? join_status : run_status;
}

int turbo_lua_worker_destroy(turbo_lua_worker_t *worker) {
  int status;
  int joined;
  if (worker == NULL) return TURBO_OK;

  status = turbo_lua_worker_stop(worker,
                                 TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL);
  turbo_mutex_lock(&worker->lock);
  joined = worker->joined;
  turbo_mutex_unlock(&worker->lock);
  if (!joined) return status;
  turbo_lua_worker_release(worker);
  return status;
}

int turbo_lua_worker_is_running(turbo_lua_worker_t *worker) {
  int running;
  if (worker == NULL) return 0;
  turbo_mutex_lock(&worker->lock);
  running = worker->state == TURBO_LUA_WORKER_RUNNING;
  turbo_mutex_unlock(&worker->lock);
  return running;
}

int turbo_lua_worker_get_stats(turbo_lua_worker_t *worker,
                               turbo_lua_executor_stats_t *out) {
  int status;
  if (worker == NULL || out == NULL) return TURBO_EINVAL;

  turbo_mutex_lock(&worker->lock);
  if (worker->executor != NULL) {
    status = turbo_lua_executor_get_stats(worker->executor, out);
  } else if (worker->has_final_stats) {
    *out = worker->final_stats;
    status = TURBO_OK;
  } else {
    status = TURBO_ECANCELED;
  }
  turbo_mutex_unlock(&worker->lock);
  return status;
}
