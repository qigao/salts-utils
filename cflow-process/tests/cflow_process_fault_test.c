/*
 * Dedicated source-level fault injection for the Process lifecycle.
 *
 * Build the real Process translation unit here with only its three outbound
 * Actor/NativeIO status edges redirected to test-local shims. This avoids a
 * second public ABI or instrumentation of the installed Process target.
 */
#include <cflow/io_actor.h>
#include <cflow/io_native_adapter.h>
#include <salts/process.h>
#include <salts/clock.h>
#include <salts/error_codes.h>
#include <salts/thread.h>
#include "tinytest.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static bool inject_close_error;
static bool inject_ack_error;
static bool inject_observe_error;
static bool inject_adapter_destroy_error;
static bool inject_actor_destroy_error;
static bool inject_executor_shutdown_error;

static int fault_actor_close(cflow_io_actor *actor);
static cflow_io_ack_status fault_actor_acknowledge(
    cflow_io_actor *actor, cflow_io_request_id request_id);
static int fault_native_observe(
    cflow_io_native_adapter *adapter, uint32_t timeout_ms, size_t *completed);
static int fault_adapter_destroy(cflow_io_native_adapter *adapter);
static int fault_actor_destroy(cflow_io_actor *actor);
static bool fault_executor_shutdown(cflow_executor *executor);

#define cflow_io_actor_close fault_actor_close
#define cflow_io_actor_acknowledge fault_actor_acknowledge
#define cflow_io_native_adapter_observe fault_native_observe
#define cflow_io_native_adapter_destroy fault_adapter_destroy
#define cflow_io_actor_destroy fault_actor_destroy
#define cflow_executor_shutdown fault_executor_shutdown
#include "../src/process.c"
#undef cflow_io_native_adapter_observe
#undef cflow_io_native_adapter_destroy
#undef cflow_io_actor_destroy
#undef cflow_executor_shutdown
#undef cflow_io_actor_acknowledge
#undef cflow_io_actor_close

/* The retainer calls the same injected Process code and is test-local. */
#include "../src/process_retainer.c"

static int fault_actor_close(cflow_io_actor *actor) {
  if (inject_close_error) {
    inject_close_error = false;
    return SALTS_EIO;
  }
  return cflow_io_actor_close(actor);
}

static cflow_io_ack_status fault_actor_acknowledge(
    cflow_io_actor *actor, cflow_io_request_id request_id) {
  if (inject_ack_error) return CFLOW_IO_ACK_INVALID_ARGUMENT;
  return cflow_io_actor_acknowledge(actor, request_id);
}

static int fault_native_observe(
    cflow_io_native_adapter *adapter, uint32_t timeout_ms, size_t *completed) {
  if (inject_observe_error) {
    if (completed != NULL) *completed = 0u;
    return SALTS_EIO;
  }
  return cflow_io_native_adapter_observe(adapter, timeout_ms, completed);
}

static int fault_adapter_destroy(cflow_io_native_adapter *adapter) {
  if (inject_adapter_destroy_error) return SALTS_EBUSY;
  return cflow_io_native_adapter_destroy(adapter);
}

static int fault_actor_destroy(cflow_io_actor *actor) {
  if (inject_actor_destroy_error) return SALTS_EBUSY;
  return cflow_io_actor_destroy(actor);
}

static bool fault_executor_shutdown(cflow_executor *executor) {
  if (inject_executor_shutdown_error) return false;
  return cflow_executor_shutdown(executor);
}

typedef struct retained_test_payload retained_test_payload;

static size_t retained_test_releases;

static void retained_test_payload_release(void *p) {
  ++retained_test_releases;
  free(p);
}

typedef struct fault_probe {
  cflow_io_request_id ids[4];
  cflow_io_completion_kind kinds[4];
  size_t callbacks;
} fault_probe;

static void fault_completed(void *user, cflow_io_request_id request_id,
                            cflow_io_lease_id lease_id,
                            cflow_process_stream stream,
                            const cflow_io_completion *completion) {
  fault_probe *probe = (fault_probe *)user;
  (void)lease_id;
  (void)stream;
  if (probe == NULL || completion == NULL) return;
  if (probe->callbacks < 4u) {
    probe->ids[probe->callbacks] = request_id;
    probe->kinds[probe->callbacks] = completion->kind;
  }
  ++probe->callbacks;
}

