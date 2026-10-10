#include <salts/shell.h>
#include <salts/error_codes.h>
#include <salts/thread.h>

#include <stdio.h>

enum { EXAMPLE_OUTPUT_LIMIT = 4096, EXAMPLE_DRIVE_STEPS = 64 };

int main(void) {
  cflow_shell shell = {0};
  cflow_shell_options options;
  cflow_shell_result result;
  int status;
  int exit_code;

  cflow_shell_options_init(&options);
  options.command = "echo hello-from-cflow-shell";
  options.max_output_bytes = EXAMPLE_OUTPUT_LIMIT;
  status = cflow_shell_execute(&shell, &options, &result);
  if (status != SALTS_OK) {
    fprintf(stderr, "shell execution failed: %d\n", status);
    if (shell.impl != NULL) {
      status = cflow_shell_cancel(&shell);
      if (status != SALTS_OK) return 1;
      while ((status = cflow_shell_poll(&shell, &result)) == SALTS_EBUSY) {
        size_t progressed = 0u;
        status = cflow_shell_run_ready(&shell, EXAMPLE_DRIVE_STEPS, &progressed);
        if (status != SALTS_OK) return 1;
        if (progressed == 0u) cmeta_sleep_ms(1u);
      }
      if (status != SALTS_OK || cflow_shell_destroy(&shell) != SALTS_OK) return 1;
    }
    return 1;
  }
  exit_code = result.outcome == CFLOW_SHELL_EXITED && result.process.exit_code == 0 ? 0 : 1;
  if (fwrite(cflow_shell_stdout(&shell), 1u, result.stdout_bytes, stdout) != result.stdout_bytes ||
      fwrite(cflow_shell_stderr(&shell), 1u, result.stderr_bytes, stderr) != result.stderr_bytes)
    exit_code = 1;
  if (cflow_shell_destroy(&shell) != SALTS_OK) exit_code = 1;
  return exit_code;
}
