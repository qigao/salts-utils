#include <salts/shell.h>

#include <salts/error_codes.h>
#include <salts/thread.h>
#include <tstr.h>

#include <stdlib.h>
#include <string.h>

enum {
  SHELL_DEFAULT_TIMEOUT_MS = 30000,
  SHELL_DEFAULT_CHUNK_BYTES = 4096,
  SHELL_DEFAULT_LIMIT_BYTES = 16 * 1024 * 1024,
  SHELL_REQUEST_CAPACITY = 6,
  SHELL_COMMAND_CAPACITY = 8,
  SHELL_DRIVE_STEPS = 64,
  SHELL_IDLE_MS = 1,
  SHELL_STREAM_COUNT = 3
};

typedef struct shell_stream {
  void *buffer;
  size_t submitted;
  bool active;
  bool eof;
} shell_stream;

typedef struct shell_impl {
  cflow_process process;
  shell_stream streams[SHELL_STREAM_COUNT];
  void *input;
  size_t input_size;
  size_t input_offset;
  size_t output_limit;
  size_t chunk_size;
  tstr output[2];
  cflow_shell_output_fn output_callback;
  void *output_user;
  int error;
  bool cancelled;
  bool output_limit_exceeded;
  bool closing;
  bool driving;
} shell_impl;

static shell_impl *shell_get(const cflow_shell *shell) {
  return shell != NULL ? (shell_impl *)shell->impl : NULL;
}

static void shell_fail(shell_impl *impl, int error) {
  if (impl->error == SALTS_OK) impl->error = error;
}

static void shell_completed(void *user, cflow_io_request_id request_id,
                            cflow_io_lease_id lease_id, cflow_process_stream stream,
                            const cflow_io_completion *completion) {
  shell_impl *impl = (shell_impl *)user;
  shell_stream *slot = &impl->streams[stream];
  size_t count = completion->bytes;
  (void)request_id;
  (void)lease_id;
  slot->active = false;
  if (completion->kind == CFLOW_IO_COMPLETION_CANCELLED && impl->closing) return;
  if (completion->kind == CFLOW_IO_COMPLETION_FAILED) {
    shell_fail(impl, completion->error != SALTS_OK ? completion->error : SALTS_EIO);
    return;
  }
  if (completion->kind == CFLOW_IO_COMPLETION_CANCELLED) {
    shell_fail(impl, SALTS_ECANCELED);
    return;
  }
  if (count > slot->submitted) {
    shell_fail(impl, SALTS_EPROTO);
    return;
  }
  if (stream == CFLOW_PROCESS_STDIN) {
    impl->input_offset += count;
    if (count == 0u) shell_fail(impl, SALTS_EPIPE);
    return;
  }
  if (count != 0u) {
    size_t index = (size_t)stream - CFLOW_PROCESS_STDOUT;
    size_t used = tstr_len(impl->output[0]) + tstr_len(impl->output[1]);
    size_t available = impl->output_limit - used;
    size_t accepted = count < available ? count : available;
    size_t offset = tstr_len(impl->output[index]);
    /* Both strings reserve the full limit at start: data-path writes never allocate. */
    memcpy(impl->output[index] + offset, slot->buffer, accepted);
    if (!tstr_set_len_checked(impl->output[index], offset + accepted)) {
      shell_fail(impl, SALTS_EPROTO);
      return;
    }
    if (impl->output_callback != NULL && accepted != 0u)
      impl->output_callback(impl->output_user, stream, slot->buffer, accepted);
    if (accepted != count) {
      impl->output_limit_exceeded = true;
      shell_fail(impl, SALTS_ERANGE);
    }
  }
  if (completion->kind == CFLOW_IO_COMPLETION_EOF || count == 0u) slot->eof = true;
}

