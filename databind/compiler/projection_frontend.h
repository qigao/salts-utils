#ifndef DATABIND_COMPILER_PROJECTION_FRONTEND_H
#define DATABIND_COMPILER_PROJECTION_FRONTEND_H

#include "plugin_projection.h"
#include "wasm_projection.h"
#include "method_plan_projection.h"
#include "native_service_projection.h"
#include "openapi_projection.h"
#include "projection_config.h"
#include "projection.h"

#include "cmeta_fs.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS 11u
#define DATABIND_COMPILER_FRONTEND_MAX_OUTPUTS 32u
#define DATABIND_COMPILER_ARTIFACT_NAME_MAX 127u

/* Compiler-private publication manifest. A zero owner ID denotes a primary
 * compiler output; other entries belong to the selected typed projection.
 * Paths borrow from the frontend input or this plan and remain valid for
 * the plan's lifetime. No generator may introduce undisclosed file outputs. */
typedef struct databind_compiler_planned_output {
  const char *path;
  databind_compiler_projection_id owner;
} databind_compiler_planned_output;

typedef struct databind_compiler_projection_frontend_input {
  const char *artifacts;
  const char *transports;
  const char *component_id;
  const char *artifact_name;
  const char *artifact_version;
  const char *projection_config_path;
  const char *wasm_core_module_path;

  /* Native/source-language output selected by the ordinary --output option. */
  const char *output_path;

  /* Other active renderer outputs reserved against generated output collisions. */
  const char *source_output_path;
  const char *guest_output_path;
  const char *dsl_output_path;
} databind_compiler_projection_frontend_input;

typedef struct databind_compiler_projection_frontend_plan {
  databind_compiler_projection_request
      requests[DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS];
  databind_compiler_projection_backend
      backends[DATABIND_COMPILER_FRONTEND_MAX_SELECTIONS];
  size_t request_count;
  size_t backend_count;

  /* Complete set of generated file paths, including secondary plugin/wasm
   * and native-service artifacts. Does not itself stage or commit files. */
  databind_compiler_planned_output
      outputs[DATABIND_COMPILER_FRONTEND_MAX_OUTPUTS];
  size_t output_count;

  databind_compiler_plugin_config plugin;
  databind_compiler_wasm_config wasm;
  databind_compiler_native_service_config native_service;
  databind_compiler_openapi_projection_config openapi;
  databind_compiler_projection_config external_config;
  databind_compiler_http_projection_config http;
  databind_compiler_rpc_projection_config rpc;
  databind_compiler_socket_projection_config socket;
  databind_compiler_flowmq_projection_config flowmq;

  char method_plan_symbol_prefix[256];
  char artifact_dir[SALTS_FS_MAX_PATH];
  char native_header[SALTS_FS_MAX_PATH];
  char native_service_header[SALTS_FS_MAX_PATH];
  char native_service_source[SALTS_FS_MAX_PATH];
  char plugin_source[SALTS_FS_MAX_PATH];
  char plugin_service_header[SALTS_FS_MAX_PATH];
  char plugin_client_header[SALTS_FS_MAX_PATH];
  char plugin_client_source[SALTS_FS_MAX_PATH];
  char wasm_component_output[SALTS_FS_MAX_PATH];
  char wasm_host_header[SALTS_FS_MAX_PATH];
  char wasm_host_source[SALTS_FS_MAX_PATH];
  char wasm_guest_header[SALTS_FS_MAX_PATH];
  char wasm_symbol_prefix[256];
  char openapi_output[SALTS_FS_MAX_PATH];
  char http_projection_header[SALTS_FS_MAX_PATH];
  char rpc_projection_header[SALTS_FS_MAX_PATH];
  char socket_projection_header[SALTS_FS_MAX_PATH];
  char flowmq_projection_header[SALTS_FS_MAX_PATH];
} databind_compiler_projection_frontend_plan;

/*
 * Lower public selection inputs into one compiler-private generation request
 * set over one canonical IR.
 *
 * artifacts and transports are independent comma-separated canonical name
 * lists. Known but not-yet-public selections fail explicitly. Format selection
 * remains owned by DataBindFormat/FormatPlan and transport projection config.
 */
int databind_compiler_projection_frontend_build(
    const databind_compiler_projection_frontend_input *input,
    databind_compiler_projection_frontend_plan *out,
    char *error,
    size_t error_size);

void databind_compiler_projection_frontend_dispose(
    databind_compiler_projection_frontend_plan *plan);

#ifdef __cplusplus
}
#endif

#endif /* DATABIND_COMPILER_PROJECTION_FRONTEND_H */
