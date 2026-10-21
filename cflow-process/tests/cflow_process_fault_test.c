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

static int fault_actor_close(cflow_io_actor *actor);
static cflow_io_ack_status fault_actor_acknowledge(
    cflow_io_actor *actor, cflow_io_request_id request_id);
static int fault_native_observe(
    cflow_io_native_adapter *adapter, uint32_t timeout_ms, size_t *completed);

#define cflow_io_actor_close fault_actor_close
#define cflow_io_actor_acknowledge fault_actor_acknowledge
#define cflow_io_native_adapter_observe fault_native_observe
#include "../src/process.c"
#undef cflow_io_native_adapter_observe
#undef cflow_io_actor_acknowledge
#undef cflow_io_actor_close

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
