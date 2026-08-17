# Lua executor

## Decision

`turbo_lua_executor_t` is the single logical owner capability for one borrowed
`lua_State`. Creation captures the calling thread as the owner; the executor
does not create a thread. Applications call owner-only control functions from
that same event-loop thread. Arbitrary producer threads only call `try_post()`
and never receive or touch the Lua state.

The alternatives were direct inline calls, a mandatory dedicated Lua thread,
and dispatch from arbitrary thread-pool workers. Inline calls remain the lowest
cost synchronous path but cannot accept cross-thread work. A mandatory thread
adds latency and shutdown complexity for applications that already own an event
loop. Arbitrary workers cannot safely share one Lua state. The selected adapter
therefore keeps thread creation optional and makes serialization explicit.

## Ownership and failure protocol

- The executor borrows `lua_State` and must be destroyed before `lua_close()`.
- `owner_state()`, `poll()`, `shutdown()`, and `destroy()` verify the captured
  owner thread and return `TURBO_EPERM` otherwise. This makes the unsafe boundary
  explicit instead of relying on a comment-only convention.
- The bounded queue stores fixed `{dispatch, context}` commands. Capacity is a
  configured power of two; it does not grow.
- Successful `try_post()` moves context responsibility to the executor. Queue
  full returns `TURBO_ENOSPC`; shutdown returns `TURBO_ECANCELED`; either failure
  leaves responsibility with the caller.
- Every accepted command receives exactly one terminal dispatch: `EXECUTE`
  during `poll()`/drain, or `CANCEL` during cancellation shutdown/destruction.
- `poll()`, `shutdown()`, and `destroy()` are single-owner control-plane calls.
  Producers may post concurrently until shutdown begins, and must be quiescent
  before the owner destroys the executor.
- Dispatch callbacks run without executor locks. They may post new commands,
  but must not recursively enter control-plane calls or longjmp across dispatch.

## Typical integration

```c
#include "turbo_error.h"
#include "turbo_lua_executor.h"

static void run_command(struct lua_State *L, void *context,
                        turbo_lua_executor_dispatch_reason_t reason) {
    int *executed = (int *)context;
    (void)L;
    if (reason == TURBO_LUA_EXECUTOR_EXECUTE) *executed = 1;
}

int run_one_lua_command(struct lua_State *L) {
    turbo_lua_executor_t *executor = NULL;
    turbo_lua_executor_config_t config = TURBO_LUA_EXECUTOR_CONFIG_DEFAULT;
    turbo_lua_executor_command_t command;
    size_t processed = 0;
    int executed = 0;
    int status;

    command.dispatch = run_command;
    command.context = &executed;
    status = turbo_lua_executor_create(&executor, L, &config);
    if (status != TURBO_OK) return status;
    status = turbo_lua_executor_try_post(executor, &command);
    if (status != TURBO_OK) {
        (void)turbo_lua_executor_destroy(executor);
        return status;
    }
    status = turbo_lua_executor_poll(executor, 1, &processed);
    if (turbo_lua_executor_destroy(executor) != TURBO_OK) return TURBO_EPERM;
    if (status != TURBO_OK) return status;
    return processed == 1 && executed ? TURBO_OK : TURBO_EIO;
}
```

Every generated Lua import client is created with and borrows one executor.
Creation, synchronous `_on_owner()` calls, and `close()` are owner-only. Async
imports annotated with `lua_async(future)` snapshot the typed request and post
through the bound executor, so producer code does not pass a Lua state or
executor per call. `future` is the only accepted async schema mode. A queued
command holds the client alive until terminal
dispatch and completes a typed Future on the owner thread. Producer threads may
submit, query, poll, or cancel Futures without touching Lua. Stop producers
before `close()`; after a successful close the client handle must not be used.
Then drain or cancel queued commands and destroy the executor before
`lua_close()`. Queue full maps to `DATA_BIND_ERR_LIMIT`; executor shutdown and
cancellation map to `DATA_BIND_ERR_CANCELED`. This changes only the generated C
control API and introduces no schema wire-format migration.

## Managed dedicated owner

Applications without an existing owner event loop can use
`turbo_lua_worker_t`. It creates one dedicated thread, then creates and closes
the Lua state and executor on that thread. Producers receive only the bounded
command submission API.

```c
#include "turbo_error.h"
#include "turbo_lua_worker.h"

static void run_managed_command(
    struct lua_State *L, void *context,
    turbo_lua_executor_dispatch_reason_t reason) {
    int *executed = (int *)context;
    (void)L;
    if (reason == TURBO_LUA_EXECUTOR_EXECUTE) *executed = 1;
}

int run_one_managed_command(void) {
    turbo_lua_worker_t *worker = NULL;
    turbo_lua_worker_config_t config = TURBO_LUA_WORKER_CONFIG_DEFAULT;
    turbo_lua_executor_command_t command;
    int executed = 0;
    int status;

    command.dispatch = run_managed_command;
    command.context = &executed;
    status = turbo_lua_worker_create(&worker, &config);
    if (status != TURBO_OK) return status;

    status = turbo_lua_worker_try_post(worker, &command);
    if (status == TURBO_OK) {
        status = turbo_lua_worker_stop(worker, TURBO_LUA_EXECUTOR_DRAIN);
    } else {
        (void)turbo_lua_worker_stop(worker, TURBO_LUA_EXECUTOR_CANCEL);
    }
    {
        int destroy_status = turbo_lua_worker_destroy(worker);
        if (status == TURBO_OK) status = destroy_status;
    }
    if (status != TURBO_OK) return status;
    return executed ? TURBO_OK : TURBO_EIO;
}
```

For generated schema clients, `on_start` receives the owner-only executor and
creates the clients; `on_stop` closes them before shutdown dispatches queued
commands. Accepted commands retain their client until terminal dispatch.
Producer submissions must be quiescent before stop/destroy. `DRAIN` executes
accepted work, while `CANCEL` dispatches cancellation; the worker observes a
stop request after every command so cancellation does not execute the rest of a
previous poll batch.
