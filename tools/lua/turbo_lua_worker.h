/**
 * @file turbo_lua_worker.h
 * @brief Managed dedicated-thread owner for one Lua state and executor.
 */
#ifndef TURBO_LUA_WORKER_H
#define TURBO_LUA_WORKER_H

#include "turbo_lua_executor.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_lua_worker turbo_lua_worker_t;

/**
 * Owner-thread lifecycle callback.
 *
 * on_start may create registry references and schema clients with executor.
 * on_stop releases those resources before queued commands are drained or
 * canceled. Once the executor exists, a configured on_stop is invoked even if
 * on_start is absent or fails, so context must track partial initialization.
 */
typedef int (*turbo_lua_worker_lifecycle_fn)(struct lua_State *L,
                                             turbo_lua_executor_t *executor,
                                             void *context);

typedef struct turbo_lua_worker_config {
  /** Fixed command capacity; must be a nonzero power of two. */
  size_t queue_capacity;
  /** Maximum commands per scheduling batch; stop is checked after each one. */
  size_t poll_batch_size;
  /** Nonzero opens Lua standard libraries before on_start. */
  int open_standard_libraries;
  /** Optional owner-thread initialization callback. */
  turbo_lua_worker_lifecycle_fn on_start;
  /** Optional owner-thread cleanup callback. */
  turbo_lua_worker_lifecycle_fn on_stop;
  /** Borrowed until turbo_lua_worker_stop() or destroy() completes. */
  void *context;
} turbo_lua_worker_config_t;

#define TURBO_LUA_WORKER_DEFAULT_POLL_BATCH ((size_t)64u)
#define TURBO_LUA_WORKER_CONFIG_DEFAULT                                      \
  {                                                                          \
    TURBO_LUA_EXECUTOR_DEFAULT_CAPACITY, TURBO_LUA_WORKER_DEFAULT_POLL_BATCH, \
        1, NULL, NULL, NULL                                                   \
  }

/**
 * Start a dedicated thread, create its Lua state/executor, and run on_start.
 *
 * This call waits for the startup handshake. out must point to NULL and is
 * unchanged on failure. The configuration is copied; context remains borrowed.
 * No Lua state or executor pointer is exposed to producer threads.
 *
 * @param out Receives the worker only after startup succeeds.
 * @param config Copied configuration; NULL selects the defaults.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOMEM, a thread startup error, or the
 *         nonzero status returned by on_start.
 */
int turbo_lua_worker_create(turbo_lua_worker_t **out,
                            const turbo_lua_worker_config_t *config);

/**
 * Try to transfer one command to the worker's bounded MPSC executor.
 *
 * Success transfers command context ownership to its terminal dispatch.
 * Failure leaves ownership with the caller. Producers may call concurrently
 * while the worker is running, but must be quiescent before stop/destroy.
 *
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOSPC, TURBO_ECANCELED, or TURBO_EIO.
 */
int turbo_lua_worker_try_post(turbo_lua_worker_t *worker,
                              const turbo_lua_executor_command_t *command);

/**
 * Stop accepting work, join the dedicated thread, and finish owner cleanup.
 *
 * The first call selects DRAIN or CANCEL. This call is idempotent and blocks
 * until on_stop, terminal command dispatch, executor destruction, and
 * lua_close() complete. It must not run on the Lua owner thread or concurrently
 * with producer/control calls.
 *
 * @param mode DRAIN executes accepted commands; CANCEL terminally cancels them.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_EPERM, a join error, or a nonzero
 *         executor/on_stop status.
 */
int turbo_lua_worker_stop(turbo_lua_worker_t *worker,
                          turbo_lua_executor_shutdown_mode_t mode);

/**
 * Stop with CANCEL when still running, then release the worker handle.
 * NULL is accepted. A call from the owner returns TURBO_EPERM without releasing
 * the live handle. Once joined, the handle is released even if cleanup failed.
 *
 * @return The stop/cleanup status, or TURBO_OK for NULL.
 */
int turbo_lua_worker_destroy(turbo_lua_worker_t *worker);

/**
 * Return nonzero while the worker accepts submissions.
 * @return 0 for NULL, starting, stopping, or stopped workers.
 */
int turbo_lua_worker_is_running(turbo_lua_worker_t *worker);

/**
 * Snapshot live executor counters or the final counters after stop.
 * @return TURBO_OK, TURBO_EINVAL, or the executor statistics error.
 */
int turbo_lua_worker_get_stats(turbo_lua_worker_t *worker,
                               turbo_lua_executor_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LUA_WORKER_H */