void cflow_shell_options_init(cflow_shell_options *options) {
  if (options == NULL) return;
  memset(options, 0, sizeof(*options));
  options->timeout_ms = SHELL_DEFAULT_TIMEOUT_MS;
  options->max_input_bytes = SHELL_DEFAULT_LIMIT_BYTES;
  options->max_output_bytes = SHELL_DEFAULT_LIMIT_BYTES;
  options->io_chunk_bytes = SHELL_DEFAULT_CHUNK_BYTES;
#if defined(_WIN32)
  options->backend_kind = CFLOW_IO_NATIVE_IOCP;
#elif defined(__linux__)
  options->backend_kind = CFLOW_IO_NATIVE_EPOLL;
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  options->backend_kind = CFLOW_IO_NATIVE_KQUEUE;
#else
  options->backend_kind = CFLOW_IO_NATIVE_POLL;
#endif
}

static void shell_free(shell_impl *impl) {
  free(impl->input);
  free(impl->streams[CFLOW_PROCESS_STDOUT].buffer);
  free(impl->streams[CFLOW_PROCESS_STDERR].buffer);
  tstr_free(impl->output[0]);
  tstr_free(impl->output[1]);
  free(impl);
}

static tstr shell_output_new(size_t capacity) {
  tstr empty = tstr_new();
  tstr reserved;
  if (empty == NULL) return NULL;
  reserved = tstr_reserve(empty, capacity);
  if (reserved == NULL) tstr_free(empty);
  return reserved;
}

