/**
 * @file turbo_lua_executor.h
 * @brief Bounded single-owner scheduling for commands that access lua_State.
 *
 * The executor borrows one lua_State and binds its owner identity to the thread
 * that successfully calls create. It does not create an OS thread.
 * Producers may call try_post concurrently, but must never access the borrowed
 * lua_State from their producer threads.
 */
#ifndef TURBO_LUA_EXECUTOR_H
#define TURBO_LUA_EXECUTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct lua_State;

#ifndef TURBO_LUA_EXECUTOR_TYPE_DEFINED
#define TURBO_LUA_EXECUTOR_TYPE_DEFINED
typedef struct turbo_lua_executor turbo_lua_executor_t;
#endif

typedef enum turbo_lua_executor_dispatch_reason {
  TURBO_LUA_EXECUTOR_EXECUTE = 0,
  TURBO_LUA_EXECUTOR_CANCEL = 1
} turbo_lua_executor_dispatch_reason_t;

/**
 * Terminal command callback.
 *
 * A successfully posted command is dispatched exactly once with EXECUTE or
 * CANCEL. The callback owns cleanup of context for that terminal reason. It
 * runs on the executor owner thread and may call try_post(), but it must not
 * call poll/shutdown/destroy recursively or longjmp across the executor.
 */
typedef void (*turbo_lua_executor_dispatch_fn)(struct lua_State *L, void *context,
                                               turbo_lua_executor_dispatch_reason_t reason);

typedef struct turbo_lua_executor_command {
  turbo_lua_executor_dispatch_fn dispatch;
  void *context;
} turbo_lua_executor_command_t;

/** Called on the posting thread after a command becomes visible. Must not access Lua. */
typedef void (*turbo_lua_executor_wake_fn)(void *context);

typedef struct turbo_lua_executor_config {
  /** Fixed command capacity. Must be a nonzero power of two. */
  size_t queue_capacity;
  /** Optional event-loop wake hook; invoked outside executor locks. */
  turbo_lua_executor_wake_fn wake;
  void *wake_context;
} turbo_lua_executor_config_t;

typedef enum turbo_lua_executor_shutdown_mode {
  TURBO_LUA_EXECUTOR_SHUTDOWN_DRAIN = 0,
  TURBO_LUA_EXECUTOR_SHUTDOWN_CANCEL = 1
} turbo_lua_executor_shutdown_mode_t;

typedef struct turbo_lua_executor_stats {
  size_t queue_capacity;
  size_t pending_commands;
  size_t peak_pending_commands;
  uint64_t posted_commands;
  uint64_t executed_commands;
  uint64_t canceled_commands;
  uint64_t rejected_commands;
  int accepting;
} turbo_lua_executor_stats_t;

#define TURBO_LUA_EXECUTOR_DEFAULT_CAPACITY ((size_t)256u)
#define TURBO_LUA_EXECUTOR_CONFIG_DEFAULT {TURBO_LUA_EXECUTOR_DEFAULT_CAPACITY, NULL, NULL}

/**
 * Create an executor that borrows L and binds the calling thread as owner.
 *
 * config may be NULL to use TURBO_LUA_EXECUTOR_CONFIG_DEFAULT. out must point
 * to NULL and is unchanged on failure. Destroy the executor before lua_close().
 *
 * @param out Receives the created executor on success.
 * @param L Borrowed Lua state owned by the caller.
 * @param config Optional copied queue and wake configuration.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_ENOMEM.
 */
int turbo_lua_executor_create(turbo_lua_executor_t **out, struct lua_State *L,
                              const turbo_lua_executor_config_t *config);

/**
 * Try to transfer one command to the bounded MPSC queue.
 *
 * Success transfers context ownership to the executor. Failure leaves it with
 * the caller. Producers may call this concurrently.
 *
 * @param executor Live executor that still accepts commands.
 * @param command Command copied into the queue on success.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOSPC, TURBO_ECANCELED, or TURBO_EIO.
 */
int turbo_lua_executor_try_post(turbo_lua_executor_t *executor,
                                const turbo_lua_executor_command_t *command);

/**
 * Execute at most max_commands on the owner thread captured by create.
 *
 * out_processed receives the number of terminal EXECUTE dispatches. This and
 * all other consumer/control APIs are single-owner and must not run concurrently.
 *
 * @param executor Live executor driven by the calling owner thread.
 * @param max_commands Nonzero upper bound for this poll.
 * @param out_processed Receives the number of executed commands.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EPERM for a non-owner caller.
 */
int turbo_lua_executor_poll(turbo_lua_executor_t *executor, size_t max_commands,
                            size_t *out_processed);

/**
 * Stop accepting commands, then dispatch every queued command exactly once.
 * DRAIN uses EXECUTE; CANCEL uses CANCEL. The operation is idempotent.
 *
 * @param executor Live executor driven by the calling owner thread.
 * @param mode Terminal disposition for commands already in the queue.
 * @param out_processed Receives the number of terminal dispatches.
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EPERM for a non-owner caller.
 */
int turbo_lua_executor_shutdown(turbo_lua_executor_t *executor,
                                turbo_lua_executor_shutdown_mode_t mode, size_t *out_processed);

/**
 * Cancel remaining commands and release the executor.
 *
 * Must run on the owner thread before lua_close(), with no concurrent producers
 * or consumer/control calls. NULL is accepted.
 *
 * @param executor Executor to cancel and release, or NULL.
 * @return TURBO_OK, or TURBO_EPERM when called from a non-owner thread.
 */
int turbo_lua_executor_destroy(turbo_lua_executor_t *executor);

/**
 * Snapshot bounded-queue counters. out is unchanged for invalid arguments.
 *
 * @param executor Live executor.
 * @param out Receives a non-transactional concurrent statistics snapshot.
 * @return TURBO_OK or TURBO_EINVAL.
 */
int turbo_lua_executor_get_stats(turbo_lua_executor_t *executor, turbo_lua_executor_stats_t *out);

/**
 * Return nonzero only on the owner thread captured by create.
 *
 * This check does not touch Lua and is safe on producer threads.
 */
int turbo_lua_executor_is_owner(const turbo_lua_executor_t *executor);

/**
 * Borrow the Lua state on the executor owner thread.
 *
 * out must point to NULL and remains unchanged on failure. This is the only
 * public raw-state accessor; producer threads receive TURBO_EPERM.
 *
 * @return TURBO_OK, TURBO_EINVAL, or TURBO_EPERM for a non-owner caller.
 */
int turbo_lua_executor_owner_state(const turbo_lua_executor_t *executor,
                                   struct lua_State **out);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_LUA_EXECUTOR_H */
