#ifndef DATABIND_COMPILER_WASM_PROJECTION_H
#define DATABIND_COMPILER_WASM_PROJECTION_H

#include "projection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct databind_compiler_wasm_config {
  const char *component_id;
  const char *native_header;

  /* Caller-supplied already-built Core Wasm implementation module. */
  const char *core_module_path;

  /* Derived generated C integration outputs. */
  const char *host_header_output;
  const char *host_source_output;
  const char *guest_header_output;

  /* Stable C identifier derived from the artifact basename. */
  const char *symbol_prefix;
} databind_compiler_wasm_config;

/* Compiler-private stage destinations; semantic names remain in config.
 * Coordinator must commit or abort the complete output set. */
int databind_compiler_wasm_render_staged(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    const char *component_stage,
    const char *host_header_stage,
    const char *host_source_stage,
    const char *guest_header_stage);

int databind_compiler_wasm_generate(
    const databind_compiler_projection_input *input,
    const databind_compiler_projection_request *request,
    void *context);

extern const databind_compiler_projection_backend
    DATABIND_COMPILER_WASM_BACKEND;

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_WASM_PROJECTION_H */