typedef struct fault_stats_observer {
  cflow_process *process;
  cmeta_mutex_t gate;
  bool stop;
  bool failed;
  size_t samples;
} fault_stats_observer;

static void fault_collect_stats(void *user) {
  fault_stats_observer *observer = (fault_stats_observer *)user;
  for (;;) {
    cflow_process_stats stats = {0};
    bool stop;
    bool valid;
    cmeta_mutex_lock(&observer->gate);
    stop = observer->stop;
    cmeta_mutex_unlock(&observer->gate);
    if (stop) break;
    valid = cflow_process_get_stats(observer->process, &stats);
    cmeta_mutex_lock(&observer->gate);
    if (!valid) observer->failed = true;
    ++observer->samples;
    cmeta_mutex_unlock(&observer->gate);
    cmeta_thread_yield();
  }
}

static void fault_process_options(cmeta_process_options_t *options) {
#if defined(_WIN32)
  static const char *args[] = {
      "-NoProfile", "-Command",
      "[Console]::OpenStandardInput().CopyTo([Console]::OpenStandardOutput())", NULL};
  cmeta_process_options_init(options);
  options->program = "powershell.exe";
  options->args = args;
#else
  cmeta_process_options_init(options);
  options->program = "/bin/cat";
  options->args = NULL;
#endif
  options->flags = 0u;
}

static cflow_process_config fault_config(fault_probe *probe) {
  cflow_process_config config = {0};
#if defined(_WIN32)
  config.backend_kind = CFLOW_IO_NATIVE_IOCP;
#elif defined(__linux__)
  config.backend_kind = CFLOW_IO_NATIVE_EPOLL;
#elif defined(__APPLE__)
  config.backend_kind = CFLOW_IO_NATIVE_KQUEUE;
#else
  config.backend_kind = CFLOW_IO_NATIVE_POLL;
#endif
  config.request_capacity = 4u;
  config.command_capacity = 4u;
  config.completion_batch_capacity = 4u;
  config.completion = fault_completed;
  config.completion_user = probe;
  return config;
}

static int fault_drive_to_quiescence(cflow_process *process) {
  const uint64_t started = cmeta_hrtime();
  while (!cflow_process_is_quiescent(process)) {
    size_t progressed = 0u;
    const int status = cflow_process_run_ready(process, 64u, &progressed);
    if (status != SALTS_OK) return status;
    if (cmeta_hrtime() - started > UINT64_C(5000000000))
      return SALTS_ETIMEDOUT;
    if (progressed == 0u) cmeta_thread_yield();
  }
  return SALTS_OK;
}