int cflow_shell_start(cflow_shell *shell, const cflow_shell_options *options) {
  shell_impl *impl;
  cmeta_process_options_t process_options;
  cflow_process_config config = {0};
  int status;
  if (shell == NULL || shell->impl != NULL || options == NULL ||
      options->command == NULL || options->command[0] == '\0' ||
      options->max_input_bytes == 0u || options->max_output_bytes == 0u ||
      options->io_chunk_bytes == 0u || options->input_size > options->max_input_bytes ||
      (options->input_size != 0u && options->input == NULL) ||
      options->max_output_bytes > SIZE_MAX / 4u || options->io_chunk_bytes > SIZE_MAX / 4u)
    return SALTS_EINVAL;
  impl = (shell_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return SALTS_ENOMEM;
  impl->output_limit = options->max_output_bytes;
  impl->chunk_size = options->io_chunk_bytes;
  impl->input_size = options->input_size;
  impl->output_callback = options->output;
  impl->output_user = options->output_user;
  impl->output[0] = shell_output_new(impl->output_limit);
  impl->output[1] = shell_output_new(impl->output_limit);
  impl->streams[CFLOW_PROCESS_STDOUT].buffer = malloc(impl->chunk_size);
  impl->streams[CFLOW_PROCESS_STDERR].buffer = malloc(impl->chunk_size);
  if (impl->input_size != 0u) impl->input = malloc(impl->input_size);
  if (impl->output[0] == NULL || impl->output[1] == NULL ||
      impl->streams[CFLOW_PROCESS_STDOUT].buffer == NULL ||
      impl->streams[CFLOW_PROCESS_STDERR].buffer == NULL ||
      (impl->input_size != 0u && impl->input == NULL)) {
    shell_free(impl);
    return SALTS_ENOMEM;
  }
  if (impl->input_size != 0u) memcpy(impl->input, options->input, impl->input_size);
  cmeta_process_options_init(&process_options);
  process_options.program = options->command;
  process_options.cwd = options->cwd;
  process_options.env = options->env;
  process_options.timeout_ms = options->timeout_ms;
  process_options.flags = SALTS_PROCESS_SHELL_COMMAND;
  if (options->clean_environment) process_options.flags |= SALTS_PROCESS_CLEAN_ENVIRONMENT;
  config.backend_kind = options->backend_kind;
  config.request_capacity = SHELL_REQUEST_CAPACITY;
  config.command_capacity = SHELL_COMMAND_CAPACITY;
  config.completion_batch_capacity = SHELL_REQUEST_CAPACITY;
  config.completion = shell_completed;
  config.completion_user = impl;
  status = cflow_process_start(&impl->process, &process_options, &config);
  if (status != SALTS_OK) {
    shell_free(impl);
    return status;
  }
  shell->impl = impl;
  return SALTS_OK;
}

static int shell_close(shell_impl *impl) {
  int status;
  if (impl->closing) return SALTS_OK;
  status = cflow_process_close(&impl->process);
  if (status == SALTS_OK) impl->closing = true;
  return status;
}

static int shell_submit(shell_impl *impl, cflow_process_stream stream, size_t *progressed) {
  shell_stream *slot = &impl->streams[stream];
  cflow_process_submit_result submitted;
  const cflow_io_lease_id lease = (cflow_io_lease_id)stream + 1u;
  if (slot->active || slot->eof) return SALTS_OK;
  slot->submitted = impl->chunk_size;
  if (stream == CFLOW_PROCESS_STDIN) {
    size_t remaining = impl->input_size - impl->input_offset;
    if (remaining < slot->submitted) slot->submitted = remaining;
    submitted = cflow_process_try_write_stdin(&impl->process, lease,
        (const char *)impl->input + impl->input_offset, slot->submitted);
  } else if (stream == CFLOW_PROCESS_STDOUT) {
    submitted = cflow_process_try_read_stdout(&impl->process, lease, slot->buffer, slot->submitted);
  } else {
    submitted = cflow_process_try_read_stderr(&impl->process, lease, slot->buffer, slot->submitted);
  }
  if (submitted.status == CFLOW_PROCESS_SUBMIT_ACCEPTED) {
    slot->active = true;
    ++*progressed;
    return SALTS_OK;
  }
  /* A delivered callback can precede the Actor acknowledgement by one drive step. */
  if (submitted.status == CFLOW_PROCESS_SUBMIT_FULL ||
      submitted.status == CFLOW_PROCESS_SUBMIT_LEASE_IN_USE) return SALTS_OK;
  return SALTS_EIO;
}

static int shell_drive(shell_impl *impl, size_t max_steps, size_t *progressed) {
  cmeta_process_result_t result;
  int status = cflow_process_run_ready(&impl->process, max_steps, progressed);
  int poll_status;
  if (status != SALTS_OK) return status;
  if (impl->closing) return SALTS_OK;
  if (impl->error != SALTS_OK || impl->cancelled) return shell_close(impl);
  poll_status = cflow_process_poll(&impl->process, &result);
  if (poll_status != SALTS_OK && poll_status != SALTS_EBUSY) return poll_status;
  if (!impl->streams[CFLOW_PROCESS_STDIN].active &&
      (impl->input_offset == impl->input_size || poll_status == SALTS_OK) &&
      !impl->streams[CFLOW_PROCESS_STDIN].eof) {
    status = cflow_process_close_stdin(&impl->process);
    if (status == SALTS_OK) impl->streams[CFLOW_PROCESS_STDIN].eof = true;
    else if (status != SALTS_EBUSY) return status;
  }
  if (poll_status == SALTS_OK && impl->streams[CFLOW_PROCESS_STDOUT].eof &&
      impl->streams[CFLOW_PROCESS_STDERR].eof && !impl->streams[CFLOW_PROCESS_STDIN].active)
    return shell_close(impl);
  status = shell_submit(impl, CFLOW_PROCESS_STDOUT, progressed);
  if (status == SALTS_OK) status = shell_submit(impl, CFLOW_PROCESS_STDERR, progressed);
  if (status == SALTS_OK && poll_status == SALTS_EBUSY && impl->input_offset < impl->input_size)
    status = shell_submit(impl, CFLOW_PROCESS_STDIN, progressed);
  return status;
}

int cflow_shell_run_ready(cflow_shell *shell, size_t max_steps, size_t *progressed) {
  shell_impl *impl = shell_get(shell);
  int status;
  if (impl == NULL || max_steps == 0u || progressed == NULL) return SALTS_EINVAL;
  if (impl->driving) return SALTS_EBUSY;
  impl->driving = true;
  status = shell_drive(impl, max_steps, progressed);
  if (status != SALTS_OK) shell_fail(impl, status);
  impl->driving = false;
  return status;
}

int cflow_shell_poll(const cflow_shell *shell, cflow_shell_result *result) {
  shell_impl *impl = shell_get(shell);
  cflow_shell_result snapshot = {0};
  int status;
  if (impl == NULL || result == NULL) return SALTS_EINVAL;
  if (impl->driving || !cflow_process_is_quiescent(&impl->process)) return SALTS_EBUSY;
  status = cflow_process_poll(&impl->process, &snapshot.process);
  if (status != SALTS_OK) return status;
  snapshot.error = impl->error != SALTS_OK ? impl->error : snapshot.process.error_code;
  snapshot.stdin_bytes = impl->input_offset;
  snapshot.stdout_bytes = tstr_len(impl->output[0]);
  snapshot.stderr_bytes = tstr_len(impl->output[1]);
  if (impl->output_limit_exceeded) snapshot.outcome = CFLOW_SHELL_OUTPUT_LIMIT;
  else if (snapshot.process.state == SALTS_PROCESS_TIMED_OUT) snapshot.outcome = CFLOW_SHELL_TIMED_OUT;
  else if (impl->cancelled && snapshot.process.state == SALTS_PROCESS_TERMINATED)
    snapshot.outcome = CFLOW_SHELL_CANCELLED;
  else if (impl->error != SALTS_OK) snapshot.outcome = CFLOW_SHELL_FAILED;
  else if (snapshot.process.state == SALTS_PROCESS_TERMINATED) snapshot.outcome = CFLOW_SHELL_CANCELLED;
  else if (snapshot.process.state == SALTS_PROCESS_EXITED) snapshot.outcome = CFLOW_SHELL_EXITED;
  else if (snapshot.process.state == SALTS_PROCESS_SIGNALED) snapshot.outcome = CFLOW_SHELL_SIGNALED;
  else snapshot.outcome = CFLOW_SHELL_FAILED;
  *result = snapshot;
  return SALTS_OK;
}

int cflow_shell_cancel(cflow_shell *shell) {
  shell_impl *impl = shell_get(shell);
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->driving) return SALTS_EBUSY;
  impl->cancelled = true;
  return shell_close(impl);
}