spec("CFlow Process private lifecycle faults") {
  it("closes admission and requests termination after actor close error") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    cflow_process_stats stats;
    cflow_process_submit_result read;
    char borrowed_buffer = 0;

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 1u, &borrowed_buffer, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);

    inject_close_error = true;
    check_equal(cflow_process_close(&process), SALTS_EIO);
    check_true(cflow_process_get_stats(&process, &stats));
    check_true(stats.close_requested);
    check_equal(stats.io.active_requests, (size_t)1u);
    check_false(cflow_process_is_quiescent(&process));
    check_equal(cflow_process_destroy(&process), SALTS_EBUSY);
    check_not_null(process.impl);
    check_equal(probe.callbacks, (size_t)0u);

    /* Even though Actor close reported an error, the native child must
     * already have received its termination request (before a second close). */
    {
      const uint64_t started = cmeta_hrtime();
      cmeta_process_result_t terminal;
      int poll_status;
      do {
        poll_status = cflow_process_poll(&process, &terminal);
        check_true(poll_status == SALTS_OK || poll_status == SALTS_EBUSY);
        if (poll_status == SALTS_OK) break;
        check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
        cmeta_thread_yield();
      } while (true);
      check_equal(poll_status, SALTS_OK);
    }

    /* The same owner may retry, then deliver/acknowledge the cancelled read. */
    check_equal(cflow_process_close(&process), SALTS_OK);
    check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
    check_equal(probe.callbacks, (size_t)1u);
    check_equal(probe.ids[0], read.request_id);
    check_equal(probe.kinds[0], CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }

  it("retains a delivered lease while Actor acknowledgment repeatedly fails") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    cflow_process_stats stats;
    cflow_process_submit_result read;
    char borrowed_buffer = 0;
    size_t round;

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 2u, &borrowed_buffer, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(cflow_process_close(&process), SALTS_OK);

    inject_ack_error = true;
    /* A native backend can take several driver turns to publish cancellation.
     * Start counting errors only after an authoritative terminal callback has
     * reached the acknowledgment boundary. */
    {
      const uint64_t started = cmeta_hrtime();
      bool observed_error = false;
      do {
        size_t progressed = 0u;
        const int status = cflow_process_run_ready(&process, 64u, &progressed);
        check_true(status == SALTS_OK || status == SALTS_EPROTO);
        if (status == SALTS_EPROTO) {
          observed_error = true;
          break;
        }
        check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
        if (progressed == 0u) cmeta_thread_yield();
      } while (true);
      check_true(observed_error);
    }
    for (round = 0u; round < 4u; ++round) {
      size_t progressed = 0u;
      check_equal(cflow_process_run_ready(&process, 64u, &progressed), SALTS_EPROTO);
      check_equal(probe.callbacks, (size_t)1u);
      check_equal(probe.ids[0], read.request_id);
      check_equal(probe.kinds[0], CFLOW_IO_COMPLETION_CANCELLED);
      check_true(cflow_process_get_stats(&process, &stats));
      check_equal(stats.io.active_requests, (size_t)1u);
      check_equal(stats.io.delivered_unacknowledged, (size_t)1u);
      check_false(cflow_process_is_quiescent(&process));
      check_equal(cflow_process_destroy(&process), SALTS_EBUSY);
      check_not_null(process.impl);
    }

    inject_ack_error = false;
    check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
    check_equal(probe.callbacks, (size_t)1u);
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }

  it("returns promptly from each shutdown step while a delivered lease is stuck") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    cflow_process_submit_result read;
    cflow_process_stats stats;
    char borrowed_buffer = 0;
    size_t round;

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 4u, &borrowed_buffer, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(cflow_process_close(&process), SALTS_OK);

    inject_ack_error = true;
    {
      const uint64_t started = cmeta_hrtime();
      for (;;) {
        size_t advanced = 0u;
        int status = cflow_process_run_ready(&process, 64u, &advanced);
        check_true(status == SALTS_OK || status == SALTS_EPROTO);
        if (status == SALTS_EPROTO) break;
        check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
        if (advanced == 0u) cmeta_thread_yield();
      }
    }
    check_equal(probe.callbacks, (size_t)1u);

    {
      const uint64_t started = cmeta_hrtime();
      for (round = 0u; round < 32u; ++round) {
        size_t progressed = SIZE_MAX;
        bool quiescent = true;
        check_equal(cflow_process_shutdown_step(
            &process, 8u, &progressed, &quiescent), SALTS_EPROTO);
        check_true(progressed <= 8u);
        check_false(quiescent);
        check_equal(probe.callbacks, (size_t)1u);
        check_true(cflow_process_get_stats(&process, &stats));
        check_equal(stats.io.active_requests, (size_t)1u);
        check_equal(stats.io.delivered_unacknowledged, (size_t)1u);
        check_equal(cflow_process_destroy(&process), SALTS_EBUSY);
        check_not_null(process.impl);
      }
      check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
    }

    inject_ack_error = false;
    {
      const uint64_t started = cmeta_hrtime();
      for (;;) {
        size_t progressed = 0u;
        bool quiescent = false;
        const int status = cflow_process_shutdown_step(
            &process, 64u, &progressed, &quiescent);
        check_true(status == SALTS_OK || status == SALTS_EBUSY);
        if (quiescent) {
          check_equal(status, SALTS_OK);
          break;
        }
        check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
        if (progressed == 0u) cmeta_thread_yield();
      }
    }
    check_equal(probe.callbacks, (size_t)1u);
    check_equal(probe.ids[0], read.request_id);
    check_equal(probe.kinds[0], CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }

  it("publishes consistent status snapshots to a concurrent observer during close") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    fault_stats_observer observer = {0};
    cmeta_thread_t watcher = NULL;
    size_t attempts;
    const uint64_t started = cmeta_hrtime();

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    observer.process = &process;
    cmeta_mutex_init(&observer.gate);
    check_not_null(observer.gate);
    check_equal(cmeta_thread_create(&watcher, fault_collect_stats, &observer), SALTS_OK);
    for (attempts = 0u; attempts < 64u; ++attempts) cmeta_thread_yield();
    check_equal(cflow_process_close(&process), SALTS_OK);
    while (!cflow_process_is_quiescent(&process)) {
      size_t progressed = 0u;
      bool quiescent = false;
      const int status = cflow_process_shutdown_step(
          &process, 32u, &progressed, &quiescent);
      check_true(status == SALTS_OK || status == SALTS_EBUSY);
      check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
      if (progressed == 0u) cmeta_thread_yield();
    }
    cmeta_mutex_lock(&observer.gate);
    observer.stop = true;
    cmeta_mutex_unlock(&observer.gate);
    check_equal(cmeta_thread_join(&watcher), SALTS_OK);
    cmeta_thread_destroy(&watcher);
    check_false(observer.failed);
    check_greater(observer.samples, (size_t)0u);
    cmeta_mutex_destroy(&observer.gate);
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }

  it("refuses Process destruction during a live driver turn") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    cflow_process_impl *impl;
    size_t progressed = SIZE_MAX;
    bool quiescent = true;

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    check_equal(cflow_process_close(&process), SALTS_OK);
    check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
    impl = (cflow_process_impl *)process.impl;
    check_not_null(impl);
    cmeta_mutex_lock(&impl->gate);
    impl->driver_active = true; /* test-only reentrant driver ownership */
    cmeta_mutex_unlock(&impl->gate);
    check_false(cflow_process_is_quiescent(&process));
    check_equal(cflow_process_destroy(&process), SALTS_EBUSY);
    check_not_null(process.impl);
    check_equal(cflow_process_shutdown_step(
        &process, 1u, &progressed, &quiescent), SALTS_EBUSY);
    check_false(quiescent);
    cmeta_mutex_lock(&impl->gate);
    impl->driver_active = false;
    cmeta_mutex_unlock(&impl->gate);
    check_true(cflow_process_is_quiescent(&process));
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }

  it("transfers closing Process and borrowed buffers to a bounded explicit owner") {
    cflow_process_retainer owner = {0};
    cflow_process_retainer_token token = {0};
    cflow_process_retainer_token second = {0};
    cflow_process process = {0};
    cmeta_process_options_t options;
    fault_probe *probe = (fault_probe *)calloc(1u, sizeof(*probe) + 8u);
    cflow_process_config config;
    cflow_process_submit_result read;
    char *buffer;
    size_t reaped = 0u, pending = 0u, tries = 0u;
    const uint64_t started = cmeta_hrtime();

    check_not_null(probe);
    buffer = (char *)(probe + 1);
    config = fault_config(probe);
    retained_test_releases = 0u;
    check_equal(cflow_process_retainer_init(&owner, 1u), SALTS_OK);
    check_equal(cflow_process_retainer_reserve(&owner, &token), SALTS_OK);
    check_equal(cflow_process_retainer_reserve(&owner, &second), SALTS_ENOBUFS);
    check_equal(cflow_process_retainer_destroy(&owner), SALTS_EBUSY);

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 7u, buffer, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(cflow_process_retainer_adopt(
        &owner, token, &process, probe, retained_test_payload_release), SALTS_EBUSY);
    check_not_null(process.impl);
    check_equal(cflow_process_close(&process), SALTS_OK);
    check_equal(cflow_process_retainer_adopt(
        &owner, token, &process, probe, retained_test_payload_release), SALTS_OK);
    check_null(process.impl);
    check_equal(cflow_process_retainer_release(&owner, token), SALTS_EINVAL);

    inject_ack_error = true;
    do {
      const int status = cflow_process_retainer_run_ready(
          &owner, 64u, &reaped, &pending);
      check_true(status == SALTS_EPROTO || status == SALTS_EBUSY);
      check_equal(reaped, (size_t)0u);
      check_equal(pending, (size_t)1u);
      check_equal(retained_test_releases, (size_t)0u);
      check_equal(cflow_process_retainer_destroy(&owner), SALTS_EBUSY);
      check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
      ++tries;
      if (status == SALTS_EPROTO) break;
      cmeta_thread_yield();
    } while (true);
    check_greater(tries, (size_t)0u);
    check_equal(probe->callbacks, (size_t)1u);
    check_equal(probe->ids[0], read.request_id);

    /* An indefinitely failing provider consumes exactly one bounded slot,
     * never invents completion and never exposes this callback buffer to a
     * second admission. Repeat many owner turns before lifting the fault. */
    {
      const uint64_t fault_started = cmeta_hrtime();
      size_t round;
      for (round = 0u; round < 64u; ++round) {
        check_equal(cflow_process_retainer_run_ready(
            &owner, 8u, &reaped, &pending), SALTS_EPROTO);
        check_equal(reaped, (size_t)0u);
        check_equal(pending, (size_t)1u);
        check_equal(retained_test_releases, (size_t)0u);
        check_equal(probe->callbacks, (size_t)1u);
        check_equal(cflow_process_retainer_reserve(&owner, &second),
                    SALTS_ENOBUFS);
        check_equal(cflow_process_retainer_destroy(&owner), SALTS_EBUSY);
      }
      check_true(cmeta_hrtime() - fault_started < UINT64_C(5000000000));
    }

    inject_ack_error = false;
    do {
      const int status = cflow_process_retainer_run_ready(
          &owner, 64u, &reaped, &pending);
      check_true(status == SALTS_OK || status == SALTS_EBUSY);
      check_true(cmeta_hrtime() - started < UINT64_C(5000000000));
      if (pending == 0u) {
        check_equal(status, SALTS_OK);
        check_equal(reaped, (size_t)1u);
        break;
      }
      cmeta_thread_yield();
    } while (true);
    check_equal(retained_test_releases, (size_t)1u);
    check_equal(cflow_process_retainer_destroy(&owner), SALTS_OK);
    check_null(owner.impl);
  }

  it("keeps the bounded Process recovery owner on refused native backend destroy") {
    cflow_process_retainer owner = {0};
    cflow_process_retainer_token token = {0};
    cflow_process process = {0};
    cmeta_process_options_t options;
    fault_probe *probe = (fault_probe *)calloc(1u, sizeof(*probe) + 8u);
    cflow_process_config config;
    cflow_process_submit_result read;
    size_t reaped = SIZE_MAX, pending = SIZE_MAX;

    check_not_null(probe);
    retained_test_releases = 0u;
    config = fault_config(probe);
    fault_process_options(&options);
    check_equal(cflow_process_retainer_init(&owner, 1u), SALTS_OK);
    check_equal(cflow_process_retainer_reserve(&owner, &token), SALTS_OK);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 11u, probe + 1, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    check_equal(cflow_process_close(&process), SALTS_OK);
    check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
    check_equal(probe->callbacks, (size_t)1u);
    check_equal(probe->ids[0], read.request_id);

    check_equal(cflow_process_retainer_adopt(
        &owner, token, &process, probe, retained_test_payload_release), SALTS_OK);
    check_null(process.impl);
    inject_adapter_destroy_error = true;
    check_equal(cflow_process_retainer_run_ready(
        &owner, 1u, &reaped, &pending), SALTS_EBUSY);
    check_equal(reaped, (size_t)0u);
    check_equal(pending, (size_t)1u);
    check_equal(retained_test_releases, (size_t)0u);
    check_equal(cflow_process_retainer_destroy(&owner), SALTS_EBUSY);

    /* A later owner turn retries precisely the refused native teardown.
     * Neither a quiescence snapshot nor a failed destroy is permission to
     * force-free the borrowed callback payload or its Process. */
    inject_adapter_destroy_error = false;
    check_equal(cflow_process_retainer_run_ready(
        &owner, 1u, &reaped, &pending), SALTS_OK);
    check_equal(reaped, (size_t)1u);
    check_equal(pending, (size_t)0u);
    check_equal(retained_test_releases, (size_t)1u);
    check_equal(cflow_process_retainer_destroy(&owner), SALTS_OK);
  }

  it("retains the exact Process on refused Executor or Actor final teardown") {
    size_t kind;
    for (kind = 0u; kind < 2u; ++kind) {
      cflow_process_retainer owner = {0};
      cflow_process_retainer_token token = {0};
      cflow_process process = {0};
      cmeta_process_options_t options;
      fault_probe *probe = (fault_probe *)calloc(1u, sizeof(*probe) + 8u);
      cflow_process_config config;
      cflow_process_submit_result read;
      size_t reaped = SIZE_MAX, pending = SIZE_MAX;

      check_not_null(probe);
      config = fault_config(probe);
      fault_process_options(&options);
      retained_test_releases = 0u;
      check_equal(cflow_process_retainer_init(&owner, 1u), SALTS_OK);
      check_equal(cflow_process_retainer_reserve(&owner, &token), SALTS_OK);
      check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
      read = cflow_process_try_read_stdout(&process, 21u + kind, probe + 1, 1u);
      check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
      check_equal(cflow_process_close(&process), SALTS_OK);
      check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
      check_equal(probe->callbacks, (size_t)1u);
      check_equal(probe->ids[0], read.request_id);
      check_equal(cflow_process_retainer_adopt(
          &owner, token, &process, probe, retained_test_payload_release), SALTS_OK);

      inject_executor_shutdown_error = kind == 0u;
      inject_actor_destroy_error = kind == 1u;
      check_equal(cflow_process_retainer_run_ready(
          &owner, 1u, &reaped, &pending), SALTS_EBUSY);
      check_equal(reaped, (size_t)0u);
      check_equal(pending, (size_t)1u);
      check_equal(retained_test_releases, (size_t)0u);
      check_equal(cflow_process_retainer_destroy(&owner), SALTS_EBUSY);

      /* The Actor refusal follows a successful NativeIO teardown and must
       * still leave an exact, retryable Process/borrowed callback owner. */
      if (kind == 1u) {
        process_retainer_impl *retained = (process_retainer_impl *)owner.impl;
        cflow_process_impl *impl =
            (cflow_process_impl *)retained->slots[token.index].process.impl;
        check_not_null(impl);
        check_null(impl->adapter.impl);
        check_not_null(impl->actor.impl);
      }

      inject_executor_shutdown_error = false;
      inject_actor_destroy_error = false;
      check_equal(cflow_process_retainer_run_ready(
          &owner, 1u, &reaped, &pending), SALTS_OK);
      check_equal(reaped, (size_t)1u);
      check_equal(pending, (size_t)0u);
      check_equal(retained_test_releases, (size_t)1u);
      check_equal(cflow_process_retainer_destroy(&owner), SALTS_OK);
    }
  }

  it("reports persistent NativeIO observe faults without discarding the read") {
    cmeta_process_options_t options;
    cflow_process process = {0};
    fault_probe probe = {0};
    cflow_process_config config = fault_config(&probe);
    cflow_process_submit_result read;
    cflow_process_stats stats;
    char borrowed_buffer = 0;
    size_t round;
    size_t faults = 0u;

    fault_process_options(&options);
    check_equal(cflow_process_start(&process, &options, &config), SALTS_OK);
    read = cflow_process_try_read_stdout(&process, 3u, &borrowed_buffer, 1u);
    check_equal(read.status, CFLOW_PROCESS_SUBMIT_ACCEPTED);
    inject_observe_error = true;
    for (round = 0u; round < 6u; ++round) {
      size_t progressed = 0u;
      int status = cflow_process_run_ready(&process, 64u, &progressed);
      if (status == SALTS_EIO) ++faults;
      else check_equal(status, SALTS_OK);
    }
    check_greater(faults, (size_t)0u);
    check_equal(probe.callbacks, (size_t)0u);
    check_true(cflow_process_get_stats(&process, &stats));
    check_equal(stats.io.active_requests, (size_t)1u);
    check_equal(cflow_process_destroy(&process), SALTS_EBUSY);
    check_not_null(process.impl);

    inject_observe_error = false;
    check_equal(cflow_process_close(&process), SALTS_OK);
    check_equal(fault_drive_to_quiescence(&process), SALTS_OK);
    check_equal(probe.callbacks, (size_t)1u);
    check_equal(probe.ids[0], read.request_id);
    check_equal(probe.kinds[0], CFLOW_IO_COMPLETION_CANCELLED);
    check_equal(cflow_process_destroy(&process), SALTS_OK);
    check_null(process.impl);
  }
}