int cflow_shell_execute(cflow_shell *shell, const cflow_shell_options *options,
                         cflow_shell_result *result) {
  int status;
  if (result == NULL) return SALTS_EINVAL;
  status = cflow_shell_start(shell, options);
  if (status != SALTS_OK) return status;
  while ((status = cflow_shell_poll(shell, result)) == SALTS_EBUSY) {
    size_t progressed = 0u;
    status = cflow_shell_run_ready(shell, SHELL_DRIVE_STEPS, &progressed);
    if (status != SALTS_OK) return status;
    if (progressed == 0u) cmeta_sleep_ms(SHELL_IDLE_MS);
  }
  return status;
}

const char *cflow_shell_stdout(const cflow_shell *shell) {
  shell_impl *impl = shell_get(shell);
  return impl != NULL ? impl->output[0] : NULL;
}

const char *cflow_shell_stderr(const cflow_shell *shell) {
  shell_impl *impl = shell_get(shell);
  return impl != NULL ? impl->output[1] : NULL;
}

int cflow_shell_destroy(cflow_shell *shell) {
  shell_impl *impl = shell_get(shell);
  int status;
  if (impl == NULL) return SALTS_EINVAL;
  if (impl->driving) return SALTS_EBUSY;
  status = cflow_process_destroy(&impl->process);
  if (impl->process.impl == NULL) {
    shell_free(impl);
    shell->impl = NULL;
  }
  return status;
}
